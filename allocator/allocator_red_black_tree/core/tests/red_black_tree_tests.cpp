#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

#include <red_black_tree.h>

#include "allocator_red_black_tree.h"

namespace {

class block_factory {
    std::vector<std::unique_ptr<block>> _nodes;

public:
    block* make(std::size_t key, bool occupied = false) {
        auto node = std::make_unique<block>();
        node->meta.occupied = occupied;
        node->meta.color = block_color::RED;
        node->prev = nullptr;
        node->next = reinterpret_cast<block*>(reinterpret_cast<std::byte*>(node.get()) + key);
        node->free_node.parent = nullptr;
        node->free_node.left = nullptr;
        node->free_node.right = nullptr;

        block* raw = node.get();
        _nodes.push_back(std::move(node));
        return raw;
    }
};

std::size_t key_of(block* b) {
    return reinterpret_cast<std::byte*>(b->next) - reinterpret_cast<std::byte*>(b);
}

block* root_from(block* node) {
    block* curr = node;
    while (curr && curr->free_node.parent) {
        curr = curr->free_node.parent;
    }
    return curr;
}

int verify_rb_subtree(block* node, block* expected_parent) {
    if (!node) {
        return 1;
    }

    EXPECT_EQ(node->free_node.parent, expected_parent);

    if (node->meta.color == block_color::RED) {
        EXPECT_TRUE(!node->free_node.left || node->free_node.left->meta.color == block_color::BLACK);
        EXPECT_TRUE(!node->free_node.right || node->free_node.right->meta.color == block_color::BLACK);
    }

    if (node->free_node.left) {
        EXPECT_LE(key_of(node->free_node.left), key_of(node));
    }
    if (node->free_node.right) {
        EXPECT_GE(key_of(node->free_node.right), key_of(node));
    }

    int left_black_height = verify_rb_subtree(node->free_node.left, node);
    int right_black_height = verify_rb_subtree(node->free_node.right, node);
    EXPECT_EQ(left_black_height, right_black_height);

    return left_black_height + (node->meta.color == block_color::BLACK ? 1 : 0);
}

void assert_rb_invariants_from(block* any_existing_node) {
    ASSERT_NE(any_existing_node, nullptr);

    block* root = root_from(any_existing_node);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->free_node.parent, nullptr);
    EXPECT_EQ(root->meta.color, block_color::BLACK);

    (void)verify_rb_subtree(root, nullptr);
}

} // namespace

TEST(redBlackTreeCoreTests, FindInEmptyTreeReturnsNullptr) {
    red_black_tree tree;

    EXPECT_EQ(tree.find(16), nullptr);
}

TEST(redBlackTreeCoreTests, InsertSingleNodeMakesItReachableAndBlackRoot) {
    red_black_tree tree;
    block_factory factory;

    block* node = factory.make(32);
    tree.insert(node);

    EXPECT_EQ(tree.find(32), node);
    assert_rb_invariants_from(node);
}

TEST(redBlackTreeCoreTests, InsertMultipleNodesPreservesFindAndInvariants) {
    red_black_tree tree;
    block_factory factory;

    for (std::size_t key : {40u, 20u, 60u, 10u, 30u, 50u, 70u, 65u, 80u}) {
        tree.insert(factory.make(key));
    }

    for (std::size_t key : {10u, 20u, 30u, 40u, 50u, 60u, 65u, 70u, 80u}) {
        EXPECT_NE(tree.find(key), nullptr);
    }
    EXPECT_EQ(tree.find(999), nullptr);

    assert_rb_invariants_from(tree.find(40));
}

TEST(redBlackTreeCoreTests, DuplicateKeysSupportedAndKeptInValidOrder) {
    red_black_tree tree;
    block_factory factory;

    tree.insert(factory.make(24));
    tree.insert(factory.make(24));
    tree.insert(factory.make(24));
    tree.insert(factory.make(12));
    tree.insert(factory.make(36));

    EXPECT_NE(tree.find(24), nullptr);
    assert_rb_invariants_from(tree.find(24));
}

TEST(redBlackTreeCoreTests, EraseLeafNodePreservesInvariants) {
    red_black_tree tree;
    block_factory factory;

    tree.insert(factory.make(10));
    tree.insert(factory.make(5));
    tree.insert(factory.make(15));

    tree.erase(5);

    EXPECT_EQ(tree.find(5), nullptr);
    EXPECT_NE(tree.find(10), nullptr);
    EXPECT_NE(tree.find(15), nullptr);
    assert_rb_invariants_from(tree.find(10));
}

TEST(redBlackTreeCoreTests, EraseNodeWithOneChildPreservesInvariants) {
    red_black_tree tree;
    block_factory factory;

    tree.insert(factory.make(10));
    tree.insert(factory.make(5));
    tree.insert(factory.make(15));
    tree.insert(factory.make(12));

    tree.erase(15);

    EXPECT_EQ(tree.find(15), nullptr);
    EXPECT_NE(tree.find(12), nullptr);
    assert_rb_invariants_from(tree.find(10));
}

TEST(redBlackTreeCoreTests, EraseNodeWithTwoChildrenPreservesInvariants) {
    red_black_tree tree;
    block_factory factory;

    tree.insert(factory.make(20));
    tree.insert(factory.make(10));
    tree.insert(factory.make(30));
    tree.insert(factory.make(25));
    tree.insert(factory.make(40));

    tree.erase(30);

    EXPECT_EQ(tree.find(30), nullptr);
    EXPECT_NE(tree.find(25), nullptr);
    EXPECT_NE(tree.find(40), nullptr);
    assert_rb_invariants_from(tree.find(20));
}

TEST(redBlackTreeCoreTests, EraseUnknownKeyThrowsInvalidArgument) {
    red_black_tree tree;
    block_factory factory;

    tree.insert(factory.make(11));
    tree.insert(factory.make(22));

    EXPECT_THROW(tree.erase(999), std::invalid_argument);
    assert_rb_invariants_from(tree.find(11));
}

TEST(redBlackTreeCoreTests, InsertOccupiedBlockThrowsInvalidArgument) {
    red_black_tree tree;
    block_factory factory;

    EXPECT_THROW(tree.insert(factory.make(19, true)), std::invalid_argument);
    EXPECT_EQ(tree.find(19), nullptr);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}