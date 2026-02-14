/*
 * test_mcs.cpp - Tests for MCS Lock
 *
 * Step 6: MCS Lock (Mellor-Crummey and Scott Lock)
 */

#include "utest.h"
#include "mameSpinLock.h"

#include <thread>
#include <vector>

// =============================================================================
// Helper: Check if MCS Lock is implemented
// =============================================================================

static bool is_mcslock_implemented() {
    mame_spinlock::MCSLock lock;
    mame_spinlock::MCSNode node;

    // Try to lock - if implemented, should succeed and change state
    lock.lock(&node);
    bool tail_changed = (lock.tail != nullptr);
    lock.unlock(&node);

    return tail_changed;
}

// =============================================================================
// Step 6: MCS Lock tests
// =============================================================================

UTEST(mcslock, node_initial_state) {
    mame_spinlock::MCSNode node;

    // Initial state: next = nullptr, locked = 0
    ASSERT_TRUE(node.next == nullptr);
    ASSERT_TRUE(node.locked == 0);
}

UTEST(mcslock, lock_initial_state) {
    mame_spinlock::MCSLock lock;

    // Initial state: tail = nullptr
    ASSERT_TRUE(lock.tail == nullptr);
}

UTEST(mcslock, single_thread_lock_unlock) {
    if (!is_mcslock_implemented()) {
        UTEST_SKIP("MCS Lock not implemented yet");
    }

    mame_spinlock::MCSLock lock;
    mame_spinlock::MCSNode node;

    // Lock should succeed
    lock.lock(&node);

    // After lock, tail should point to our node
    ASSERT_TRUE(lock.tail == &node);

    // Unlock
    lock.unlock(&node);

    // After unlock with no waiters, tail should be null
    ASSERT_TRUE(lock.tail == nullptr);
}

UTEST(mcslock, concurrent_counter) {
    if (!is_mcslock_implemented()) {
        UTEST_SKIP("MCS Lock not implemented yet");
    }

    // Multiple threads increment a shared counter protected by MCS lock
    constexpr int N_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;

    mame_spinlock::MCSLock lock;
    uint64_t counter = 0;

    auto worker = [&]() {
        mame_spinlock::MCSNode node;  // Each thread has its own node
        for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
            lock.lock(&node);
            ++counter;
            lock.unlock(&node);
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

UTEST(mcslock, no_data_race) {
    if (!is_mcslock_implemented()) {
        UTEST_SKIP("MCS Lock not implemented yet");
    }

    // Test that lock properly protects critical section
    constexpr int N_THREADS = 4;
    constexpr int ITERATIONS = 5000;

    mame_spinlock::MCSLock lock;

    // Two values that should always be equal
    uint64_t value_a = 0;
    uint64_t value_b = 0;
    bool race_detected = false;

    auto worker = [&](int thread_id) {
        mame_spinlock::MCSNode node;
        for (int i = 0; i < ITERATIONS; ++i) {
            lock.lock(&node);

            // Check invariant
            if (value_a != value_b) {
                race_detected = true;
            }

            // Update both values
            uint64_t new_val = static_cast<uint64_t>(thread_id * 1000000 + i);
            value_a = new_val;
            value_b = new_val;

            lock.unlock(&node);
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
