#pragma once

#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <esp_heap_caps.h>
#include <sdkconfig.h>

// Non-DMA retained audio belongs in PSRAM. With the normal malloc policy,
// 512-sample wake-history blocks each consume 1024 bytes of internal SRAM.
// Keep the default allocator for driver/DMA buffers and task stacks.
template <typename T>
class AudioStorageAllocator {
public:
    using value_type = T;
    using is_always_equal = std::true_type;

    AudioStorageAllocator() noexcept = default;
    template <typename U>
    AudioStorageAllocator(const AudioStorageAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(size_t count) {
        if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
            AllocationFailed();
        }
#if CONFIG_SPIRAM
        constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        constexpr uint32_t caps = MALLOC_CAP_8BIT;
#endif
        auto* memory = heap_caps_malloc(count * sizeof(T), caps);
        if (memory == nullptr) {
            // Do not silently spill retained history back into scarce SRAM.
            // The registered allocation-failure hook records the request first.
            AllocationFailed();
        }
        return static_cast<T*>(memory);
    }

    void deallocate(T* pointer, size_t) noexcept {
        heap_caps_free(pointer);
    }

private:
    [[noreturn]] static void AllocationFailed() {
#if defined(__cpp_exceptions)
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
};

template <typename T, typename U>
bool operator==(const AudioStorageAllocator<T>&, const AudioStorageAllocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
bool operator!=(const AudioStorageAllocator<T>&, const AudioStorageAllocator<U>&) noexcept {
    return false;
}
