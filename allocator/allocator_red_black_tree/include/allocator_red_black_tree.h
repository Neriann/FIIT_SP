#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H

#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <cstddef>
#include <mutex>
#include <optional>

#include <red_black_tree.h>


class allocator_red_black_tree final :
        public smart_mem_resource,
        public allocator_test_utils,
        public allocator_with_fit_mode {
private:
    friend red_black_tree;

    struct memory_header {
        std::pmr::memory_resource *_parent;
        fit_mode _fit;
        std::size_t _size;
        red_black_tree *_root;
        std::mutex _mutex;
    };

    void *_trusted_memory;

    static constexpr std::size_t allocator_metadata_size = sizeof(memory_header);
    static constexpr std::size_t occupied_block_metadata_size =
            offsetof(block, occupied_node.parent) + sizeof(void *);
    static constexpr std::size_t free_block_metadata_size =
            offsetof(block, free_node.right) + sizeof(block *);

public:
    ~allocator_red_black_tree() override;

    allocator_red_black_tree(
        allocator_red_black_tree const &other) = delete;

    allocator_red_black_tree &operator=(
        allocator_red_black_tree const &other) = delete;

    allocator_red_black_tree(
        allocator_red_black_tree &&other) noexcept;

    allocator_red_black_tree &operator=(
        allocator_red_black_tree &&other) noexcept;

public:
    explicit allocator_red_black_tree(
        std::size_t space_size,
        std::pmr::memory_resource *parent_allocator = nullptr,
        allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

private:
    [[nodiscard]] void *do_allocate_sm(
        std::size_t size) override;

    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource &) const noexcept override;

    std::vector<allocator_test_utils::block_info> get_blocks_info() const override;

    inline void set_fit_mode(allocator_with_fit_mode::fit_mode mode) override;

private:
    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

    class rb_iterator {
        block *_block_ptr{};
        void *_trusted{};

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = void *;
        using reference = void *&;
        using pointer = void **;
        using difference_type = ptrdiff_t;

        bool operator==(const rb_iterator &) const noexcept;

        bool operator!=(const rb_iterator &) const noexcept;

        rb_iterator &operator++() & noexcept;

        rb_iterator operator++(int n);

        size_t size() const noexcept;

        void *operator*() const noexcept;

        block* get_ptr() const noexcept;

        bool occupied() const noexcept;

        rb_iterator(void *trusted);
    };

    friend class rb_iterator;

    rb_iterator begin() const noexcept;

    rb_iterator end() const noexcept;

    std::optional<std::pair<block*, std::size_t>> find_block_to_allocate(std::size_t size) const noexcept;

    static std::byte *get_arena_of_allocator(void *header);

    static block *get_header_from_arena_of_free(void *arena);

    static block *get_header_from_arena_of_occupied(void *arena);

    static std::byte *get_arena_from_header_of_free(void *header);

    static std::byte *get_arena_from_header_of_occupied(void *header);

    static std::byte *block_end(void *arena, std::size_t size);
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H
