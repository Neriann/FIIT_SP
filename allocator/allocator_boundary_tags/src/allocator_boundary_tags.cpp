#include "../include/allocator_boundary_tags.h"


///
/// ALLOCATOR IMPLEMENTATION
///

///
/// PUBLIC METHODS OF ALLOCATOR
///

allocator_boundary_tags::~allocator_boundary_tags() {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);
    if (header) {
        std::size_t full_pool_size = header->_size + allocator_metadata_size;
        auto *parent = header->_parent;

        // Constructed with placement-new
        header->~memory_header();

        parent->deallocate(header, full_pool_size);
        _trusted_memory = nullptr;
    }
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept {
    auto *header = reinterpret_cast<memory_header *>(other._trusted_memory);
    _trusted_memory = nullptr;
    if (header) {
        std::lock_guard lock(header->_mutex);

        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    // move and swap
    allocator_boundary_tags tmp = std::move(other);
    std::swap(_trusted_memory, tmp._trusted_memory);

    return *this;
}


/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
    std::size_t space_size,
    std::pmr::memory_resource *parent_allocator,
    allocator_with_fit_mode::fit_mode allocate_fit_mode) {
    std::size_t full_pool_size = space_size + allocator_metadata_size;

    if (!parent_allocator) {
        parent_allocator = std::pmr::get_default_resource();
    }

    void *raw_header = parent_allocator->allocate(full_pool_size);

    auto *allocator_header = new(raw_header) memory_header();

    _trusted_memory = raw_header;

    allocator_header->_fit_mode = allocate_fit_mode;
    allocator_header->_parent = parent_allocator;
    allocator_header->_size = space_size;
    allocator_header->_first_block = nullptr;
}

void* allocator_boundary_tags::do_allocate_sm(std::size_t size) {
    auto* header = reinterpret_cast<memory_header*>(_trusted_memory);
    std::lock_guard lock(header->_mutex);

    const std::size_t total = size + occupied_block_metadata_size;
    const auto candidate = find_block_to_allocate(total);
    if (!candidate.has_value()) {
        throw std::bad_alloc();
    }

    auto* arena_begin = get_arena_of_allocator(_trusted_memory);
    auto* prev = candidate->prev;
    auto* insert_pos = prev
        ? block_end(get_arena_from_header(prev), prev->size)
        : arena_begin;

    std::size_t actual_size = size;
    if (candidate->free_size - total < occupied_block_metadata_size) {
        actual_size = candidate->free_size - occupied_block_metadata_size;
    }

    auto* new_block = new(insert_pos) occupied_block{};
    new_block->size = actual_size;
    new_block->prev = prev;
    new_block->parent = _trusted_memory;

    if (!prev) {
        new_block->next = header->_first_block;
        if (header->_first_block) {
            header->_first_block->prev = new_block;
        }
        header->_first_block = new_block;
    } else {
        new_block->next = prev->next;
        if (prev->next) {
            prev->next->prev = new_block;
        }
        prev->next = new_block;
    }

    return get_arena_from_header(new_block);
}

void allocator_boundary_tags::do_deallocate_sm(void *at) {
    if (!at) return;

    auto* header = reinterpret_cast<memory_header*>(_trusted_memory);
    std::lock_guard lock(header->_mutex);

    auto* block = get_header_from_arena(at);

    // ===== unlink =====
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        header->_first_block = block->next;
    }

    if (block->next) {
        block->next->prev = block->prev;
    }

    // (опционально) можно занулить
    block->prev = block->next = nullptr;
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode) {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    if (header) {
        std::lock_guard lock(header->_mutex);
        header->_fit_mode = mode;
    }
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const {
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept {
    return {_trusted_memory};
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept {
    return {nullptr};
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const {
    auto* header = reinterpret_cast<memory_header*>(_trusted_memory);
    if (!header) {
        return {};
    }

    std::lock_guard lock(header->_mutex);

    std::vector<allocator_test_utils::block_info> data;

    const auto* arena_begin = get_arena_of_allocator(_trusted_memory);
    const auto* arena_end = arena_begin + header->_size;

    const auto append_segment = [&](std::size_t block_size, bool occupied) {
        if (block_size > 0) {
            data.push_back({.block_size = block_size, .is_block_occupied = occupied});
        }
    };

    auto* cursor = arena_begin;
    for (auto* block = header->_first_block; block != nullptr; block = block->next) {
        const auto* block_begin = reinterpret_cast<std::byte*>(block);

        append_segment(static_cast<std::size_t>(block_begin - cursor), false);
        append_segment(occupied_block_metadata_size + block->size, true);

        cursor = block_end(get_arena_from_header(block), block->size);
    }

    append_segment(static_cast<std::size_t>(arena_end - cursor), false);

    return data;
}


bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept {
    return dynamic_cast<const allocator_boundary_tags *>(&other) != nullptr;
}


///
/// BOUNDARY ITERATOR IMPLEMENTATION
/// (Think that iterator works under the lock mutex of allocator and invalidation
/// of iterator is not possible)
///


///
/// PUBLIC METHODS OF BOUNDARY ITERATOR
///

bool allocator_boundary_tags::boundary_iterator::operator==(
    const allocator_boundary_tags::boundary_iterator &other) const noexcept {
    return _occupied_ptr == other._occupied_ptr &&
               _occupied == other._occupied;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
    const allocator_boundary_tags::boundary_iterator &other) const noexcept {
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept {
    if (!_occupied_ptr) return *this;

    _occupied_ptr = _occupied_ptr->next;

    if (!_occupied_ptr) _occupied = false; // stop, when we got to the end

    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept {
    if (!_occupied_ptr && !_occupied) {

        auto* header = reinterpret_cast<memory_header*>(_trusted_memory);
        auto* curr = header->_first_block;

        if (!curr) return *this;

        while (curr->next) {
            curr = curr->next;
        }
        _occupied_ptr = curr;
        _occupied = true;
        return *this;
    }

    if (!_occupied_ptr) return *this;

    _occupied_ptr = _occupied_ptr->prev;

    if (!_occupied_ptr) {
        _occupied = false; // stop, when we got to the beginning
    }

    return *this;

}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n) {
    boundary_iterator tmp = *this;
    ++(*this);
    return tmp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n) {
    boundary_iterator tmp = *this;
    --(*this);
    return tmp;
}

std::size_t allocator_boundary_tags::boundary_iterator::size() const noexcept {
    if (!_occupied_ptr) return 0;

    return occupied_block_metadata_size + _occupied_ptr->size;
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept {
    return _occupied_ptr != nullptr;
}

void *allocator_boundary_tags::boundary_iterator::operator*() const noexcept {
    if (!_occupied_ptr) return nullptr;
    return get_arena_from_header(_occupied_ptr);}


allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted) {
    if (!trusted) return;

    auto *header = reinterpret_cast<memory_header *>(trusted);

    _trusted_memory = trusted;
    _occupied_ptr = header->_first_block;

    _occupied = (_occupied_ptr != nullptr);
}

allocator_boundary_tags::occupied_block *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept {
    return _occupied_ptr;
}


///
/// PRIVATE METHODS OF ALLOCATOR
///

auto allocator_boundary_tags::find_block_to_allocate(std::size_t size) const noexcept
    -> std::optional<allocation_candidate>
{
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    if (!header) {
        return std::nullopt;
    }

    auto* arena_begin = get_arena_of_allocator(_trusted_memory);
    auto* arena_end = arena_begin + header->_size;

    std::optional<allocation_candidate> best = std::nullopt;

    auto consider = [&](occupied_block* prev, std::size_t free_size) -> bool {
        if (free_size < size) {
            return false;
        }

        if (header->_fit_mode == fit_mode::first_fit) {
            best = allocation_candidate{.prev = prev, .free_size = free_size};
            return true;
        }

        if (!best.has_value()) {
            best = allocation_candidate{.prev = prev, .free_size = free_size};
            return false;
        }

        if (header->_fit_mode == fit_mode::the_best_fit && free_size < best->free_size) {
            best = allocation_candidate{.prev = prev, .free_size = free_size};
        }

        if (header->_fit_mode == fit_mode::the_worst_fit && free_size > best->free_size) {
            best = allocation_candidate{.prev = prev, .free_size = free_size};
        }

        return false;
    };

    occupied_block* prev = nullptr;
    auto* cursor = arena_begin;

    for (auto* curr = header->_first_block; curr != nullptr; curr = curr->next) {
        auto* curr_begin = reinterpret_cast<std::byte*>(curr);
        if (cursor < curr_begin && consider(prev, static_cast<std::size_t>(curr_begin - cursor))) {
            return best;
        }

        cursor = block_end(get_arena_from_header(curr), curr->size);
        prev = curr;
    }

    if (cursor < arena_end) {
        consider(prev, static_cast<std::size_t>(arena_end - cursor));
    }

    return best;
}

std::byte *allocator_boundary_tags::get_arena_of_allocator(void *header) {
    return reinterpret_cast<std::byte *>(header) + allocator_metadata_size;
}

allocator_boundary_tags::occupied_block *allocator_boundary_tags::get_header_from_arena(void *arena) {
    return reinterpret_cast<occupied_block *>(reinterpret_cast<std::byte *>(arena) - occupied_block_metadata_size);
}

std::byte *allocator_boundary_tags::get_arena_from_header(void *header) {
    return reinterpret_cast<std::byte *>(header) + occupied_block_metadata_size;
}

std::byte *allocator_boundary_tags::block_end(void *arena, std::size_t size) {
    return reinterpret_cast<std::byte *>(arena) + size;
}
