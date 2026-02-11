/*
 * test_atomic.cpp - Tests for atomic primitives
 *
 * Step 0: Smoke test only (header compiles)
 * Step 1: cpu_relax / compiler_barrier tests
 * Step 2: CAS(u64) tests
 * Step 3: load_acquire / store_release tests
 */

#include "utest.h"
#include "mameSpinLock.h"

#include <thread>
#include <vector>

// =============================================================================
// Step 0: Smoke test - verify header compiles
// =============================================================================

UTEST(smoke, header_compiles) {
    // Just verify the header compiles and basic types are accessible
    mame_spinlock::SpinLockTTAS lock1;
    mame_spinlock::TicketLock lock2;

    // Suppress unused variable warnings
    (void)lock1;
    (void)lock2;

    ASSERT_TRUE(true);
}

UTEST(smoke, struct_sizes) {
    // Verify structs have expected alignment
    ASSERT_TRUE(alignof(mame_spinlock::SpinLockTTAS) >= 64);
    ASSERT_TRUE(alignof(mame_spinlock::TicketLock) >= 64);
}

// =============================================================================
// Step 1: cpu_relax / compiler_barrier - verify callable
// =============================================================================

UTEST(primitives, cpu_relax_callable) {
    // Verify cpu_relax can be called without crashing
    // After user implements: should execute PAUSE instruction
    for (int i = 0; i < 100; ++i) {
        mame_spinlock::detail::cpu_relax();
    }
    ASSERT_TRUE(true);
}

UTEST(primitives, compiler_barrier_callable) {
    // Verify compiler_barrier can be called without crashing
    // After user implements: should prevent compiler reordering
    volatile int x = 0;
    x = 1;
    mame_spinlock::detail::compiler_barrier();
    x = 2;
    mame_spinlock::detail::compiler_barrier();
    ASSERT_TRUE(x == 2);
}

UTEST(primitives, fence_seq_cst_callable) {
    // Verify fence_seq_cst can be called without crashing
    // After user implements: should execute MFENCE instruction
    mame_spinlock::detail::fence_seq_cst();
    ASSERT_TRUE(true);
}

// =============================================================================
// Step 2: CAS(u64) tests
// =============================================================================

UTEST(cas, single_thread_success) {
    // CAS should succeed when *p == expected
    volatile uint64_t value = 42;
    uint64_t expected = 42;
    uint64_t desired = 100;

    bool result = mame_spinlock::detail::cas_u64(&value, &expected, desired);

    // After user implements: result should be true, value should be 100
    ASSERT_TRUE(result);
    ASSERT_TRUE(value == desired);
}

UTEST(cas, single_thread_failure) {
    // CAS should fail when *p != expected
    volatile uint64_t value = 42;
    uint64_t expected = 999;  // Wrong expected value
    uint64_t desired = 100;

    bool result = mame_spinlock::detail::cas_u64(&value, &expected, desired);

    // After user implements: result should be false, expected should be updated to 42
    ASSERT_FALSE(result);
    ASSERT_TRUE(expected == 42);  // expected updated to actual value
    ASSERT_TRUE(value == 42);     // value unchanged
}

// Helper: Check if CAS is implemented (not just a stub)
static bool is_cas_implemented() {
    volatile uint64_t test_val = 42;
    uint64_t expected = 42;
    return mame_spinlock::detail::cas_u64(&test_val, &expected, 100);
}

// Helper: CAS-based fetch_add for concurrent test
static uint64_t fetch_add_u64(volatile uint64_t* p, uint64_t addend) {
    uint64_t old_val;
    do {
        old_val = *p;
    } while (!mame_spinlock::detail::cas_u64(p, &old_val, old_val + addend));
    return old_val;
}

UTEST(cas, concurrent_fetch_add) {
    // Skip if CAS is not implemented yet (would cause infinite loop)
    if (!is_cas_implemented()) {
        UTEST_SKIP("CAS not implemented yet - skipping concurrent test");
    }

    // Multiple threads increment a counter using CAS-based fetch_add
    // Final value should equal N_THREADS * INCREMENTS_PER_THREAD
    constexpr int N_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;

    alignas(64) volatile uint64_t counter = 0;

    auto worker = [&]() {
        for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
            fetch_add_u64(&counter, 1);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // After user implements: counter should equal N_THREADS * INCREMENTS_PER_THREAD
    uint64_t expected_final = static_cast<uint64_t>(N_THREADS) * INCREMENTS_PER_THREAD;
    ASSERT_TRUE(counter == expected_final);
}

// =============================================================================
// Step 3: load_acquire / store_release tests
// =============================================================================

// Helper: Check if load_acquire is implemented
static bool is_load_acquire_implemented() {
    volatile uint64_t test_val = 42;
    return mame_spinlock::detail::load_acquire_u64(&test_val) == 42;
}

UTEST(memory_order, load_acquire_basic) {
    // Basic test: load_acquire should return the current value
    volatile uint64_t value = 12345;
    uint64_t loaded = mame_spinlock::detail::load_acquire_u64(&value);
    ASSERT_TRUE(loaded == 12345);
}

UTEST(memory_order, store_release_basic) {
    // Basic test: store_release should store the value
    volatile uint64_t value = 0;
    mame_spinlock::detail::store_release_u64(&value, 99999);
    ASSERT_TRUE(value == 99999);
}

UTEST(memory_order, message_passing) {
    // Skip if not implemented
    if (!is_load_acquire_implemented()) {
        UTEST_SKIP("load_acquire not implemented yet");
    }

    // Classic message passing pattern:
    // Producer: writes data, then sets flag (store_release)
    // Consumer: reads flag (load_acquire), then reads data
    //
    // Without proper ordering, consumer might see flag=1 but stale data
    constexpr int ITERATIONS = 10000;

    alignas(64) volatile uint64_t data = 0;
    alignas(64) volatile uint64_t flag = 0;

    bool success = true;

    for (int iter = 0; iter < ITERATIONS && success; ++iter) {
        data = 0;
        flag = 0;

        std::thread producer([&]() {
            // Write data first, then set flag with release semantics
            data = iter + 1;
            mame_spinlock::detail::store_release_u64(&flag, 1);
        });

        std::thread consumer([&]() {
            // Spin until flag is set (with acquire semantics)
            while (mame_spinlock::detail::load_acquire_u64(&flag) == 0) {
                mame_spinlock::detail::cpu_relax();
            }
            // After seeing flag=1, data must be visible
            if (data != static_cast<uint64_t>(iter + 1)) {
                success = false;
            }
        });

        producer.join();
        consumer.join();
    }

    ASSERT_TRUE(success);
}

UTEST_MAIN();
