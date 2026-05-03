#pragma once

#include <functional>

class allocator_red_black_tree;

enum class block_color : unsigned char { RED, BLACK };

struct block_data {
    bool occupied: 1;
    bool is_end: 1;
    block_color color: 2;
};

struct block {
    block_data meta{.occupied = false, .color = block_color::RED};
    block *prev{};
    block *next{};

    union {
        struct {
            block *parent{};
            block *left{};
            block *right{};
        } free_node{};

        struct {
            void *parent{};
        } occupied_node;
    };

    bool is_left_child();

    bool is_right_child();
};

class red_black_tree {
    friend allocator_red_black_tree;
    friend block;

    // struct rb_node {
    //     const std::size_t key{};
    //     // Value value{};
    //     rb_node* left{};
    //     rb_node* right{};
    //     rb_node* parent{};
    //     block_data color{.color=block_color::RED};
    //
    //     bool is_left_child();
    //
    //     bool is_right_child();
    // };
    static bool is_black(block *b);

    static bool is_red(block *b);

    void transplant(block *u, block *v);

    void rotate_left(block *p);

    void rotate_right(block *p);

    void fix_insert(block *b);

    void fix_erase(block *p, block *c, bool x_was_left_child);

    void erase_case1(block *p, bool x_was_left_child);

    void erase_case2(block *p, bool x_was_left_child);

    void erase_case3(block *p, bool x_was_left_child);

    void erase_case4(block *p, bool x_was_left_child);

    template<typename Cmp>
    block *find(std::size_t key, Cmp go_there) const;

    void erase_block(block* b);

    static std::size_t get_key(block *b);

    block *_root{};
    std::size_t _size{};

public:
    [[nodiscard]] block *find(std::size_t key) const;

    void insert(block *b);

    void erase(std::size_t key); // return deleted block
};


template<typename Cmp>
block *red_black_tree::find(std::size_t key, Cmp go_there) const {
    block *curr = _root;

    while (curr) {
        std::size_t curr_key = get_key(curr);

        int cmp = go_there(curr_key, key);

        if (cmp == 0) {
            return curr;
        }

        if (cmp < 0) {
            if (!curr->free_node.left) {
                break;
            }
            curr = curr->free_node.left;
        } else {
            if (!curr->free_node.right) {
                break;
            }
            curr = curr->free_node.right;
        }
    }
    return curr;
}