/*
 * test_spinlock.cpp - Tests for SpinLockTTAS
 *
 * Step 4: SpinLockTTAS exclusion test
 */

#include "utest.h"
#include "mameSpinLock.h"

#include <thread>
#include <vector>

// =============================================================================
// Helper: Check if SpinLockTTAS is implemented
// =============================================================================

static bool is_spinlock_implemented() {
    mame_spinlock::SpinLockTTAS lock;
    // If try_lock returns true (stub returns false), it's implemented
    bool result = lock.try_lock();
    if (result) {
        lock.unlock();
    }
    return result;
}

// =============================================================================
// Step 4: SpinLockTTAS tests
// =============================================================================

UTEST(spinlock_ttas, try_lock_basic) {
    mame_spinlock::SpinLockTTAS lock;

    // First try_lock should succeed
    bool first = lock.try_lock();
    ASSERT_TRUE(first);

    // Second try_lock should fail (already locked)
    bool second = lock.try_lock();
    ASSERT_FALSE(second);

    lock.unlock();

    // After unlock, try_lock should succeed again
    bool third = lock.try_lock();
    ASSERT_TRUE(third);
    lock.unlock();
}

UTEST(spinlock_ttas, lock_unlock_basic) {
    if (!is_spinlock_implemented()) {
        UTEST_SKIP("SpinLockTTAS not implemented yet");
    }

    mame_spinlock::SpinLockTTAS lock;

    // Lock should not block on first call
    lock.lock();

    // try_lock should fail while locked
    bool result = lock.try_lock();
    ASSERT_FALSE(result);

    // Unlock should release
    lock.unlock();

    // Now try_lock should succeed
    result = lock.try_lock();
    ASSERT_TRUE(result);
    lock.unlock();
}

UTEST(spinlock_ttas, concurrent_counter) {
    if (!is_spinlock_implemented()) {
        UTEST_SKIP("SpinLockTTAS not implemented yet");
    }

    // Multiple threads increment a shared counter protected by spinlock
    // Final value should equal N_THREADS * INCREMENTS_PER_THREAD
    constexpr int N_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;

    mame_spinlock::SpinLockTTAS lock;
    uint64_t counter = 0;  // Shared counter (not volatile - protected by lock)

    auto worker = [&]() {
        for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
            lock.lock();
            ++counter;
            lock.unlock();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    uint64_t expected = static_cast<uint64_t>(N_THREADS) * INCREMENTS_PER_THREAD;
    ASSERT_TRUE(counter == expected);
}

UTEST(spinlock_ttas, no_data_race) {
    if (!is_spinlock_implemented()) {
        UTEST_SKIP("SpinLockTTAS not implemented yet");
    }

    // Test that lock properly protects critical section
    // Multiple threads update a multi-word value atomically
    constexpr int N_THREADS = 4;
    constexpr int ITERATIONS = 5000;

    mame_spinlock::SpinLockTTAS lock;

    // Two values that should always be equal
    uint64_t value_a = 0;
    uint64_t value_b = 0;
    bool race_detected = false;

    auto worker = [&](int thread_id) {
        for (int i = 0; i < ITERATIONS; ++i) {
            lock.lock();

            // Check invariant: value_a should equal value_b
            if (value_a != value_b) {
                race_detected = true;
            }

            // Update both values
            uint64_t new_val = static_cast<uint64_t>(thread_id * 1000000 + i);
            value_a = new_val;
            value_b = new_val;

            lock.unlock();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    ASSERT_FALSE(race_detected);
    ASSERT_TRUE(value_a == value_b);
}

UTEST_MAIN();
