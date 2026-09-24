// Replaces global operator new and delete to count allocations, so a test can
// show that a call made none. Linked into a test executable only when that
// executable calls allocationCount(). ThreadSanitizer's runtime owns these
// operators, so under it nothing is replaced and nothing is counted.

#include "rawframe/test/test.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<std::size_t> allocations{0};
}

std::size_t rawframe::test::allocationCount() noexcept {
    return allocations.load();
}

#if RAWFRAME_TEST_COUNTS_ALLOCATIONS

void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    std::abort();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++allocations;
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

#endif
