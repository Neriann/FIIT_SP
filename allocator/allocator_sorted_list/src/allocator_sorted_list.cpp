#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"

allocator_sorted_list::~allocator_sorted_list()
{
    void* header = reinterpret_cast<std::byte*>(_trusted_memory) - allocator_metadata_size;

    ::operator delete(header); // TODO maybe need to use parent allocator
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
    // TODO need to check
    std::lock_guard lock(mtx);
    void* header = parent_allocator ? parent_allocator->allocate(space_size + allocator_metadata_size) : ::operator new(space_size + allocator_metadata_size);
    _trusted_memory = reinterpret_cast<std::byte*>(header) + allocator_metadata_size;

    _first_block = static_cast<std::byte*>(_trusted_memory) + block_metadata_size;

    _fit_mode = allocate_fit_mode;
    _size = space_size;
    ;}

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
    switch (_fit_mode) {
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
    std::lock_guard lock(mtx);

    void* occupied = nullptr;

    try {
        auto [block, prev_block] = find_block_to_allocate(size);

        std::size_t real_size = size + block_metadata_size;

        std::size_t cur_block_size = block.size();

        // quantity used bytes without meta
        std::size_t difference = cur_block_size - real_size;

        auto* header_of_free_part =
            reinterpret_cast<free_block*>(
                reinterpret_cast<std::byte*>(*block) - block_metadata_size);
        // auto* free_header = static_cast<free_block*>(header_of_free_part);

        // TODO need to check
        if (difference >= extra_memory_of_block) {

            header_of_free_part->size = difference;

            auto* header_of_used_part =
                reinterpret_cast<used_block*>(
                        reinterpret_cast<std::byte*>(*block) + difference);

            // auto* used_header = static_cast<used_block*>(header_of_used_part);
            header_of_used_part->size = size;
            header_of_used_part->parent = this; // TODO maybe not this, but parent

            occupied =
                reinterpret_cast<std::byte*>(header_of_used_part) + block_metadata_size;
        } else {
            // change previous pointer to free block
            if (prev_block == free_end()) { // if the first block wanted to use
                _first_block = header_of_free_part->next;
            } else {
                auto* prev_header_of_free_part =
                    reinterpret_cast<free_block*>(
                        reinterpret_cast<std::byte*>(*prev_block) - block_metadata_size);

                prev_header_of_free_part->next = header_of_free_part->next;
            }
            occupied = *block;
        }
    } catch (...) {
        throw std::bad_alloc();
    }
    // returned occupied block
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
    // checking the fit_mode is optional, because if this object has allocated memory, then its successor can delete this memory regardless of the fit_mode
    return dynamic_cast<const allocator_sorted_list*>(&other) != nullptr;
}

allocator_sorted_list::candidate allocator_sorted_list::find_block_to_deallocate(void *at) const {
    // search to deallocate
    sorted_free_iterator prev{_trusted_memory};
    for (auto cur = free_begin(); cur != free_end(); ++cur) {
        // <= because possible situation is: _pool_begin->[used][free]
        if (*prev <= at && at < *cur) {
            return {cur, prev};
        }
        prev = cur;
    }
    throw std::logic_error("Invalid pointer to deallocate");
}


void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!at) return;

    std::lock_guard lock(mtx);

    auto [prev_free_block, curr_free_block] = find_block_to_deallocate(at);

    // TODO realise get_header, maybe get_payload



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

    for (auto it = sorted_iterator(_trusted_memory, _first_block); it != end(); ++it) {
        blocks_info.emplace_back(it.size(), it.occupied());
    }
    // TODO maybe move
    return blocks_info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return {_first_block};
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return {nullptr}; // can {};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return {_trusted_memory, _first_block};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return {nullptr, nullptr};
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
        auto* header = reinterpret_cast<free_block*>(reinterpret_cast<std::byte*>(_free_ptr) - block_metadata_size);
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
        auto* header = reinterpret_cast<free_block*>(reinterpret_cast<std::byte*>(_free_ptr) - block_metadata_size);
        return header->size;
    }
    throw std::logic_error("Invalid iterator");
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
    return _prev_free == other._prev_free &&
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
        auto* free_header = reinterpret_cast<free_block*>(reinterpret_cast<std::byte*>(_curr_free) - block_metadata_size);
        return free_header->size;
    }
    // situation is: _pool_begin->[used][free]
    if (!_prev_free) {
        auto* used_header = reinterpret_cast<used_block*>(_trusted_memory);
        return used_header->size;
    }
    // situation is: _prev_free->[free][used][free]
    auto* prev_free_header = reinterpret_cast<free_block*>(reinterpret_cast<std::byte*>(_prev_free) - block_metadata_size);
    auto* used_header = reinterpret_cast<used_block*>(reinterpret_cast<std::byte*>(_prev_free) + prev_free_header->size);
    return used_header->size;
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    // situation is: _curr_free->[free]
    if (_is_free) {
        return _curr_free;
    }
    // situation is: _pool_begin->[used][free]
    if (!_prev_free) {
        return reinterpret_cast<std::byte*>(_trusted_memory) + block_metadata_size;
    }
    // situation is: _prev_free->[free][used][free]
    auto* prev_free_header = reinterpret_cast<free_block*>(reinterpret_cast<std::byte*>(_prev_free) - block_metadata_size);
    return reinterpret_cast<std::byte*>(_prev_free) + prev_free_header->size + block_metadata_size;

}

allocator_sorted_list::sorted_iterator::sorted_iterator() = default;

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted, void* first_free): _curr_free(first_free), _trusted_memory(trusted), _is_free(_trusted_memory == _curr_free) {}

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

// template <typename T>
// requires std::same_as<T, allocator_sorted_list::used_block> || std::same_as<T, allocator_sorted_list::free_block>
// T* allocator_sorted_list::get_header(void* arena) {
//     return reinterpret_cast<T*>(reinterpret_cast<std::byte*>(arena) - block_metadata_size);
// }