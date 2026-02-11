/*
 * mameSpinLock - Single-header spin lock library
 *
 * Learning-oriented implementation using only CPU instructions (CAS/barriers).
 * No OS-dependent primitives (futex, pthread mutex, etc.)
 *
 * Initial target: x86-64 only
 */

#ifndef MAME_SPINLOCK_H
#define MAME_SPINLOCK_H

#include <cstdint>

// Architecture check: x86-64 only for v0
#if !defined(__x86_64__)
#error "mameSpinLock v0 supports x86-64 only"
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
 *
 */
inline void cpu_relax() {
    asm volatile("pause" ::: "memory");
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
 *
 * TODO: Implement with inline assembly (optional for v0)
 */
inline void fence_seq_cst() {
    // TODO: asm volatile("mfence" ::: "memory");
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
 *
 */
inline bool cas_u64(volatile uint64_t* p, uint64_t* expected, uint64_t desired) {
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
}

/**
 * load_acquire_u64 - Load with acquire semantics
 *
 * x86-64: Regular load + compiler barrier (x86 has strong memory model)
 *
 * TODO: Implement
 */
inline uint64_t load_acquire_u64(const volatile uint64_t* p) {
    (void)p;
    // TODO: Implement
    return 0;
}

/**
 * store_release_u64 - Store with release semantics
 *
 * x86-64: Compiler barrier + regular store (x86 has strong memory model)
 *
 * TODO: Implement
 */
inline void store_release_u64(volatile uint64_t* p, uint64_t v) {
    (void)p; (void)v;
    // TODO: Implement
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
     * TODO: Implement TTAS algorithm
     */
    void lock() {
        // TODO: Implement
    }

    /**
     * unlock - Release the lock
     * TODO: Implement
     */
    void unlock() {
        // TODO: Implement
    }

    /**
     * try_lock - Try to acquire the lock (non-blocking)
     * Returns true if lock acquired, false otherwise
     * TODO: Implement
     */
    bool try_lock() {
        // TODO: Implement
        return false;
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
     * TODO: Implement using CAS-based fetch_add
     */
    void lock() {
        // TODO: Implement
    }

    /**
     * unlock - Release the lock
     * TODO: Implement
     */
    void unlock() {
        // TODO: Implement
    }
};

} // namespace mame_spinlock

#endif // MAME_SPINLOCK_H
