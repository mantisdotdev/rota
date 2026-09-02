#include "allocation_counter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<uint64_t> count_(0);

void* counted(std::size_t size) {
  count_.fetch_add(1, std::memory_order_relaxed);
  void* memory = std::malloc(size == 0 ? 1 : size);
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

// C++17 routes an over-aligned new to the aligned overloads first, so they count too.
void* counted_aligned(std::size_t size, std::align_val_t alignment) {
  count_.fetch_add(1, std::memory_order_relaxed);
  std::size_t align = static_cast<std::size_t>(alignment);
  if (align < sizeof(void*)) align = sizeof(void*);
  void* memory = nullptr;
  if (posix_memalign(&memory, align, size == 0 ? 1 : size) != 0) throw std::bad_alloc();
  return memory;
}

}  // namespace

namespace allocation_counter {

uint64_t count() { return count_.load(std::memory_order_relaxed); }

}  // namespace allocation_counter

void* operator new(std::size_t size) { return counted(size); }
void* operator new[](std::size_t size) { return counted(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return counted_aligned(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return counted_aligned(size, alignment); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
