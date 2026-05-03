#include "allocator_red_black_tree.h"

bool red_black_tree::is_black(block* b) {
    return !b || b->meta.color == block_color::BLACK;
}

bool red_black_tree::is_red(block* b) {
    return !is_black(b);
}


void red_black_tree::transplant(block* u, block* v) {
    if (!u->free_node.parent) {
        _root = v;
    } else if (u->is_left_child()) {
        u->free_node.parent->free_node.left = v;
    } else {
        u->free_node.parent->free_node.right = v;
    }
    if (v) {
        v->free_node.parent = u->free_node.parent;
    }
}

void red_black_tree::rotate_left(block* p) {
    block* x = p->free_node.right;
    if (!x) return; // can't rotate left if right child is null

    p->free_node.right = x->free_node.left;

    if (x->free_node.left) x->free_node.left->free_node.parent = p;

    transplant(p, x);

    x->free_node.left = p;
    p->free_node.parent = x;
}

void red_black_tree::rotate_right(block* p) {
    block* x = p->free_node.left;
    if (!x) return; // can't rotate right if left child is null

    p->free_node.left = x->free_node.right;

    if (x->free_node.right) x->free_node.right->free_node.parent = p;

    transplant(p, x);

    x->free_node.right = p;
    p->free_node.parent = x;
}



std::size_t red_black_tree::get_key(block* b) {
    if (b->meta.is_end) {
        return std::numeric_limits<std::size_t>::max();
    }
    return reinterpret_cast<std::byte*>(b->next) - reinterpret_cast<std::byte*>(b);
}


block* red_black_tree::find(std::size_t key) const {
    block* curr = _root;

    while (curr) {

        std::size_t curr_key = get_key(curr);

        if (curr_key == key) {
            return curr;
        }

        curr = curr_key > key ? curr->free_node.left : curr->free_node.right;
    }
    return curr;
}

void red_black_tree::insert(block* b) {
    if (!b) throw std::invalid_argument("inserted block is null");
    if (b->meta.occupied) throw std::invalid_argument("inserted block is occupied");
    if (is_black(b)) throw std::invalid_argument("inserted block is black");

    std::size_t key = get_key(b);

    // A block can be removed and reinserted by allocator merge/split logic.
    b->free_node.parent = nullptr;
    b->free_node.left = nullptr;
    b->free_node.right = nullptr;

    if (!_root) {
        _root = b;
        ++_size;
        fix_insert(b);
        return;
    }
    block* curr = _root;
    while (curr) {

        std::size_t curr_key = get_key(curr);
        if (curr_key <= key) { // key == curr_key lie always in right subtree
            if (!curr->free_node.right) {
                curr->free_node.right = b;
                break;
            }
            curr = curr->free_node.right;
        } else {
            if (!curr->free_node.left) {
                curr->free_node.left = b;
                break;
            }
            curr = curr->free_node.left;
        }
    }
    b->free_node.parent = curr;
    ++_size;
    fix_insert(b);
}

void red_black_tree::fix_insert(block* b) {
    if (!b->free_node.parent) {
        b->meta.color = block_color::BLACK;
        return;
    }
    if (is_black(b->free_node.parent)) {
        return;
    }
    block* parent = b->free_node.parent;
    block* grandparent = parent->free_node.parent;

    block* uncle = parent->is_left_child() ? grandparent->free_node.right : grandparent->free_node.left;

    if (is_red(uncle)) {

        parent->meta.color = block_color::BLACK;
        uncle->meta.color = block_color::BLACK;

        grandparent->meta.color = block_color::RED;

        fix_insert(grandparent);
    } else {
        if (parent->is_left_child()) {
            if (b->is_right_child()) {
                rotate_left(parent);

                parent = b; // after rotation, b becomes the parent of the original parent
            }
            rotate_right(grandparent);

        } else {
            if (b->is_left_child()) {
                rotate_right(parent);

                parent = b; // after rotation, b becomes the parent of the original parent
            }
            rotate_left(grandparent);
        }
        parent->meta.color = block_color::BLACK;

        grandparent->meta.color = block_color::RED;
    }
}

void red_black_tree::erase_block(block* b) {
    if (!b) throw std::invalid_argument("invalid input");

    block* x = nullptr;          // child that replaces removed node position
    block* x_parent = nullptr;   // parent of x after transplant
    bool x_was_left_child = false;
    block_color removed_color;

    if (b->free_node.left && b->free_node.right) {
        block* successor = b->free_node.right;
        while (successor->free_node.left) {
            successor = successor->free_node.left;
        }

        removed_color = successor->meta.color;
        x = successor->free_node.right;

        if (successor->free_node.parent == b) {
            x_parent = successor;
            x_was_left_child = false;
            if (x) x->free_node.parent = successor;
        } else {
            x_parent = successor->free_node.parent;
            x_was_left_child = successor->is_left_child();

            transplant(successor, successor->free_node.right);

            successor->free_node.right = b->free_node.right;
            successor->free_node.right->free_node.parent = successor;
        }

        transplant(b, successor);
        successor->free_node.left = b->free_node.left;
        successor->free_node.left->free_node.parent = successor;
        successor->meta.color = b->meta.color;
    } else {
        block* child = b->free_node.left ? b->free_node.left : b->free_node.right;
        removed_color = b->meta.color;
        x_parent = b->free_node.parent;
        x_was_left_child = b->is_left_child();
        x = child;

        transplant(b, child);
    }

    if (removed_color == block_color::BLACK) {
        fix_erase(x_parent, x, x_was_left_child);
    }
    --_size;
}

void red_black_tree::erase(std::size_t key) {
    block* b = find(key);

    erase_block(b);
}

// x - deleted node, p - parent of x, c - child of x (can be null)
// if child is Root/RED - just recolor it to BLACK
void red_black_tree::fix_erase(block* p, block* c, bool x_was_left_child) {
    if (!p && !c) return;

    if (c && (!p || is_red(c))) {
        c->meta.color = block_color::BLACK;
        return;
    }
    erase_case1(p, x_was_left_child);
}

// Brother - RED

void red_black_tree::erase_case1(block* p, bool x_was_left_child) {
    if (!p) return;

    block* sibling = x_was_left_child ? p->free_node.right : p->free_node.left;

    if (is_red(sibling)) {

        p->meta.color = block_color::RED;
        sibling->meta.color = block_color::BLACK;

        if (x_was_left_child) {
            rotate_left(p);
        } else {
            rotate_right(p);
        }
    }

    erase_case2(p, x_was_left_child);
}

// Brother - BLACK
void red_black_tree::erase_case2(block* p, bool x_was_left_child) {
    if (!p) return;

    block* sibling = x_was_left_child ? p->free_node.right : p->free_node.left;
    if (!sibling) return;

    // Brother - BLACK, both nephew - BLACK
    if (is_black(sibling->free_node.left) && is_black(sibling->free_node.right)) {
        sibling->meta.color = block_color::RED;
        fix_erase(p->free_node.parent, p, p->is_left_child());
    } else {
        erase_case3(p, x_was_left_child);
    }
}

void red_black_tree::erase_case3(block* p, bool x_was_left_child) {
    block* sibling = x_was_left_child ?p->free_node.right : p->free_node.left;
    if (!sibling) return;

    // Brother - BLACK, near-nephew - RED, far-nephew - BLACK
    if (x_was_left_child && is_red(sibling->free_node.left) && is_black(sibling->free_node.right)) {
        sibling->meta.color = block_color::RED;
        sibling->free_node.left->meta.color = block_color::BLACK;
        rotate_right(sibling);
    } else if (!x_was_left_child && is_red(sibling->free_node.right) && is_black(sibling->free_node.left)) {
        sibling->meta.color = block_color::RED;
        sibling->free_node.right->meta.color = block_color::BLACK;
        rotate_left(sibling);
    }
     erase_case4(p, x_was_left_child);
}

// Brother - BLACK, near-nephew - Any, far-nephew - RED
void red_black_tree::erase_case4(block* p, bool x_was_left_child) {
    block* sibling = x_was_left_child ? p->free_node.right : p->free_node.left;
    if (!sibling) return;

    sibling->meta.color = p->meta.color;
    p->meta.color = block_color::BLACK;

    if (x_was_left_child) {
        if (sibling->free_node.right) sibling->free_node.right->meta.color = block_color::BLACK;
        rotate_left(p);
    } else {
        if (sibling->free_node.left) sibling->free_node.left->meta.color = block_color::BLACK;
        rotate_right(p);
    }
}


bool block::is_left_child() {
    return free_node.parent && free_node.parent->free_node.left == this;
}

bool block::is_right_child() {
    return free_node.parent && free_node.parent->free_node.right == this;
}