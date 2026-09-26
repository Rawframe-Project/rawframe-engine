#include "allocations.h"

#include <atomic>
#include <cstddef>

#if RAWFRAME_QUIC_HEAP_COUNTED
#include <malloc.h>
#endif

namespace rawframe::network_quic {

#if RAWFRAME_QUIC_HEAP_COUNTED

namespace {

// MsQuic allocates from every thread it runs.
std::atomic<std::uint64_t> held{0};

} // namespace

} // namespace rawframe::network_quic

// The linker sends MsQuic's calls here and these on to MsQuic's own
// functions (`--wrap`, in this module's CMakeLists.txt). A block is counted
// at the size the allocator gave it, so a free takes off what was added.
extern "C" {

void* __real_CxPlatAlloc(std::size_t bytes, std::uint32_t tag);
void* __real_CxPlatAllocUninitialized(std::size_t bytes, std::uint32_t tag);
void __real_CxPlatFree(void* memory, std::uint32_t tag);

void* __wrap_CxPlatAlloc(std::size_t bytes, std::uint32_t tag) {
    void* const kMemory = __real_CxPlatAlloc(bytes, tag);
    if (kMemory != nullptr) {
        rawframe::network_quic::held.fetch_add(::malloc_usable_size(kMemory), std::memory_order_relaxed);
    }
    return kMemory;
}

void* __wrap_CxPlatAllocUninitialized(std::size_t bytes, std::uint32_t tag) {
    void* const kMemory = __real_CxPlatAllocUninitialized(bytes, tag);
    if (kMemory != nullptr) {
        rawframe::network_quic::held.fetch_add(::malloc_usable_size(kMemory), std::memory_order_relaxed);
    }
    return kMemory;
}

void __wrap_CxPlatFree(void* memory, std::uint32_t tag) {
    if (memory != nullptr) {
        rawframe::network_quic::held.fetch_sub(::malloc_usable_size(memory), std::memory_order_relaxed);
    }
    __real_CxPlatFree(memory, tag);
}

} // extern "C"

namespace rawframe::network_quic {

std::optional<std::uint64_t> quicHeapBytes() noexcept {
    return held.load(std::memory_order_relaxed);
}

#else

std::optional<std::uint64_t> quicHeapBytes() noexcept {
    return std::nullopt;
}

#endif

} // namespace rawframe::network_quic
