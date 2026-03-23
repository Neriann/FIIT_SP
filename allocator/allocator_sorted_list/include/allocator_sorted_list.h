#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_SORTED_LIST_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_SORTED_LIST_H

#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <iterator>
#include <mutex>
#include <optional>

class allocator_sorted_list final :
        public smart_mem_resource,
        public allocator_test_utils,
        public allocator_with_fit_mode {
private:
    struct free_block;

    struct used_block;

    struct memory_header;

    // it looks like ControlBlock in the shared_ptr
    memory_header *_mem_header;
    void *_trusted_memory; // for easier access (you don't have to store it.)

    static std::size_t extra_memory_of_block() noexcept;

    static std::size_t allocator_metadata_size() noexcept;

    static std::size_t block_metadata_size() noexcept;

public:
    explicit allocator_sorted_list(
        std::size_t space_size,
        std::pmr::memory_resource *parent_allocator = nullptr,
        allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

    allocator_sorted_list(
        allocator_sorted_list const &other) = delete;

    allocator_sorted_list &operator=(
        allocator_sorted_list const &other) = delete;

    allocator_sorted_list(
        allocator_sorted_list &&other) noexcept;

    allocator_sorted_list &operator=(
        allocator_sorted_list &&other) noexcept;

    ~allocator_sorted_list() override;

private:
    [[nodiscard]] void *do_allocate_sm(
        std::size_t size) override;

    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource &) const noexcept override;

    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;

    void swap(allocator_sorted_list &other) noexcept;

    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

private:
    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

    // For inserting (Algorithm A)
    class sorted_free_iterator {
        void *_free_ptr{};

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = void *;
        using reference = void *&;
        using pointer = void **;
        using difference_type = ptrdiff_t;

        bool operator==(const sorted_free_iterator &) const noexcept;

        bool operator!=(const sorted_free_iterator &) const noexcept;

        // moving through real free data (not meta)
        sorted_free_iterator &operator++() & noexcept;

        sorted_free_iterator operator++(int);

        // return real size of data
        std::size_t size() const noexcept;

        // return ptr to payload
        void *operator*() const noexcept;

        sorted_free_iterator();

        sorted_free_iterator(void *trusted);
    };

    // For clearing (algorithm B)
    class sorted_iterator // used to move through all blocks
    {
        void *_current_block{}; // arena pointer of the current block
        void *_next_free{}; // arena pointer of the next free block at or after current
        void *_trusted_memory{};
        std::size_t _size{};
        bool _is_free{false};

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = void *;
        using reference = void *&;
        using pointer = void **;
        using difference_type = ptrdiff_t;

        bool operator==(const sorted_iterator &) const noexcept;

        bool operator!=(const sorted_iterator &) const noexcept;

        sorted_iterator &operator++() & noexcept;

        sorted_iterator operator++(int n);

        std::size_t size() const noexcept;

        void *operator*() const noexcept;

        bool occupied() const noexcept;

        sorted_iterator();

        sorted_iterator(void *trusted, std::size_t size, void *first_free);
    };

    friend class sorted_iterator;
    friend class sorted_free_iterator;

    sorted_free_iterator free_begin() const noexcept;

    sorted_free_iterator free_end() const noexcept;

    sorted_iterator begin() const noexcept;

    sorted_iterator end() const noexcept;

    class find_error : public std::exception {
    public:
        find_error(const char *msg);

        const char *what() const noexcept override;

    private:
        const char *_msg{};
    };

    struct candidate {
        sorted_free_iterator block{};
        sorted_free_iterator prev_block{};

        bool is_valid() const noexcept;
    };

    // search to allocate
    std::optional<candidate> find_block_to_allocate(std::size_t size) const noexcept;

    std::optional<candidate> find_first_block_to_allocate(std::size_t size) const noexcept;

    // pattern Strategy
    template<typename Compare>
    std::optional<candidate> find_block_to_allocate_impl(std::size_t size, Compare cmp) const noexcept;

    candidate find_block_to_deallocate(void *at) const noexcept;

    // arena is free block workspace
    template<typename T>
    // requires (std::same_as<T, used_block> || std::same_as<T, free_block> || std::same_as<T, void>)
    static T *to_pointer(auto *header);

    template<typename T>
    // requires (std::same_as<T, used_block> || std::same_as<T, free_block> || std::same_as<T, void>)
    static T *get_header_from_arena(void *arena, std::size_t meta = block_metadata_size());

    static void *get_arena_from_header(auto *header, std::size_t meta = block_metadata_size());

    static void *block_end(void *arena, std::size_t size);
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_SORTED_LIST_H
