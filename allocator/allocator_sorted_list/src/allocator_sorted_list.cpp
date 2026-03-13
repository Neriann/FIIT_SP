#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"

allocator_sorted_list::~allocator_sorted_list()
{
    throw not_implemented("allocator_sorted_list::~allocator_sorted_list()", "your code should be here...");
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept
{
    throw not_implemented("allocator_sorted_list::allocator_sorted_list(allocator_sorted_list &&) noexcept", "your code should be here...");
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    throw not_implemented("allocator_sorted_list &allocator_sorted_list::operator=(allocator_sorted_list &&) noexcept", "your code should be here...");
}

allocator_sorted_list::allocator_sorted_list(
        std::size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    _trusted_memory = parent_allocator ? parent_allocator->allocate(space_size + allocator_metadata_size) : ::operator new(space_size + allocator_metadata_size);
    _first_block = static_cast<std::byte*>(_trusted_memory) + block_metadata_size;

    _fit_mode = allocate_fit_mode;
    _size = space_size;
    ;}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    std::size_t size)
{
    std::lock_guard lock(mtx);

    void* occupied = nullptr;

    std::size_t real_size = size + block_metadata_size;

    sorted_free_iterator prev_data = free_end(); // TODO maybe prev_data{}
    for (auto data = free_begin(); data != free_end(); prev_data = data, ++data) {
        std::size_t cur_block_size = data.size();
        if (cur_block_size < real_size) continue;

        // quantity used bytes without meta
        std::size_t difference = cur_block_size - real_size;

        auto* header_of_free_part =
            reinterpret_cast<free_block*>(
                reinterpret_cast<std::byte*>(*data) - block_metadata_size);
        // auto* free_header = static_cast<free_block*>(header_of_free_part);

        // TODO need to check
        if (difference >= extra_memory_of_block) {

            header_of_free_part->size = difference;

            auto* header_of_used_part =
                reinterpret_cast<used_block*>(
                        reinterpret_cast<std::byte*>(*data) + difference);

            // auto* used_header = static_cast<used_block*>(header_of_used_part);
            header_of_used_part->size = size;
            header_of_used_part->parent = this; // TODO maybe not this, but parent

            occupied =
                reinterpret_cast<std::byte*>(header_of_used_part) + block_metadata_size;
        } else {
            // change previous pointer to free block
            if (prev_data == free_end()) { // if the first block wanted to use
                _first_block = header_of_free_part->next;
            } else {
                auto* prev_header_of_free_part =
                    reinterpret_cast<free_block*>(
                        reinterpret_cast<std::byte*>(*prev_data) - block_metadata_size);

                prev_header_of_free_part->next = header_of_free_part->next;
            }
            occupied = *data;
        }
        break;
    }
    if (!occupied) {
        throw std::bad_alloc();
    }
    return occupied;
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    throw not_implemented("allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)", "your code should be here...");
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    throw not_implemented("allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)", "your code should be here...");
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    throw not_implemented("bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept", "your code should be here...");
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    throw not_implemented("void allocator_sorted_list::do_deallocate_sm(void *)", "your code should be here...");
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    _fit_mode = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> blocks_info;

    for (auto it = sorted_iterator(_first_block); it != end(); ++it) {
        blocks_info.emplace_back(it.size(), it.occupied());
    }
    // TODO maybe move
    return blocks_info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    throw not_implemented("allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    throw not_implemented("allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    throw not_implemented("allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    throw not_implemented("allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept", "your code should be here...");
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    _free_ptr = static_cast<unsigned char*>(_free_ptr) + size() + block_metadata_size;
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp;
    ++(*this);
    return tmp;
}

std::size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    return *static_cast<std::size_t*>(_free_ptr);
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() = default;

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted) {
    if (trusted) {
        _free_ptr = static_cast<unsigned char*>(trusted) - block_metadata_size;
    }
}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    throw not_implemented("bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator &) const noexcept", "your code should be here...");
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    throw not_implemented("bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &) const noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    throw not_implemented("allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    throw not_implemented("allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int)", "your code should be here...");
}

std::size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    throw not_implemented("std::size_t allocator_sorted_list::sorted_iterator::size() const noexcept", "your code should be here...");
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    throw not_implemented("void *allocator_sorted_list::sorted_iterator::operator*() const noexcept", "your code should be here...");
}

allocator_sorted_list::sorted_iterator::sorted_iterator()
{
    throw not_implemented("allocator_sorted_list::sorted_iterator::sorted_iterator() ", "your code should be here...");
}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted)
{
    throw not_implemented("allocator_sorted_list::sorted_iterator::sorted_iterator(void *)", "your code should be here...");
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    throw not_implemented("bool allocator_sorted_list::sorted_iterator::occupied() const noexcept", "your code should be here...");
}
