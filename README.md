# mameSpinLock

[![CI](https://github.com/ar90n/mameSpinLock/actions/workflows/ci.yml/badge.svg)](https://github.com/ar90n/mameSpinLock/actions/workflows/ci.yml)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Header Only](https://img.shields.io/badge/header--only-brightgreen.svg)](src/mameSpinLock.h)
[![x86-64](https://img.shields.io/badge/arch-x86--64-orange.svg)]()
![Built with vibe coding](https://img.shields.io/badge/built%20with-vibe%20coding-ff69b4)

A single-header spin-lock library built entirely from CPU instructions (CAS / barriers) with no OS synchronization primitives. Designed as a learning project, incrementally building up design decisions step by step.

## Key Characteristics

- **OS-independent** -- no futex, pthread mutex, or sleep-based locks
- **Single-header** -- just include `mameSpinLock.h`
- **x86-64 only** (v0)
- **C++17**, requires `g++` and `-pthread`

## Usage

```cpp
#include "mameSpinLock.h"

// TTAS SpinLock
mame_spinlock::SpinLockTTAS lock;
lock.lock();
// critical section
lock.unlock();

// TicketLock (FIFO fair)
mame_spinlock::TicketLock ticket;
ticket.lock();
// critical section
ticket.unlock();

// MCS Lock (scalable, local spinning)
mame_spinlock::MCSLock mcs;
mame_spinlock::MCSNode node;  // thread-local
mcs.lock(&node);
// critical section
mcs.unlock(&node);
```

## Build & Test

```bash
make        # build
make test   # run all tests
make clean  # clean
```

## API

### Public API

| Struct | Methods | Description |
|--------|---------|-------------|
| `SpinLockTTAS` | `lock()`, `unlock()`, `try_lock()` | Test-and-Test-and-Set spin lock |
| `TicketLock` | `lock()`, `unlock()` | Fair FIFO spin lock |
| `MCSLock` | `lock(MCSNode*)`, `unlock(MCSNode*)` | Scalable local-spinning lock |

### Internal API (`mame_spinlock::detail`)

| Function | Description |
|----------|-------------|
| `cpu_relax()` | PAUSE instruction (spin-loop hint) |
| `compiler_barrier()` | Compiler barrier |
| `cas_u64()` | Compare-And-Swap (uint64_t) |
| `cas_ptr<T>()` | Compare-And-Swap (pointer) |
| `exchange_ptr<T>()` | Atomic exchange (pointer) |
| `load_acquire_u64()` | Load with acquire semantics |
| `store_release_u64()` | Store with release semantics |

## Project Structure

```
mameSpinLock/
  Makefile
  src/
    mameSpinLock.h        # single-header library
  tests/
    utest.h               # test framework
    test_atomic.cpp        # atomic primitives tests
    test_spinlock.cpp      # SpinLockTTAS tests
    test_ticket.cpp        # TicketLock tests
    test_mcs.cpp           # MCS Lock tests
```

## Implementation Conventions

- All shared variable accesses go through the atomic API
- Inline assembly always includes `"memory"` clobber
- Lock state variables use `alignas(64)` to avoid false sharing
- No OS-dependent features (futex, etc.)
