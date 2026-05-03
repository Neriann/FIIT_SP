#include <not_implemented.h>

#include "../include/allocator_red_black_tree.h"

#ifdef NDEBUG
#define DEBUG_LOG_ENABLED 0
#else
#define DEBUG_LOG_ENABLED 1
#include <iostream>
#endif


allocator_red_black_tree::~allocator_red_black_tree() {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);
    if (header) {
        std::size_t full_pool_size = header->_size + allocator_metadata_size + free_block_metadata_size +
                                     occupied_block_metadata_size; // + space for end block
        auto *parent = header->_parent;

        delete header->_root;

        // Constructed with placement-new
        header->~memory_header();

        parent->deallocate(header, full_pool_size);
        _trusted_memory = nullptr;
    }
}

allocator_red_black_tree::allocator_red_black_tree(
    allocator_red_black_tree &&other) noexcept {
    auto *header = reinterpret_cast<memory_header *>(other._trusted_memory);

    _trusted_memory = nullptr;

    if (!header) {
        return;
    }

    std::lock_guard lock(header->_mutex);

    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_red_black_tree &allocator_red_black_tree::operator=(
    allocator_red_black_tree &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    // move and swap
    allocator_red_black_tree tmp = std::move(other);
    std::swap(_trusted_memory, tmp._trusted_memory);
    return *this;
}

allocator_red_black_tree::allocator_red_black_tree(
    std::size_t space_size,
    std::pmr::memory_resource *parent_allocator,
    allocator_with_fit_mode::fit_mode allocate_fit_mode) {
    std::size_t total_size = space_size + allocator_metadata_size + free_block_metadata_size +
                             occupied_block_metadata_size; // + space for end block

    if (!parent_allocator) {
        parent_allocator = std::pmr::get_default_resource();
    }
    void *raw = parent_allocator->allocate(total_size);

    auto *header = new(raw) memory_header();

    void *arena_begin = get_arena_of_allocator(header);

    void *arena_end = block_end(get_arena_from_header_of_free(arena_begin), space_size);

    auto *first_block = new(arena_begin) block();

    auto *end = reinterpret_cast<block *>(arena_end);

    end->meta.is_end = true;
    end->meta.occupied = true;
    end->prev = first_block;
    end->next = nullptr;

    first_block->next = end;

    header->_fit = allocate_fit_mode;
    header->_parent = parent_allocator;
    header->_size = space_size;
    header->_root = new red_black_tree();

    header->_root->insert(first_block);

    _trusted_memory = header;
}


bool allocator_red_black_tree::do_is_equal(const std::pmr::memory_resource &other) const noexcept {
    return dynamic_cast<const allocator_red_black_tree *>(&other) != nullptr;
}

[[nodiscard]] void *allocator_red_black_tree::do_allocate_sm(
    std::size_t size) {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    std::lock_guard lock(header->_mutex);

    std::size_t total_size = size + occupied_block_metadata_size;

    auto candidate = find_block_to_allocate(total_size);
    if (!candidate.has_value()) {
        throw std::bad_alloc();
    }

    auto [target_block, arena_of_target_block_size] = candidate.value();

    // target_block->prev = target_block->next;

    header->_root->erase_block(target_block);

    // split
    if (arena_of_target_block_size - total_size > free_block_metadata_size) {
        auto *new_free_block = new(block_end(target_block,
                                             total_size)) block();

        new_free_block->next = target_block->next;
        if (target_block->next) {
            target_block->next->prev = new_free_block;
        }
        target_block->next = new_free_block;
        new_free_block->prev = target_block;

        header->_root->insert(new_free_block);
    }

    target_block->meta.occupied = true;
    target_block->occupied_node.parent = _trusted_memory;

#if DEBUG_LOG_ENABLED
    std::cout << "allocate " << print_blocks() << std::endl;
#endif



    return get_arena_from_header_of_occupied(target_block);
}


void allocator_red_black_tree::do_deallocate_sm(
    void *at) {
    if (!at) return;

    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    std::lock_guard lock(header->_mutex);

    auto *block = get_header_from_arena_of_occupied(at);

    if (block->occupied_node.parent != _trusted_memory) {
        throw std::logic_error("Pointer was not allocated by this allocator");
    }

    // merge
    if (block->next && !block->next->meta.occupied && !block->next->meta.is_end) {
        header->_root->erase_block(block->next);

        // unlink
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    if (block->prev && !block->prev->meta.occupied && !block->prev->meta.is_end) {
        auto *left = block->prev;
        header->_root->erase_block(left);

        left->next = block->next;
        if (block->next) {
            block->next->prev = left;
        }
        block = left;
    }

    block->meta.occupied = false;
    block->meta.color = block_color::RED;

    header->_root->insert(block);

#if DEBUG_LOG_ENABLED
    std::cout << "deallocate " << print_blocks() << std::endl;
#endif

}

void allocator_red_black_tree::set_fit_mode(allocator_with_fit_mode::fit_mode mode) {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    std::lock_guard lock(header->_mutex);

    header->_fit = mode;
}


std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info() const {
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info_inner() const {
    std::vector<allocator_test_utils::block_info> data;

    for (auto it = begin(); it != end(); ++it) {
        data.push_back({.block_size = it.size(), .is_block_occupied = it.occupied()});
    }
    return data;
}


allocator_red_black_tree::rb_iterator allocator_red_black_tree::begin() const noexcept {
    return {_trusted_memory};
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::end() const noexcept {
    return {nullptr};
}


bool allocator_red_black_tree::rb_iterator::operator==(
    const allocator_red_black_tree::rb_iterator &other) const noexcept {
    return _trusted == other._trusted && _block_ptr == other._block_ptr;
}

bool allocator_red_black_tree::rb_iterator::operator!=(
    const allocator_red_black_tree::rb_iterator &other) const noexcept {
    return !(*this == other);
}

allocator_red_black_tree::rb_iterator &allocator_red_black_tree::rb_iterator::operator++() & noexcept {
    if (_block_ptr) {
        _block_ptr = _block_ptr->next;
    }
    if (_block_ptr->meta.is_end) {
        _trusted = _block_ptr = nullptr;
    }
    return *this;
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::rb_iterator::operator++(int n) {
    rb_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_red_black_tree::rb_iterator::size() const noexcept {
    if (!_block_ptr || _block_ptr->meta.is_end) {
        return 0;
    }
    return reinterpret_cast<std::byte *>(_block_ptr->next) - reinterpret_cast<std::byte *>(_block_ptr);
}

void *allocator_red_black_tree::rb_iterator::operator*() const noexcept {
    if (!_block_ptr || _block_ptr->meta.is_end) {
        return nullptr;
    }
    return _block_ptr->meta.occupied
               ? get_arena_from_header_of_occupied(_block_ptr)
               : get_arena_from_header_of_free(_block_ptr);
}

block *allocator_red_black_tree::rb_iterator::get_ptr() const noexcept {
    return _block_ptr;
}

allocator_red_black_tree::rb_iterator::rb_iterator(void *trusted) {
    _trusted = trusted;

    if (trusted) {
        auto *header = reinterpret_cast<memory_header *>(trusted);
        _block_ptr = reinterpret_cast<block *>(get_arena_of_allocator(header));
    } else {
        _block_ptr = nullptr;
    }
}

bool allocator_red_black_tree::rb_iterator::occupied() const noexcept {
    return _block_ptr && !_block_ptr->meta.is_end && _block_ptr->meta.occupied;
}

std::optional<std::pair<block *, std::size_t> > allocator_red_black_tree::find_block_to_allocate(
    std::size_t size) const noexcept {
    auto *header = reinterpret_cast<memory_header *>(_trusted_memory);

    if (!header) return std::nullopt;

    auto go_there = [_fit = header->_fit](std::size_t curr_key, std::size_t key) -> int {
        // if (curr_key == key) return 0;

        if (curr_key < key) return 1;

        switch (_fit) {
            case fit_mode::first_fit:
                return 0; // return any suitable block
            case fit_mode::the_best_fit:
                return -1; // go left to find smaller block
            case fit_mode::the_worst_fit:
                return 1; // go right to find bigger block
        }
        return -1;
    };

    auto *block = header->_root->find(size, go_there);
    if (!block || block->meta.occupied || block->meta.is_end) {
        return std::nullopt;
    }

    std::size_t block_size = reinterpret_cast<std::byte *>(block->next) - reinterpret_cast<std::byte *>(block);

    // ensure the found block can actually fit the requested size (header-to-header distance)
    if (block_size < size) {
        return std::nullopt;
    }

    return std::make_pair(block, block_size);
}

std::byte *allocator_red_black_tree::get_arena_of_allocator(void *header) {
    return reinterpret_cast<std::byte *>(header) + allocator_metadata_size;
}

block *allocator_red_black_tree::get_header_from_arena_of_free(void *arena) {
    return reinterpret_cast<block *>(reinterpret_cast<std::byte *>(arena) - free_block_metadata_size);
}

block *allocator_red_black_tree::get_header_from_arena_of_occupied(void *arena) {
    return reinterpret_cast<block *>(reinterpret_cast<std::byte *>(arena) - occupied_block_metadata_size);
}

std::byte *allocator_red_black_tree::get_arena_from_header_of_free(void *header) {
    return reinterpret_cast<std::byte *>(header) + free_block_metadata_size;
}

std::byte *allocator_red_black_tree::get_arena_from_header_of_occupied(void *header) {
    return reinterpret_cast<std::byte *>(header) + occupied_block_metadata_size;
}

std::byte *allocator_red_black_tree::block_end(void *arena, std::size_t size) {
    return reinterpret_cast<std::byte *>(arena) + size;
}
