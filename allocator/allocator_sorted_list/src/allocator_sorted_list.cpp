#include "../include/allocator_sorted_list.h"

#include <iostream>

// Debug logging control: compile-time based on NDEBUG
// In Debug mode: logs enabled by default
// In Release mode: logs disabled by default
#ifdef NDEBUG
#define DEBUG_LOG_ENABLED 0
#else
#define DEBUG_LOG_ENABLED 1
#endif


struct allocator_sorted_list::memory_header {
    std::pmr::memory_resource *_parent;
    void *_first_block; // first free block
    std::size_t _size;
    fit_mode _fit_mode;
    std::mutex _mutex;
};

struct allocator_sorted_list::free_block {
    std::size_t size{};
    free_block *next{};
};

struct allocator_sorted_list::used_block {
    std::size_t size{};
    void *parent{};
};

std::size_t allocator_sorted_list::allocator_metadata_size() noexcept {
    return sizeof(memory_header);
}

std::size_t allocator_sorted_list::block_metadata_size() noexcept {
    static_assert(sizeof(free_block) == sizeof(used_block),
                  "free_block and used_block headers must have equal size");
    return sizeof(free_block);
}

std::size_t allocator_sorted_list::extra_memory_of_block() noexcept {
    return 8;
}

allocator_sorted_list::~allocator_sorted_list() {
    if (_mem_header) {
        std::size_t total_size = _mem_header->_size + allocator_metadata_size();
        auto *parent = _mem_header->_parent;

        // Constructed via placement-new
        _mem_header->~memory_header();

        if (parent) {
            parent->deallocate(_mem_header, total_size);
        } else {
            ::operator delete(_mem_header);
        }
    }
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept {
    _mem_header = other._mem_header;
    _trusted_memory = other._trusted_memory;

    other._mem_header = nullptr;
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    // move and swap
    allocator_sorted_list tmp = std::move(other);
    swap(tmp);
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
    std::size_t space_size,
    std::pmr::memory_resource *parent_allocator,
    allocator_with_fit_mode::fit_mode allocate_fit_mode) {
    std::size_t full_pool_size = space_size + block_metadata_size();
    std::size_t total_size = full_pool_size + allocator_metadata_size();

    void *raw_header = parent_allocator
                           ? parent_allocator->allocate(total_size)
                           : ::operator new(total_size);

    auto *allocator_header = new(raw_header) memory_header();

    _mem_header = allocator_header;
    _mem_header->_parent = parent_allocator;
    _mem_header->_size = full_pool_size;
    _mem_header->_fit_mode = allocate_fit_mode;

    void *allocator_arena = get_arena_from_header(allocator_header, allocator_metadata_size());
    auto *first_block_header = to_pointer<free_block>(allocator_arena);

    // Constructor argument is user arena size; first free block stores all of it.
    first_block_header->size = space_size;
    first_block_header->next = nullptr;

    _mem_header->_first_block = get_arena_from_header(first_block_header);
    _trusted_memory = allocator_arena;
}

auto allocator_sorted_list::find_first_block_to_allocate(
    std::size_t size) const noexcept -> std::optional<candidate> {
    std::size_t real_size = size + block_metadata_size();

    auto prev_data = free_end();
    for (auto data = free_begin(); data != free_end(); ++data) {
        std::size_t cur_block_size = data.size();

        if (cur_block_size >= real_size) {
            return std::make_optional<candidate>(data, prev_data);
        }
        prev_data = data;
    }
    return std::nullopt;
}

template<typename Compare>
auto allocator_sorted_list::find_block_to_allocate_impl(std::size_t size,
                                                        Compare cmp) const noexcept -> std::optional<candidate> {
    std::size_t real_size = size + block_metadata_size();

    std::size_t best_metric = 0;

    candidate cand{};

    auto prev = free_end();
    for (auto it = free_begin(); it != free_end(); ++it) {
        std::size_t cur_block_size = it.size();

        if (cur_block_size < real_size) continue;

        std::size_t diff = cur_block_size - real_size;
        if (!cand.is_valid() || cmp(diff, best_metric)) {
            cand = {it, prev};
            best_metric = diff;
        }
        prev = it;
    }
    if (!cand.is_valid()) {
        return std::nullopt;
    }
    return cand;
}

auto allocator_sorted_list::find_block_to_allocate(std::size_t size) const noexcept -> std::optional<candidate> {
    switch (_mem_header->_fit_mode) {
        case fit_mode::first_fit:
            return find_first_block_to_allocate(size);

        // use pattern strategy
        case fit_mode::the_best_fit:
            return find_block_to_allocate_impl(size, [](std::size_t a, std::size_t b) { return a < b; });
        case fit_mode::the_worst_fit:
            return find_block_to_allocate_impl(size, [](std::size_t a, std::size_t b) { return a > b; });
    }
    return std::nullopt;
}


[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    std::size_t size) {
    std::lock_guard lock(_mem_header->_mutex);

    void *occupied = nullptr;

    auto cand = find_block_to_allocate(size);
    if (!cand.has_value()) {
        throw std::bad_alloc();
    }

    auto [block, prev_block] = cand.value();

    std::size_t real_size = size + block_metadata_size();

    std::size_t cur_block_size = block.size();

    // quantity used bytes without meta
    std::size_t difference = cur_block_size - real_size;

    auto *header_of_free_part = get_header_from_arena<free_block>(*block);

    if (difference >= extra_memory_of_block()) {
        header_of_free_part->size = difference;

        // it's used block header
        void *free_block_end = block_end(*block, difference);

        auto *header_of_used_part = to_pointer<used_block>(free_block_end);

        header_of_used_part->size = size;
        header_of_used_part->parent = _trusted_memory;

        occupied =
                get_arena_from_header(header_of_used_part);
    } else {
        // change previous pointer to free block
        if (prev_block == free_end()) {
            // if the first block wanted to use
            _mem_header->_first_block = header_of_free_part->next;
        } else {
            auto *prev_header_of_free_part = get_header_from_arena<free_block>(*prev_block);

            prev_header_of_free_part->next = header_of_free_part->next;
        }
        auto *header_of_used_part = get_header_from_arena<used_block>(*block);

        // No split: convert the whole free block into used block.
        // Keep full arena size so deallocation can restore all bytes.
        header_of_used_part->size = cur_block_size;
        header_of_used_part->parent = _trusted_memory;

        occupied = *block;
    }

#if DEBUG_LOG_ENABLED
    std::cout << "allocate " << print_blocks() << std::endl;
#endif

    // returned occupied block
    return occupied;
}


bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept {
    // checking the fit_mode is optional, because if this object has allocated memory, then its successor can delete this memory regardless of the fit_mode
    return dynamic_cast<const allocator_sorted_list *>(&other) != nullptr;
}

allocator_sorted_list::candidate allocator_sorted_list::find_block_to_deallocate(void *at) const noexcept {
    // Find first free block on the right of `at` and remember free block on the left.
    auto prev = free_end();
    for (auto cur = free_begin(); cur != free_end(); ++cur) {
        if (to_pointer<std::byte>(*cur) > to_pointer<std::byte>(at)) {
            return {cur, prev};
        }
        prev = cur;
    }

    return {free_end(), prev};
}


void allocator_sorted_list::do_deallocate_sm(
    void *at) {
    if (!at) return;

    std::lock_guard lock(_mem_header->_mutex);

    auto [curr_free_block, prev_free_block] = find_block_to_deallocate(at);

    auto *at_byte = to_pointer<std::byte>(at);
    auto *pool_begin = to_pointer<std::byte>(_trusted_memory);
    auto *pool_end = to_pointer<std::byte>(block_end(_trusted_memory, _mem_header->_size));

    if (at_byte < pool_begin || at_byte >= pool_end) {
        throw find_error("The required block was not found");
    }

    auto *used_header = get_header_from_arena<used_block>(at);
    if (used_header->parent != _trusted_memory) {
        throw find_error("The required block was incorrectly allocated or heap fault");
    }

    auto used_size = used_header->size;
    auto *used_block_end = to_pointer<std::byte>(block_end(at, used_size));

    free_block *prev_header = nullptr;
    free_block *curr_header = nullptr;
    if (prev_free_block != free_end()) {
        prev_header = get_header_from_arena<free_block>(*prev_free_block);
    }
    if (curr_free_block != free_end()) {
        curr_header = get_header_from_arena<free_block>(*curr_free_block);
    }

    bool merge_left = prev_header &&
                      to_pointer<std::byte>(block_end(*prev_free_block, prev_header->size)) == to_pointer<std::byte>(
                          used_header);
    bool merge_right = curr_header &&
                       used_block_end == to_pointer<std::byte>(curr_header);

    if (merge_left && merge_right) {
        auto *prev_free_arena = *prev_free_block;
        auto *curr_free_arena = *curr_free_block;
        auto *new_end = to_pointer<std::byte>(block_end(curr_free_arena, curr_header->size));
        prev_header->size = static_cast<std::size_t>(new_end - to_pointer<std::byte>(prev_free_arena));
        prev_header->next = curr_header->next;
    } else if (merge_left) {
        auto *prev_free_arena = *prev_free_block;
        prev_header->size = static_cast<std::size_t>(used_block_end - to_pointer<std::byte>(prev_free_arena));
    } else if (merge_right) {
        auto *new_free = to_pointer<free_block>(used_header);
        auto *new_arena = get_arena_from_header(new_free);
        auto *curr_free_arena = *curr_free_block;
        auto *new_end = to_pointer<std::byte>(block_end(curr_free_arena, curr_header->size));

        new_free->size = static_cast<std::size_t>(new_end - to_pointer<std::byte>(new_arena));
        new_free->next = curr_header->next;

        if (prev_header) {
            prev_header->next = to_pointer<free_block>(new_arena);
        } else {
            _mem_header->_first_block = new_arena;
        }
    } else {
        auto *new_free = to_pointer<free_block>(used_header);
        auto *new_arena = get_arena_from_header(new_free);

        new_free->size = used_size;
        new_free->next = curr_free_block != free_end() ? to_pointer<free_block>(*curr_free_block) : nullptr;

        if (prev_header) {
            prev_header->next = to_pointer<free_block>(new_arena);
        } else {
            _mem_header->_first_block = new_arena;
        }
    }

#if DEBUG_LOG_ENABLED
    std::cout << "deallocate " << print_blocks() << std::endl;
#endif
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode) {
    std::lock_guard lock(_mem_header->_mutex);
    _mem_header->_fit_mode = mode;
}

void allocator_sorted_list::swap(allocator_sorted_list &other) noexcept {
    std::swap(_trusted_memory, other._trusted_memory);
    std::swap(_mem_header, other._mem_header);
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept {
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const {
    std::vector<allocator_test_utils::block_info> blocks_info;

    for (auto it = sorted_iterator(_trusted_memory, _mem_header->_size, _mem_header->_first_block); it != end(); ++it) {
        blocks_info.emplace_back(it.size(), it.occupied());
    }
    return blocks_info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept {
    return {_mem_header->_first_block};
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept {
    return {}; // can {};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept {
    return {_trusted_memory, _mem_header->_size, _mem_header->_first_block};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept {
    return {}; // dummy block is free
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
    const allocator_sorted_list::sorted_free_iterator &other) const noexcept {
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
    const allocator_sorted_list::sorted_free_iterator &other) const noexcept {
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept {
    if (_free_ptr) {
        auto *header = get_header_from_arena<free_block>(_free_ptr);
        _free_ptr = header->next;
    }

    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n) {
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}

std::size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept {
    if (_free_ptr) {
        auto *header = get_header_from_arena<free_block>(_free_ptr);
        return header->size;
    }
    return {};
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept {
    return _free_ptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() = default;

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted) : _free_ptr(trusted) {
}

bool allocator_sorted_list::sorted_iterator::operator==(
    const allocator_sorted_list::sorted_iterator &other) const noexcept {
    bool this_is_end = _trusted_memory &&
                       _current_block == block_end(_trusted_memory, _size);
    bool other_is_end = other._trusted_memory &&
                        other._current_block == block_end(other._trusted_memory, other._size);

    if (this_is_end && other_is_end) {
        return true;
    }

    if (this_is_end && !other._trusted_memory && !other._current_block) {
        return true;
    }

    if (other_is_end && !_trusted_memory && !_current_block) {
        return true;
    }

    if (!_trusted_memory && !other._trusted_memory) {
        return true;
    }

    return _current_block == other._current_block &&
           _next_free == other._next_free &&
           _is_free == other._is_free &&
           _trusted_memory == other._trusted_memory;
}

bool allocator_sorted_list::sorted_iterator::operator!=(
    const allocator_sorted_list::sorted_iterator &other) const noexcept {
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept {
    if (!_trusted_memory) {
        return *this;
    }

    auto *end_ptr = block_end(_trusted_memory, _size);
    auto *end_byte = to_pointer<std::byte>(end_ptr);
    if (_current_block == end_ptr) {
        return *this;
    }

    if (_is_free) {
        auto *free_header = get_header_from_arena<free_block>(_current_block);
        auto *next_header = block_end(_current_block, free_header->size);
        auto *next_header_byte = to_pointer<std::byte>(next_header);

        _next_free = free_header->next;

        if (next_header_byte >= end_byte) {
            _current_block = end_ptr;
            _is_free = false;
            return *this;
        }

        _current_block = get_arena_from_header(next_header);
        _is_free = false;
        return *this;
    }

    if (_next_free) {
        _current_block = _next_free;
        _is_free = true;
    } else {
        _current_block = end_ptr;
        _is_free = false;
    }

    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n) {
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}

std::size_t allocator_sorted_list::sorted_iterator::size() const noexcept {
    if (!_trusted_memory || !_current_block) {
        return 0;
    }

    if (_is_free) {
        auto *free_header = get_header_from_arena<free_block>(_current_block);
        return free_header->size;
    }

    auto *end_ptr = to_pointer<std::byte>(block_end(_trusted_memory, _size));
    auto *current = to_pointer<std::byte>(_current_block);
    if (current >= end_ptr) {
        return 0;
    }

    if (_next_free) {
        auto *next_header = to_pointer<std::byte>(get_header_from_arena<void>(_next_free));
        return static_cast<std::size_t>(next_header - current);
    }

    return static_cast<std::size_t>(end_ptr - current);
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept {
    return _current_block;
}

allocator_sorted_list::sorted_iterator::sorted_iterator() = default;

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted, std::size_t size, void *first_free)
    : _trusted_memory(trusted), _size(size) {
    if (!_trusted_memory) {
        return;
    }

    auto *first_arena = get_arena_from_header(_trusted_memory);
    _next_free = first_free;
    _is_free = (_next_free == first_arena);
    _current_block = _is_free ? _next_free : first_arena;
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept {
    return !_is_free;
}

allocator_sorted_list::find_error::find_error(const char *msg) : _msg(msg) {
}

const char *allocator_sorted_list::find_error::what() const noexcept {
    return _msg;
}

bool allocator_sorted_list::candidate::is_valid() const noexcept {
    return block != sorted_free_iterator();
}

template<typename T>
// requires (std::same_as<T, allocator_sorted_list::used_block> || std::same_as<T, allocator_sorted_list::free_block>
// || std::same_as<T, void>)
T *allocator_sorted_list::to_pointer(auto *header) {
    return reinterpret_cast<T *>(header);
}

template<typename T>
// requires (std::same_as<T, allocator_sorted_list::used_block> || std::same_as<T, allocator_sorted_list::free_block>
// || std::same_as<T, void>)
T *allocator_sorted_list::get_header_from_arena(void *arena, std::size_t meta) {
    return to_pointer<T>(reinterpret_cast<std::byte *>(arena) - meta);
}

void *allocator_sorted_list::get_arena_from_header(auto *header, std::size_t meta) {
    return reinterpret_cast<std::byte *>(header) + meta;
}

void *allocator_sorted_list::block_end(void *arena, std::size_t size) {
    return reinterpret_cast<std::byte *>(arena) + size;
}
