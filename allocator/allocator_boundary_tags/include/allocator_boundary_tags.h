#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H

#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <pp_allocator.h>
#include <iterator>
#include <mutex>
#include <optional>

class allocator_boundary_tags final :
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:
    struct occupied_block {
        std::size_t size;
        occupied_block* prev;
        occupied_block* next;
        void* parent;
    };

    struct memory_header {
        std::pmr::memory_resource *_parent;
        occupied_block *_first_block;
        std::size_t _size;
        fit_mode _fit_mode;
        std::mutex _mutex;
    };


    static constexpr std::size_t allocator_metadata_size = sizeof(memory_header);

    static constexpr std::size_t occupied_block_metadata_size = sizeof(occupied_block);

    static constexpr std::size_t free_block_metadata_size = 0;

    void *_trusted_memory;

public:
    
    ~allocator_boundary_tags() override;
    
    allocator_boundary_tags(allocator_boundary_tags const &other) = delete;
    
    allocator_boundary_tags &operator=(allocator_boundary_tags const &other) = delete;
    
    allocator_boundary_tags(
        allocator_boundary_tags &&other) noexcept;
    
    allocator_boundary_tags &operator=(
        allocator_boundary_tags &&other) noexcept;

public:
    
    explicit allocator_boundary_tags(
            std::size_t space_size,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

private:
    
    [[nodiscard]] void *do_allocate_sm(
        std::size_t bytes) override;
    
    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;

    void swap(allocator_boundary_tags &other) noexcept;

public:
    
    std::vector<allocator_test_utils::block_info> get_blocks_info() const override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

/** TODO: Highly recommended for helper functions to return references */

    class boundary_iterator
    {
        occupied_block* _occupied_ptr{};
        void* _trusted_memory{};
        bool _occupied{};

    public:

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const boundary_iterator&) const noexcept;

        bool operator!=(const boundary_iterator&) const noexcept;

        boundary_iterator& operator++() & noexcept;

        boundary_iterator& operator--() & noexcept;

        boundary_iterator operator++(int n);

        boundary_iterator operator--(int n);

        [[nodiscard]] std::size_t size() const noexcept;

        [[nodiscard]] bool occupied() const noexcept;

        void* operator*() const noexcept;

        [[nodiscard]] occupied_block* get_ptr() const noexcept;

        boundary_iterator() = default;

        boundary_iterator(void* trusted);
    };

    friend class boundary_iterator;

    boundary_iterator begin() const noexcept;

    boundary_iterator end() const noexcept;

    struct allocation_candidate {
        occupied_block* prev;
        std::size_t free_size;
    };

    std::optional<allocation_candidate> find_block_to_allocate(std::size_t size) const noexcept;

    // arena is free block workspace
    static std::byte* get_arena_of_allocator(void* header);

    static occupied_block *get_header_from_arena(void *arena);

    static std::byte *get_arena_from_header(void *header);

    static std::byte *block_end(void *arena, std::size_t size);
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H