/*
 * mameSpinLock - Single-header spin lock library
 *
 * Learning-oriented implementation using only CPU instructions (CAS/barriers).
 * No OS-dependent primitives (futex, pthread mutex, etc.)
 *
 * Supported architectures: x86-64, aarch64
 */

#ifndef MAME_SPINLOCK_H
#define MAME_SPINLOCK_H

#include <cstdint>

// Architecture check
#if !defined(__x86_64__) && !defined(__aarch64__)
#error "mameSpinLock supports x86-64 and aarch64 only"
#endif

namespace mame_spinlock {

// =============================================================================
// Internal API (detail namespace)
// =============================================================================
namespace detail {

// -----------------------------------------------------------------------------
// CPU/Compiler primitives
// -----------------------------------------------------------------------------

/**
 * cpu_relax - Hint to CPU that we are in a spin loop
 * x86-64: PAUSE instruction
 * aarch64: YIELD instruction
 */
inline void cpu_relax() {
#if defined(__x86_64__)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#endif
}

/**
 * compiler_barrier - Prevent compiler reordering
 *
 */
inline void compiler_barrier() {
    asm volatile("" ::: "memory");
}

/**
 * fence_seq_cst - Full memory fence (sequential consistency)
 * x86-64: MFENCE instruction
 * aarch64: DMB ISH (inner shareable full barrier)
 */
inline void fence_seq_cst() {
#if defined(__x86_64__)
    asm volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("dmb ish" ::: "memory");
#endif
}

// -----------------------------------------------------------------------------
// Atomic operations
// -----------------------------------------------------------------------------

/**
 * cas_u64 - Compare-And-Swap for uint64_t
 *
 * Atomically: if (*p == *expected) { *p = desired; return true; }
 *             else { *expected = *p; return false; }
 *
 * x86-64: LOCK CMPXCHGQ
 * aarch64: LDXR/STXR (LL/SC) loop with full barriers
 */
inline bool cas_u64(volatile uint64_t* p, uint64_t* expected, uint64_t desired) {
#if defined(__x86_64__)
    bool success;
    asm volatile(
        "lock cmpxchgq %[desired], %[ptr]"
        : "=@ccz" (success),
          "+a" (*expected),
          [ptr] "+m" (*p)
        : [desired] "r" (desired)
        : "memory"
    );
    return success;
#elif defined(__aarch64__)
    uint64_t old_val;
    uint32_t tmp;
    asm volatile(
        "1:\n"
        "    ldxr   %[old], %[ptr]\n"
        "    cmp    %[old], %[exp]\n"
        "    b.ne   2f\n"
        "    stxr   %w[tmp], %[des], %[ptr]\n"
        "    cbnz   %w[tmp], 1b\n"
        "    dmb    ish\n"
        "    mov    %[old], %[exp]\n"
        "    b      3f\n"
        "2:\n"
        "    clrex\n"
        "3:\n"
        : [old] "=&r" (old_val),
          [tmp] "=&r" (tmp),
          [ptr] "+Q" (*p)
        : [exp] "r" (*expected),
          [des] "r" (desired)
        : "memory", "cc"
    );
    if (old_val != *expected) {
        *expected = old_val;
        return false;
    }
    return true;
#endif
}

/**
 * cas_ptr - Compare-And-Swap for pointers
 *
 * Atomically: if (*p == *expected) { *p = desired; return true; }
 *             else { *expected = *p; return false; }
 *
 * x86-64: LOCK CMPXCHGQ (pointers are 64-bit)
 * aarch64: LDXR/STXR (LL/SC) loop with full barriers
 */
template<typename T>
inline bool cas_ptr(volatile T** p, T** expected, T* desired) {
#if defined(__x86_64__)
    bool success;
    asm volatile(
        "lock cmpxchgq %[desired], %[ptr]"
        : "=@ccz" (success),
          "+a" (*expected),
          [ptr] "+m" (*p)
        : [desired] "r" (desired)
        : "memory"
    );
    return success;
#elif defined(__aarch64__)
    T* old_val;
    uint32_t tmp;
    asm volatile(
        "1:\n"
        "    ldxr   %[old], %[ptr]\n"
        "    cmp    %[old], %[exp]\n"
        "    b.ne   2f\n"
        "    stxr   %w[tmp], %[des], %[ptr]\n"
        "    cbnz   %w[tmp], 1b\n"
        "    dmb    ish\n"
        "    mov    %[old], %[exp]\n"
        "    b      3f\n"
        "2:\n"
        "    clrex\n"
        "3:\n"
        : [old] "=&r" (old_val),
          [tmp] "=&r" (tmp),
          [ptr] "+Q" (*p)
        : [exp] "r" (*expected),
          [des] "r" (desired)
        : "memory", "cc"
    );
    if (old_val != *expected) {
        *expected = old_val;
        return false;
    }
    return true;
#endif
}

/**
 * exchange_ptr - Atomic exchange for pointers
 *
 * Atomically: old = *p; *p = new_val; return old;
 *
 * x86-64: XCHGQ (implicitly locked)
 * aarch64: LDXR/STXR (LL/SC) loop with full barriers
 */
template<typename T>
inline T* exchange_ptr(volatile T** p, T* new_val) {
#if defined(__x86_64__)
    asm volatile(
        "xchgq %[new_val], %[ptr]"
        : [new_val] "+r" (new_val),
          [ptr] "+m" (*p)
        :
        : "memory"
    );
    return new_val;
#elif defined(__aarch64__)
    T* old_val;
    uint32_t tmp;
    asm volatile(
        "dmb    ish\n"
        "1:\n"
        "    ldxr   %[old], %[ptr]\n"
        "    stxr   %w[tmp], %[new_val], %[ptr]\n"
        "    cbnz   %w[tmp], 1b\n"
        "dmb    ish\n"
        : [old] "=&r" (old_val),
          [tmp] "=&r" (tmp),
          [ptr] "+Q" (*p)
        : [new_val] "r" (new_val)
        : "memory"
    );
    return old_val;
#endif
}
/**
 * load_acquire_u64 - Load with acquire semantics
 *
 * x86-64: Regular load + compiler barrier (TSO makes HW fence unnecessary)
 * aarch64: LDAR instruction (load-acquire, HW fence required for weak model)
 */
inline uint64_t load_acquire_u64(const volatile uint64_t* p) {
#if defined(__x86_64__)
    auto const ret = *p;
    compiler_barrier();
    return ret;
#elif defined(__aarch64__)
    uint64_t ret;
    asm volatile(
        "ldar %[ret], %[ptr]"
        : [ret] "=r" (ret)
        : [ptr] "Q" (*p)
        : "memory"
    );
    return ret;
#endif
}

/**
 * store_release_u64 - Store with release semantics
 *
 * x86-64: Compiler barrier + regular store (TSO makes HW fence unnecessary)
 * aarch64: STLR instruction (store-release, HW fence required for weak model)
 */
inline void store_release_u64(volatile uint64_t* p, uint64_t v) {
#if defined(__x86_64__)
    compiler_barrier();
    *p = v;
#elif defined(__aarch64__)
    asm volatile(
        "stlr %[val], %[ptr]"
        : [ptr] "=Q" (*p)
        : [val] "r" (v)
        : "memory"
    );
#endif
}

} // namespace detail

// =============================================================================
// Public API
// =============================================================================

/**
 * SpinLockTTAS - Test-and-Test-and-Set SpinLock
 *
 * Simple spin lock using TTAS algorithm:
 * - First test (read) without atomic operation
 * - Then try to acquire with CAS
 *
 * Internal state: 0 = free, 1 = locked
 */
struct SpinLockTTAS {
    alignas(64) volatile uint64_t state = 0;

    /**
     * lock - Acquire the lock (blocking)
     */
    void lock() {
        while(true) {
            while(detail::load_acquire_u64(&state) != 0) {
                detail::cpu_relax();
            }
            uint64_t expected = 0;
            if(detail::cas_u64(&state, &expected, 1)) {
                return;
            }
        }
    }

    /**
     * unlock - Release the lock
     */
    void unlock() {
        detail::store_release_u64(&state, 0);
    }

    /**
     * try_lock - Try to acquire the lock (non-blocking)
     * Returns true if lock acquired, false otherwise
     */
    bool try_lock() {
        uint64_t expected = 0;
        return detail::cas_u64(&state, &expected, 1);
    }
};

/**
 * TicketLock - Fair FIFO spin lock
 *
 * Uses ticket system to ensure fairness:
 * - next: Next ticket number to be issued
 * - owner: Current ticket being served
 *
 * fetch_add is synthesized using CAS loop
 */
struct TicketLock {
    alignas(64) volatile uint64_t next = 0;
    alignas(64) volatile uint64_t owner = 0;

    /**
     * lock - Acquire the lock (blocking, FIFO order)
     */
    void lock() {
        uint64_t const my_ticket = fetch_add_u64(&next, 1);
        while(detail::load_acquire_u64(&owner) != my_ticket) {
            detail::cpu_relax();
        }
    }

    /**
     * unlock - Release the lock
     */
    void unlock() {
        detail::store_release_u64(&owner, detail::load_acquire_u64(&owner) + 1);
    }

private:
    static inline uint64_t fetch_add_u64(volatile uint64_t *p, uint64_t v) {
        uint64_t old;
        do{
            old = detail::load_acquire_u64(p);
        } while(!detail::cas_u64(p, &old, old + v));
        return old;
    }
};

/**
 * MCSNode - Node for MCS Lock queue
 *
 * Each thread must have its own MCSNode.
 * The node is used to form a linked list of waiting threads.
 */
struct MCSNode {
    alignas(64) volatile MCSNode* next = nullptr;
    alignas(64) volatile uint64_t locked = 0;  // 0 = unlocked, 1 = locked (waiting)
};

/**
 * MCSLock - Mellor-Crummey and Scott Lock
 *
 * Scalable spin lock where each thread spins on its own local variable.
 * This reduces cache line contention significantly.
 *
 * Features:
 * - Fair (FIFO ordering)
 * - Scalable (local spinning)
 * - Each thread must provide its own MCSNode
 *
 * Algorithm:
 * lock():
 *   1. Atomically swap tail with my node
 *   2. If there was a predecessor, link to it and spin on my node's locked
 *
 * unlock():
 *   1. If no successor, try to CAS tail to null
 *   2. If successor exists, set its locked to 0
 */
struct MCSLock {
    alignas(64) volatile MCSNode* tail = nullptr;

    /**
     * lock - Acquire the lock
     * @param node: Thread-local MCSNode (must be provided by caller)
     *
     * TODO: Implement MCS lock algorithm
     */
    void lock(MCSNode* node) {
        node->next = nullptr;
        node->locked = 1;

        MCSNode* pred = detail::exchange_ptr(&tail, node);
        if(pred != nullptr) {
            pred->next = node;
            while(node->locked) {
            detail::cpu_relax();
            }
        }
    }

    /**
     * unlock - Release the lock
     * @param node: The same MCSNode used in lock()
     *
     */
    void unlock(MCSNode* node) {
        if(node->next == nullptr) {
            MCSNode* expected = node;
            if(detail::cas_ptr(&tail, &expected, (MCSNode*)(nullptr))) {
                return;
            }
            while(node->next == nullptr) {
                detail::cpu_relax();
            }
        }
        node->next->locked = 0;
    }
};

} // namespace mame_spinlock

#endif // MAME_SPINLOCK_H
