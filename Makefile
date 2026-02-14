# mameSpinLock Makefile
# Simple build for x86-64 only

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread -I./src -I./tests
LDFLAGS  := -pthread

# Test sources
TEST_SRCS := tests/test_atomic.cpp tests/test_spinlock.cpp tests/test_ticket.cpp tests/test_mcs.cpp
TEST_BINS := $(TEST_SRCS:.cpp=)

.PHONY: all test clean

all: $(TEST_BINS)

tests/%: tests/%.cpp src/mameSpinLock.h tests/utest.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "Running $$t..."; \
		./$$t || exit 1; \
	done
	@echo "All tests passed!"

clean:
	rm -f $(TEST_BINS) tests/test_spinlock tests/test_ticket
