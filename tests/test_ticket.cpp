/*
 * test_ticket.cpp - Tests for TicketLock
 *
 * Step 5: TicketLock exclusion and fairness test
 */

#include "utest.h"
#include "mameSpinLock.h"

#include <thread>
#include <vector>
#include <atomic>

// =============================================================================
// Helper: Check if TicketLock is implemented
// =============================================================================

// We check if lock/unlock changes the internal state properly
static bool is_ticketlock_implemented() {
    mame_spinlock::TicketLock lock;
    uint64_t initial_next = lock.next;
    lock.lock();
    lock.unlock();
    // If implemented, 'next' should have been incremented
    return lock.next > initial_next;
}

// =============================================================================
// Step 5: TicketLock tests
// =============================================================================

UTEST(ticketlock, initial_state) {
    mame_spinlock::TicketLock lock;

    // Initial state: next = 0, owner = 0
    ASSERT_TRUE(lock.next == 0);
    ASSERT_TRUE(lock.owner == 0);
}

UTEST(ticketlock, lock_unlock_sequence) {
    if (!is_ticketlock_implemented()) {
        UTEST_SKIP("TicketLock not implemented yet");
    }

    mame_spinlock::TicketLock lock;

    // After lock(): next should be incremented
    lock.lock();
    ASSERT_TRUE(lock.next == 1);
    ASSERT_TRUE(lock.owner == 0);

    // After unlock(): owner should be incremented
    lock.unlock();
    ASSERT_TRUE(lock.next == 1);
    ASSERT_TRUE(lock.owner == 1);

    // Second lock/unlock cycle
    lock.lock();
    ASSERT_TRUE(lock.next == 2);
    lock.unlock();
    ASSERT_TRUE(lock.owner == 2);
}

UTEST(ticketlock, concurrent_counter) {
    if (!is_ticketlock_implemented()) {
        UTEST_SKIP("TicketLock not implemented yet");
    }

    // Multiple threads increment a shared counter protected by ticket lock
    // Final value should equal N_THREADS * INCREMENTS_PER_THREAD
    constexpr int N_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;

    mame_spinlock::TicketLock lock;
    uint64_t counter = 0;

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

UTEST(ticketlock, fairness_fifo) {
    if (!is_ticketlock_implemented()) {
        UTEST_SKIP("TicketLock not implemented yet");
    }

    // Test FIFO ordering: threads should acquire lock in order they requested
    constexpr int N_THREADS = 4;

    mame_spinlock::TicketLock lock;
    std::atomic<int> ready_count{0};
    std::atomic<bool> start{false};
    std::vector<int> acquisition_order;
    mame_spinlock::SpinLockTTAS order_lock;  // Protect acquisition_order vector

    // Note: This test relies on SpinLockTTAS being implemented too
    // If not, we just test basic functionality

    lock.lock();  // Main thread holds lock initially

    auto worker = [&](int thread_id) {
        ready_count.fetch_add(1);
        while (!start.load()) {
            // Spin wait for start signal
        }

        lock.lock();

        // Record when this thread acquired the lock
        // Use a simple busy-wait mechanism for the order_lock
        while (true) {
            if (order_lock.try_lock()) {
                acquisition_order.push_back(thread_id);
                order_lock.unlock();
                break;
            }
        }

        lock.unlock();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    // Wait for all threads to be ready
    while (ready_count.load() < N_THREADS) {
        // Spin
    }

    // Signal threads to start and release main lock
    start.store(true);
    lock.unlock();

    for (auto& t : threads) {
        t.join();
    }

    // All threads should have acquired the lock exactly once
    ASSERT_TRUE(acquisition_order.size() == static_cast<size_t>(N_THREADS));

    // Note: Due to timing, we can't guarantee strict FIFO order in this test
    // But we can verify all threads got the lock
    std::vector<bool> seen(N_THREADS, false);
    for (int id : acquisition_order) {
        ASSERT_TRUE(id >= 0 && id < N_THREADS);
        seen[id] = true;
    }
    for (int i = 0; i < N_THREADS; ++i) {
        ASSERT_TRUE(seen[i]);
    }
}

UTEST_MAIN();
