#include <not_implemented.h>
#include "../include/allocator_global_heap.h"

allocator_global_heap::allocator_global_heap() = default;

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    return ::operator new(size * sizeof(char));
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    ::operator delete(at);
}

allocator_global_heap::~allocator_global_heap() = default;

allocator_global_heap::allocator_global_heap(const allocator_global_heap &other) = default;

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other)
= default;

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return dynamic_cast<const allocator_global_heap*>(&other) != nullptr;
}

allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept
= default;

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept = default;