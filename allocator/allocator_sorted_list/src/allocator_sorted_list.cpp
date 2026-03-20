#include "../include/allocator_sorted_list.h"

#include <iostream>

struct allocator_sorted_list::memory_header {
    std::atomic<std::size_t> _refcount;
    std::pmr::memory_resource* _parent;
    void* _first_block; // first free block
    std::size_t _size;
    fit_mode _fit_mode;
    std::mutex _mutex;
};


allocator_sorted_list::~allocator_sorted_list()
{

    if (_mem_header && --_mem_header->_refcount == 0) {
        if (_mem_header->_parent) {
            std::size_t total_size = _mem_header->_size + allocator_metadata_size;

            _mem_header->_parent->deallocate(_mem_header, total_size);
        } else {
            ::operator delete(_mem_header);
        }
    }
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept
{
    _mem_header = other._mem_header;
    _trusted_memory = other._trusted_memory;

    other._mem_header = nullptr;
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    // move and swap
    allocator_sorted_list tmp = other;
    swap(tmp);
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        std::size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    auto* allocator_header = to_pointer<memory_header>(
        parent_allocator ? parent_allocator->allocate(space_size + allocator_metadata_size)
        : ::operator new(space_size + allocator_metadata_size));

    _mem_header = allocator_header;
    _mem_header->_parent = parent_allocator;
    _mem_header->_size = space_size;
    _mem_header->_fit_mode = allocate_fit_mode;
    _mem_header->_refcount = 1;

    void* allocator_arena = get_arena_from_header(allocator_header, allocator_metadata_size);
    auto* first_block_header = to_pointer<free_block>(allocator_arena);

    first_block_header->size = space_size - block_metadata_size;
    first_block_header->next = nullptr;

    _mem_header->_first_block = get_arena_from_header(first_block_header);
    _trusted_memory = allocator_arena;
}

allocator_sorted_list::candidate allocator_sorted_list::find_first_block_to_allocate(std::size_t size) const {
    std::size_t real_size = size + block_metadata_size;

    auto prev_data = free_end();
    for (auto data = free_begin(); data != free_end(); ++data) {
        std::size_t cur_block_size = data.size();

        if (cur_block_size >= real_size) {
            return {data, prev_data};
        }
        prev_data = data;
    }
    throw find_error("The required block was not found");
}

template <typename Compare>
allocator_sorted_list::candidate allocator_sorted_list::find_block_to_allocate_impl(std::size_t size, Compare cmp) const {
    std::size_t real_size = size + block_metadata_size;

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
        throw find_error("The required block was not found");
    }
    return cand;
}

allocator_sorted_list::candidate allocator_sorted_list::find_block_to_allocate(std::size_t size) const {
    switch (_mem_header->_fit_mode) {
        case fit_mode::first_fit:
            return find_first_block_to_allocate(size);

        // use pattern strategy
        case fit_mode::the_best_fit:
            return find_block_to_allocate_impl(size, [](std::size_t a, std::size_t b) { return a < b; });
        case fit_mode::the_worst_fit:
            return find_block_to_allocate_impl(size, [](std::size_t a, std::size_t b) { return a > b; });
    }
    throw std::logic_error("Unknown fit mode");
}


[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    std::size_t size)
{
    std::lock_guard lock(_mem_header->_mutex);

    void* occupied = nullptr;

    try {
        auto [block, prev_block] = find_block_to_allocate(size);

        std::size_t real_size = size + block_metadata_size;

        std::size_t cur_block_size = block.size();

        // quantity used bytes without meta
        std::size_t difference = cur_block_size - real_size;

        auto* header_of_free_part = get_header_from_arena<free_block>(*block);

        if (difference >= extra_memory_of_block /* + block_metadata_size*/) {

            header_of_free_part->size = difference;

            // it's used block header
            void* free_block_end = block_end(*block, difference);

            auto* header_of_used_part = to_pointer<used_block>(free_block_end);

            header_of_used_part->size = size;
            header_of_used_part->parent = this; // TODO maybe not this, but parent

            occupied =
                    get_arena_from_header(header_of_used_part);
        } else {

            // change previous pointer to free block
            if (prev_block == free_end()) { // if the first block wanted to use
                _mem_header->_first_block = header_of_free_part->next;
            } else {
                auto* prev_header_of_free_part = get_header_from_arena<free_block>(*prev_block);

                prev_header_of_free_part->next = header_of_free_part->next;
            }
            // TODO check this
            auto* header_of_used_part = get_header_from_arena<used_block>(*block);

            header_of_used_part->size = size + difference;
            header_of_used_part->parent = this;

            occupied = *block;
        }
    } catch (...) {
        throw std::bad_alloc();
    }
    std::cout << "allocate " << print_blocks() << std::endl;
    // returned occupied block
    return occupied;
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{   _mem_header = other._mem_header;

    _trusted_memory = other._trusted_memory;

    if (_mem_header) {
        ++_mem_header->_refcount;
    }
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this == &other) {
        return *this;
    }
    // copy and swap
    allocator_sorted_list tmp = other;
    swap(tmp);
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    // checking the fit_mode is optional, because if this object has allocated memory, then its successor can delete this memory regardless of the fit_mode
    return dynamic_cast<const allocator_sorted_list*>(&other) != nullptr;
}

allocator_sorted_list::candidate allocator_sorted_list::find_block_to_deallocate(void *at) const {
    // search to deallocate
    sorted_free_iterator prev{_trusted_memory};
    for (auto cur = free_begin(); cur != free_end(); ++cur) {
        // <= because possible situation is: _trusted_memory->[used][free]
        if (*prev <= at && at < *cur) {
            return {cur, prev};
        }
        prev = cur;
    }
    if (*prev <= at) {
        return {free_end(), prev};
    }
    throw std::logic_error("Invalid pointer to deallocate");
}


void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!at) return;

    std::lock_guard lock(_mem_header->_mutex);

    // prev maybe not available
    auto [curr_free_block, prev_free_block] = find_block_to_deallocate(at);


    if (prev_free_block != free_end()) {
        // situation is: [free][used][free]
        void* prev_arena = *prev_free_block;
        void* curr_arena = *curr_free_block;

        auto* used_header = to_pointer<used_block>(block_end(prev_arena, prev_free_block.size()));
        if (used_header->parent != this) return;

        auto* used_arena = get_arena_from_header(used_header);

        auto* curr_header = get_header_from_arena<free_block>(curr_arena);
        auto* prev_header = get_header_from_arena<free_block>(prev_arena);

        if (block_end(used_arena, used_header->size) == curr_header) {
            used_header->size += curr_header->size;
            prev_header->next = curr_header->next;
        }
        prev_header->size += used_header->size;
    } else {
        // situation is: _trusted_memory->[used][free] => [free][used][free] or [free]
        void* curr_arena = *curr_free_block;

        auto* used_header = to_pointer<used_block>(_trusted_memory);

        if (used_header->parent != this) return;

        auto* used_arena = get_arena_from_header(used_header);

        auto* curr_header = get_header_from_arena<free_block>(curr_arena);

        // a used block becomes a free block
        auto* free_header = to_pointer<free_block>(used_header);

        if (block_end(used_arena, used_header->size) == curr_header) {
            // situation is: [used][free] => [   free   ]

            free_header->size += curr_header->size;
            free_header->next = curr_header->next;
        } else {
            // situation is: [   used   ][free] => [free][ used ][free]

            free_header->next = curr_header;

        }

        _mem_header->_first_block = get_arena_from_header(free_header);
    }
    std::cout << "deallocate " << print_blocks() << std::endl;
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard lock(_mem_header->_mutex);
    _mem_header->_fit_mode = mode;
}

void allocator_sorted_list::swap(allocator_sorted_list& other) noexcept {
    std::swap(_trusted_memory, other._trusted_memory);
    std::swap(_mem_header, other._mem_header);
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> blocks_info;

    for (auto it = sorted_iterator(_trusted_memory, _mem_header->_size, _mem_header->_first_block); it != end(); ++it) {
        blocks_info.emplace_back(it.size(), it.occupied());
    }
    return blocks_info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return {_mem_header->_first_block};
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return {}; // can {};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return {_trusted_memory, _mem_header->_size, _mem_header->_first_block};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return {}; // dummy block is free
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
    if (_free_ptr) {
        auto* header = get_header_from_arena<free_block>(_free_ptr);
        _free_ptr = header->next;
    }

    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}

std::size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (_free_ptr) {
        auto* header = get_header_from_arena<free_block>(_free_ptr);
        return header->size;
    }
    return {};
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() = default;

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted): _free_ptr(trusted) {
}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    // for compare with end()
    // if (_curr_free == nullptr && other._curr_free == nullptr) {
    //     return true;
    // }
    return /*_prev_free == other._prev_free && */
        _curr_free == other._curr_free &&
            _is_free == other._is_free;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_is_free) {
        _prev_free = _curr_free;

        if (_curr_free) {
            // move _curr_free pointer to next free_block
            auto it = sorted_free_iterator(_curr_free);
            _curr_free = *(++it);
        }
    }
    // if this is a used block, then _curr_free is already in the right place, due to the fact
    // that the used/free blocks alternate
    _is_free = !_is_free;
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}

std::size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    // situation is: _curr_free->[free]
    if (_is_free) {
        auto* free_header = get_header_from_arena<free_block>(_curr_free);
        return free_header->size;
    }
    // situation is: _trusted_memory->[used][free]
    if (!_prev_free) {
        auto* used_header = to_pointer<used_block>(_trusted_memory);
        return used_header->size;
    }
    // auto* prev_free_header = get_header_from_arena<free_block>(_prev_free);
    // auto* used_header = to_pointer<used_block>(block_end(_prev_free, prev_free_header->size));
    // return used_header->size;

    // situation is: _prev_free->[free][used][nullptr]<-_curr_free
    if (!_curr_free) {
        auto* prev_free_header = get_header_from_arena<free_block>(_prev_free);

        auto* used_header = block_end(_prev_free, prev_free_header->size);
        auto* used_arena = get_arena_from_header(used_header);

        auto* pool_end = block_end(_trusted_memory, _size);
        return std::distance(to_pointer<std::byte>(used_arena), to_pointer<std::byte>(pool_end));
    }
    // situation is: _prev_free->[free][used][free]
    return to_pointer<std::byte>(_curr_free) - to_pointer<std::byte>(_prev_free);
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    // situation is: _curr_free->[free]
    if (_is_free) {
        return _curr_free;
    }
    // situation is: _trusted_memory->[used][free]
    if (!_prev_free) {
        return get_arena_from_header(_trusted_memory);
    }
    // situation is: _prev_free->[free][used][free]
    auto* prev_free_header = get_header_from_arena<free_block>(_prev_free);
    return get_arena_from_header(block_end(_prev_free, prev_free_header->size));

}

allocator_sorted_list::sorted_iterator::sorted_iterator() = default;

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted, std::size_t size, void* first_free): _curr_free(first_free), _trusted_memory(trusted), _size(size), _is_free(get_arena_from_header(_trusted_memory) == _curr_free) {}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    return !_is_free;
}

allocator_sorted_list::find_error::find_error(const char* msg): _msg(msg) {}

const char* allocator_sorted_list::find_error::what() const noexcept {
    return _msg;
}

bool allocator_sorted_list::candidate::is_valid() const noexcept {
    return block != sorted_free_iterator();
}

template <typename T>
// requires (std::same_as<T, allocator_sorted_list::used_block> || std::same_as<T, allocator_sorted_list::free_block>
    // || std::same_as<T, void>)
T* allocator_sorted_list::to_pointer(auto* header) {
    return reinterpret_cast<T*>(header);
}
template <typename T>
// requires (std::same_as<T, allocator_sorted_list::used_block> || std::same_as<T, allocator_sorted_list::free_block>
    // || std::same_as<T, void>)
T* allocator_sorted_list::get_header_from_arena(void* arena, std::size_t meta) {
    return to_pointer<T>(reinterpret_cast<std::byte*>(arena) - meta);
}

void *allocator_sorted_list::get_arena_from_header(auto* header, std::size_t meta) {
    return reinterpret_cast<std::byte*>(header) + meta;
}

void* allocator_sorted_list::block_end(void *arena, std::size_t size) {
    return reinterpret_cast<std::byte*>(arena) + size;
}