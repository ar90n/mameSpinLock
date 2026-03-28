# mameSpinLock 実装解説 -- メモリモデルと x86-64 固有事項

## 1. 全体構成

本ライブラリは OS の同期プリミティブ (futex, pthread mutex 等) を一切使わず、CPU 命令 (CAS / バリア) だけでスピンロックを実装している。提供するロックは 3 種類:

| ロック | 特徴 |
|--------|------|
| **SpinLockTTAS** | Test-and-Test-and-Set。単純だが不公平 |
| **TicketLock** | チケット方式で FIFO 公平性を保証 |
| **MCSLock** | 各スレッドがローカル変数でスピンし、キャッシュライン競合を低減 |

---

## 2. 低レベルプリミティブ (`detail` 名前空間)

### 2.1 `cpu_relax()` -- PAUSE 命令

```cpp
inline void cpu_relax() {
    asm volatile("pause" ::: "memory");
}
```

x86-64 の `PAUSE` 命令は **スピンループ専用のヒント** である。効果は 2 つ:

1. **パイプラインの投機実行を抑制**: スピン中にストアを投機的に実行すると、ロック解放時にメモリオーダリング違反 (memory order violation) が検出されてパイプラインフラッシュが発生する。`PAUSE` を入れるとこのペナルティを回避できる。
2. **消費電力の削減**: CPU に「ビジーウェイト中」と伝えることで、ハイパースレッディング環境では兄弟スレッドにリソースを譲れる。

`"memory"` clobber はコンパイラバリアも兼ねている。

### 2.2 `compiler_barrier()` -- コンパイラバリア

```cpp
inline void compiler_barrier() {
    asm volatile("" ::: "memory");
}
```

空のインラインアセンブリに `"memory"` clobber を付けると、**コンパイラがこの前後でメモリアクセスを並べ替えることを禁止** する。CPU レベルのフェンスではなく、あくまでコンパイラの最適化を抑制するだけ。

x86-64 では CPU のメモリモデルが強い (後述) ため、多くの場面でコンパイラバリアだけで十分になる。

### 2.3 `cas_u64()` / `cas_ptr()` -- Compare-And-Swap

```cpp
asm volatile(
    "lock cmpxchgq %[desired], %[ptr]"
    : "=@ccz" (success),
      "+a" (*expected),
      [ptr] "+m" (*p)
    : [desired] "r" (desired)
    : "memory"
);
```

**`LOCK CMPXCHGQ`** は x86-64 の CAS 命令。動作:

1. `RAX` (= `*expected`) と `*p` を比較
2. 等しければ `*p = desired` を書き込み、ZF=1 (success=true)
3. 等しくなければ `*expected = *p` (現在値を返す)、ZF=0 (success=false)

重要なポイント:

- **`LOCK` プレフィックス**: キャッシュラインをロックして原子性を保証する。MESI プロトコル上では、該当キャッシュラインを Exclusive/Modified 状態に遷移させ、他コアのキャッシュを無効化する。
- **`"memory"` clobber**: コンパイラバリアとしても機能し、この CAS の前後で他のメモリアクセスが並べ替わらない。
- **`=@ccz` 制約**: GCC 拡張で、条件コードフラグ (ZF) を直接 bool 変数にマッピングする。`setz` 命令の生成を回避でき効率的。
- **`+a` 制約**: `*expected` を `RAX` レジスタに配置する (`CMPXCHG` が暗黙に使うレジスタ)。

#### x86-64 における `LOCK` プレフィックスの暗黙的保証

`LOCK` プレフィックス付き命令は **フルメモリフェンス** として機能する (Intel SDM Vol. 3A, Section 8.2.3.9)。つまり:

- `LOCK` 前のすべてのロード/ストアは、`LOCK` 命令より前にグローバルに可視化される
- `LOCK` 後のすべてのロード/ストアは、`LOCK` 命令より後にグローバルに可視化される

これは C++ の `memory_order_seq_cst` に相当する強さを持つ。

### 2.4 `exchange_ptr()` -- Atomic Exchange

```cpp
asm volatile(
    "xchgq %[new_val], %[ptr]"
    : [new_val] "+r" (new_val),
      [ptr] "+m" (*p)
    :
    : "memory"
);
```

x86-64 では **`XCHG` は暗黙的に `LOCK` プレフィックスが付く** (Intel SDM Vol. 2B)。明示的に `lock` を書く必要はなく、常に原子的かつフルフェンス。MCS ロックの `tail` の入れ替えに使用される。

### 2.5 `load_acquire_u64()` / `store_release_u64()` -- Acquire/Release セマンティクス

```cpp
// Acquire: load + compiler barrier
inline uint64_t load_acquire_u64(const volatile uint64_t* p) {
    auto const ret = *p;
    compiler_barrier();
    return ret;
}

// Release: compiler barrier + store
inline void store_release_u64(volatile uint64_t* p, uint64_t v) {
    compiler_barrier();
    *p = v;
}
```

**ここが x86-64 のメモリモデルの恩恵を最も受ける部分。**

#### x86-64 の TSO (Total Store Order) メモリモデル

x86-64 は **TSO (Total Store Order)** というメモリモデルを採用している。TSO の保証:

| 並び替え | 許可? |
|----------|-------|
| Load → Load | **禁止** (ロード同士は順序維持) |
| Load → Store | **禁止** |
| Store → Store | **禁止** (ストア同士も順序維持) |
| **Store → Load** | **許可** (唯一の並び替え) |

つまり、x86-64 で発生しうる並び替えは **Store の後の Load が先に実行される** ケースだけ。これは Store Buffer に書き込みが滞留し、後続の Load が先に完了するために起こる。

この強いモデルのおかげで:

- **Acquire load**: 通常の load + コンパイラバリアで十分。CPU は Load → Load、Load → Store の並び替えをしないため、CPU レベルのフェンス命令は不要。コンパイラが最適化で並び替えないようにするだけで良い。
- **Release store**: コンパイラバリア + 通常の store で十分。CPU は Store → Store の並び替えをしないため、同様にコンパイラバリアだけで良い。

**ARM や RISC-V のような弱いメモリモデルのアーキテクチャでは、これらの操作にハードウェアフェンス命令 (`dmb` / `fence` 等) が必要になる。** x86-64 が本ライブラリの初期ターゲットである理由の一つ。

---

## 3. SpinLockTTAS -- Test-and-Test-and-Set

```cpp
void lock() {
    while(true) {
        // Test (読むだけ): ロックが空くまでスピン
        while(detail::load_acquire_u64(&state) != 0) {
            detail::cpu_relax();
        }
        // Test-and-Set: CAS でロック獲得を試みる
        uint64_t expected = 0;
        if(detail::cas_u64(&state, &expected, 1)) {
            return;
        }
    }
}
```

### なぜ TAS ではなく TTAS か

単純な TAS (Test-and-Set) はループの毎回 `LOCK CMPXCHG` を実行する。`LOCK` プレフィックス付き命令は **キャッシュラインを排他状態にする** ため、複数コアが同時にスピンすると、キャッシュラインが絶えずバウンスし (キャッシュラインバウンシング)、バス帯域を大量消費する。

TTAS では内側ループで **通常のロード** (`load_acquire_u64`) だけを使い、キャッシュラインを Shared 状態で読む。ロックが解放されたときだけ `LOCK CMPXCHG` を発行するため、キャッシュトラフィックが大幅に減る。

### メモリオーダリングの正しさ

- `lock()`: `cas_u64` は `LOCK CMPXCHG` であり、フルフェンスとして機能する。ロック獲得後のクリティカルセクション内のメモリアクセスがロック獲得前に並び替わることはない。
- `unlock()`: `store_release_u64` はリリースセマンティクス。クリティカルセクション内のすべてのストアが、`state = 0` より前にグローバルに可視化される。x86-64 の TSO モデルにより、コンパイラバリアだけで保証される。

### `alignas(64)` -- False Sharing の回避

```cpp
alignas(64) volatile uint64_t state = 0;
```

x86-64 のキャッシュラインは通常 64 バイト。`state` を 64 バイト境界に配置することで、他のデータと同じキャッシュラインに載ることを防ぎ、false sharing (偽共有) を回避する。

---

## 4. TicketLock -- 公平なスピンロック

```cpp
struct TicketLock {
    alignas(64) volatile uint64_t next = 0;   // 次に発行するチケット番号
    alignas(64) volatile uint64_t owner = 0;  // 現在サービス中の番号

    void lock() {
        uint64_t const my_ticket = fetch_add_u64(&next, 1);
        while(detail::load_acquire_u64(&owner) != my_ticket) {
            detail::cpu_relax();
        }
    }

    void unlock() {
        detail::store_release_u64(&owner, detail::load_acquire_u64(&owner) + 1);
    }
};
```

### fetch_add の CAS ループによる合成

```cpp
static inline uint64_t fetch_add_u64(volatile uint64_t *p, uint64_t v) {
    uint64_t old;
    do {
        old = detail::load_acquire_u64(p);
    } while(!detail::cas_u64(p, &old, old + v));
    return old;
}
```

x86-64 には `LOCK XADD` という fetch-and-add 命令があるが、本ライブラリは学習目的のため CAS ループで実装している。CAS が失敗した場合、`expected` に現在値が入るため、リトライコストは低い。

### next と owner が別キャッシュライン

`next` と `owner` は `alignas(64)` で別々のキャッシュラインに配置されている。これにより:

- ロック獲得側 (`next` への fetch_add) とスピン側 (`owner` の読み取り) が異なるキャッシュラインで動作
- ロック解放側 (`owner` への書き込み) が `next` のキャッシュラインに影響しない

### TTAS との比較

TicketLock は **FIFO 公平性** を保証するが、全スレッドが同じ `owner` 変数でスピンするため、ロック解放時にキャッシュラインの無効化が全コアに波及する (thundering herd)。次の MCSLock がこの問題を解決する。

---

## 5. MCSLock -- スケーラブルなローカルスピニング

```cpp
struct MCSNode {
    alignas(64) volatile MCSNode* next = nullptr;
    alignas(64) volatile uint64_t locked = 0;
};

struct MCSLock {
    alignas(64) volatile MCSNode* tail = nullptr;
    // ...
};
```

### lock アルゴリズム

```cpp
void lock(MCSNode* node) {
    node->next = nullptr;
    node->locked = 1;

    // 1. tail を自分のノードにアトミックに入れ替え
    MCSNode* pred = detail::exchange_ptr(&tail, node);

    if (pred != nullptr) {
        // 2. 先行者がいる: 先行者のリストに自分を繋ぎ、自分の locked でスピン
        pred->next = node;
        while (node->locked) {
            detail::cpu_relax();
        }
    }
    // pred == nullptr ならロック取得成功 (キューが空だった)
}
```

**ポイント**: `exchange_ptr` は `XCHGQ` を使い、暗黙の `LOCK` プレフィックスによりアトミックかつフルフェンス。

**ローカルスピニング**: 各スレッドは自分の `MCSNode::locked` でスピンする。このノードは各スレッド固有なので、他スレッドのスピンとキャッシュラインを共有しない。これが TTAS や TicketLock と決定的に異なる点。

### unlock アルゴリズム

```cpp
void unlock(MCSNode* node) {
    if (node->next == nullptr) {
        // 後続者がいないように見える → tail を nullptr に CAS
        MCSNode* expected = node;
        if (detail::cas_ptr(&tail, &expected, (MCSNode*)(nullptr))) {
            return;  // キューが空になった
        }
        // CAS 失敗: 誰かが tail を更新したが next がまだ設定されていない
        // → next が設定されるのを待つ
        while (node->next == nullptr) {
            detail::cpu_relax();
        }
    }
    // 後続者の locked を 0 にして起こす
    node->next->locked = 0;
}
```

unlock 時の CAS 失敗パスは **レースコンディション** への対処:

1. スレッド A が `unlock` を開始、`node->next == nullptr` を確認
2. スレッド B が `lock` を開始、`exchange_ptr(&tail, nodeB)` で `tail` を `nodeB` に更新
3. スレッド A の `cas_ptr(&tail, &expected, nullptr)` は失敗 (`tail` はもう `nodeB`)
4. スレッド B はまだ `pred->next = nodeB` を実行していない可能性がある
5. スレッド A は `node->next` が non-null になるまでスピンして待つ

### メモリオーダリングの分析

- `exchange_ptr` (`XCHGQ`): 暗黙の `LOCK` プレフィックスによりフルフェンス。`node->locked = 1` の書き込みが `tail` の更新前にグローバルに可視化されることを保証。
- `pred->next = node`: 通常のストア。x86-64 の TSO により、先行する `exchange_ptr` のフルフェンス以降にグローバルに可視化される。
- `node->next->locked = 0`: 通常のストア。x86-64 TSO により、先行するすべてのストア (クリティカルセクション内の書き込み含む) の後にグローバルに可視化される。

---

## 6. `volatile` vs `std::atomic` について

本ライブラリは `std::atomic` を使わず、`volatile` + インラインアセンブリで実装している。

- `volatile` はコンパイラに「この変数への読み書きを最適化で省略・並べ替えするな」と伝える (C++ 標準上の保証)
- ただし `volatile` は **スレッド間の可視性を保証しない** (C++ 標準上はデータ競合は未定義動作)
- 本ライブラリでは、すべての共有変数アクセスをインラインアセンブリ (`LOCK CMPXCHG`, `XCHG`) またはコンパイラバリア付きの load/store 経由で行うことで正しさを担保している
- これは学習目的の設計であり、プロダクションコードでは `std::atomic` を使うべき

---

## 7. まとめ: x86-64 の TSO が本実装にもたらす簡潔さ

| 操作 | ARM/RISC-V (弱いモデル) | x86-64 (TSO) |
|------|------------------------|---------------|
| Acquire load | `ldar` / `fence` + load | 通常 load + **コンパイラバリアのみ** |
| Release store | store + `stlr` / `fence` | **コンパイラバリアのみ** + 通常 store |
| CAS | LL/SC ループ + barrier | `LOCK CMPXCHGQ` (フルフェンス内蔵) |
| Atomic exchange | LL/SC ループ + barrier | `XCHGQ` (暗黙 LOCK、フルフェンス内蔵) |

x86-64 の TSO モデルにより、acquire/release セマンティクスがコンパイラバリアだけで実現でき、実装が大幅に簡潔になっている。これが「x86-64 only」と限定している理由であり、他アーキテクチャに移植する際にはハードウェアフェンス命令の追加が必要になる。
