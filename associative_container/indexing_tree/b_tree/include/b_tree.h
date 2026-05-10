#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

#include <associative_container.h>
#include <boost/container/static_vector.hpp>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <not_implemented.h>
#include <pp_allocator.h>
#include <print>
#include <stack>
#include <utility>

template<typename TKey, typename TValue,
    comparator<TKey> Compare = std::less<TKey>, std::size_t t = 5>
class B_tree final {
public:
    using tree_data_type = std::pair<TKey, TValue>;
    using tree_data_type_const = std::pair<const TKey, TValue>;
    using value_type = tree_data_type_const;

private:
    static constexpr std::size_t minimum_keys_in_node = t - 1;
    static constexpr std::size_t maximum_keys_in_node = 2 * t - 1;

    struct btree_node;

    /**
         *  use tree_data_type because more easily to erase/update value if key
         * exists, but for iterators and element access return const key to avoid
         * key modification
         *
         *  use static_vector because of better cache locality and performance
         * (not-moving in memory) and fixed size of keys and children in node
         */
    using keys_container =
        boost::container::static_vector<
            tree_data_type,
            maximum_keys_in_node
        >;

    using children_container =
        boost::container::static_vector<
            btree_node *,
            maximum_keys_in_node + 1
        >;

    // region comparators declaration

    auto compare_keys(const TKey &lhs, const TKey &rhs) const -> bool;

    auto compare_pairs(const tree_data_type &lhs,
                       const tree_data_type &rhs) const -> bool;

    auto is_equals_keys(const TKey &lhs, const TKey &rhs) const -> bool;

    // endregion comparators declaration

    struct btree_node {

        keys_container _keys;
        children_container _children;

        explicit btree_node(tree_data_type data);

        btree_node(
            keys_container keys,
            children_container children = {});
    };


    using value_allocator = pp_allocator<value_type>;
    using node_allocator =
    std::allocator_traits<value_allocator>::template rebind_alloc<btree_node>;

    using allocator_traits = std::allocator_traits<value_allocator>;
    using node_allocator_traits = std::allocator_traits<node_allocator>;

    // to avoid empty base class optimization (c++20 style - avoid problem when
    // cmp is final or pointer to function)
    [[no_unique_address]] Compare _compare;

    pp_allocator<value_type> _allocator;
    btree_node *_root{};
    std::size_t _size{};

    auto get_allocator() const noexcept -> pp_allocator<value_type>;

public:
    // region constructors declaration

    explicit B_tree(const Compare &cmp = Compare(),
                    pp_allocator<value_type> alloc = pp_allocator<value_type>());

    explicit B_tree(pp_allocator<value_type> alloc,
                    const Compare &cmp = Compare());

    template<input_iterator_for_pair<TKey, TValue> iterator>
    explicit B_tree(iterator begin, iterator end, const Compare &cmp = Compare(),
                    pp_allocator<value_type> = pp_allocator<value_type>());

    B_tree(std::initializer_list<std::pair<TKey, TValue> > data,
           const Compare &cmp = Compare(),
           pp_allocator<value_type> = pp_allocator<value_type>());

    // endregion constructors declaration

    // region five declaration

    B_tree(const B_tree &other);

    B_tree(B_tree &&other) noexcept;

    auto operator=(const B_tree &other) -> B_tree &;

    auto operator=(B_tree &&other) noexcept -> B_tree &;

    ~B_tree() noexcept;

    // endregion five declaration

    // region iterators declaration

    template<bool IsConst>
    class btree_iterator_base final {
        using node_ptr_type =
        std::conditional_t<IsConst, const btree_node *, btree_node *>;
        using stack_type = std::stack<std::pair<
            node_ptr_type, std::size_t> >; // node and index of child in parent node

        node_ptr_type _root;
        stack_type _path;
        std::size_t _index; // in current node

        /**
         * problem: keep non-const key but must return const key to avoid
         * modification (value can be modified for non-const iterator) solve: create
         * wrapper on pair of key and value to return const key and non-const value
         * for non-const iterator, and const key and const value for const iterator
         */
        struct proxy {
            const TKey &first;
            std::conditional_t<IsConst, const TValue &, TValue &> second;

            /**
             * only for non-const iterator, for const iterator this operator will not
             * be called because of const correctness
             */
            operator value_type() const { return {first, second}; }
        };

        /**
         * to return reference to value and const reference to key without copying
         * pair for non-const iterator, for const iterator this field will not be
         * used because of const correctness
         */
        mutable std::optional<proxy> _proxy{};

    public:
        using value_type = tree_data_type_const;
        using reference = proxy; // on value access (references in fields)
        using pointer = proxy *;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_iterator_base;

        friend class B_tree;
        // friend class btree_reverse_iterator;

        auto operator*() const noexcept -> reference; // TODO check this

        auto operator->() const noexcept -> pointer; // TODO check this

        // template <typename U>
        // U& operator->*(U value_type::*p); TODO maybe implement in future

        auto operator++() -> self &;

        auto operator++(int) -> self;

        // self& operator+=(int n); TODO maybe implement in future

        auto operator--() -> self &;

        auto operator--(int) -> self;

        // self& operator-=(int n);

        auto operator==(const self &other) const noexcept -> bool;

        auto operator!=(const self &other) const noexcept -> bool;

        [[nodiscard]] auto depth() const noexcept -> std::size_t;

        [[nodiscard]] auto current_node_keys_count() const noexcept -> std::size_t;

        // [[nodiscard]] bool is_terminate_node() const noexcept;

        [[nodiscard]] auto index() const noexcept -> std::size_t;

        explicit btree_iterator_base(node_ptr_type root = nullptr,
                                     const stack_type &path = stack_type(),
                                     std::size_t index = 0) noexcept;
    };

    using btree_iterator = btree_iterator_base<false>;
    using btree_const_iterator = btree_iterator_base<true>;

    template<bool IsConst>
    friend class btree_iterator_base;

    template<typename Iterator> // pattern adapter
    class btree_reverse_iterator_base final {
        Iterator _base_iterator;

    public:
        using value_type = std::iterator_traits<Iterator>::value_type;
        using reference = std::iterator_traits<Iterator>::reference;
        using pointer = std::iterator_traits<Iterator>::pointer;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_reverse_iterator_base;

        friend class B_tree;

        // template <bool IsConst>
        // friend class btree_iterator_base;

        btree_reverse_iterator_base(const Iterator &it) noexcept;

        auto operator*() const noexcept -> reference;

        auto operator->() const noexcept -> pointer;

        // template <typename U>
        // U& operator->*(U value_type::*p); TODO maybe implement in future

        auto operator++() -> self &;

        auto operator++(int) -> self;

        // self& operator+=(int n);

        auto operator--() -> self &;

        auto operator--(int) -> self;

        // self& operator-=(int n);

        auto operator==(const self &other) const noexcept -> bool;

        auto operator!=(const self &other) const noexcept -> bool;

        [[nodiscard]] auto depth() const noexcept -> std::size_t;

        [[nodiscard]] auto current_node_keys_count() const noexcept -> std::size_t;

        // bool is_terminate_node() const noexcept;

        [[nodiscard]] auto index() const noexcept -> std::size_t;

        /**
         *
         * @return returns normal iterator to element next to current reverse
         * iterator element
         */
        auto base() const noexcept -> Iterator;
    };

    using btree_reverse_iterator = btree_reverse_iterator_base<btree_iterator>;
    using btree_const_reverse_iterator =
    btree_reverse_iterator_base<btree_const_iterator>;

    template<typename Iterator>
    friend class btree_reverse_iterator_base;

    // endregion iterators declaration

    // region element access declaration

    /*
     * Returns a reference to the mapped value of the element with specified key.
     * If no such element exists, an exception of type std::out_of_range is
     * thrown.
     */

    template<typename Self>
    auto at(this Self &&self,
            const TKey &key) -> auto &&; // auto&& by reference collapsing

    /*
     * If key not exists, makes default initialization of value
     */
    auto operator[](const TKey &key) -> TValue &;

    auto operator[](TKey &&key) -> TValue &;

    // endregion element access declaration
    // region iterator begins declaration

    auto begin() -> btree_iterator;

    auto end() -> btree_iterator;

    auto begin() const -> btree_const_iterator;

    auto end() const -> btree_const_iterator;

    auto cbegin() const -> btree_const_iterator;

    auto cend() const -> btree_const_iterator;

    auto rbegin() -> btree_reverse_iterator;

    auto rend() -> btree_reverse_iterator;

    auto rbegin() const -> btree_const_reverse_iterator;

    auto rend() const -> btree_const_reverse_iterator;

    auto crbegin() const -> btree_const_reverse_iterator;

    auto crend() const -> btree_const_reverse_iterator;

    // endregion iterator begins declaration

    // region lookup declaration

    [[nodiscard]] auto size() const noexcept -> std::size_t;

    [[nodiscard]] auto empty() const noexcept -> bool;

    /*
     * Returns end() if not exist
     */

    /**
     *
     * helper type to select const or non-const iterator type based on constness
     * of self parameter in find/lower_bound/upper_bound to avoid code duplication
     */
    template<typename Self>
    using select_iterator_t =
    std::conditional_t<std::is_const_v<std::remove_reference_t<Self> >,
        btree_const_iterator, btree_iterator>;

    template<typename Self>
    auto find(this Self &&self, const TKey &key) -> select_iterator_t<Self>;

    template<typename Self>
    auto lower_bound(this Self &&self, const TKey &key) -> select_iterator_t<Self>;

    template<typename Self>
    auto upper_bound(this Self &&self, const TKey &key) -> select_iterator_t<Self>;

    auto contains(const TKey &key) const -> bool;

    // endregion lookup declaration

    // region modifiers declaration

    void clear() noexcept;

    /*
     * Does nothing if key exists, delegates to emplace.
     * Second return value is true, when inserted
     */
    auto insert(const tree_data_type &data) -> std::pair<btree_iterator, bool>;

    auto insert(tree_data_type &&data) -> std::pair<btree_iterator, bool>;

    template<typename... Args>
        requires std::constructible_from<tree_data_type, Args...>
    auto emplace(Args &&... args) -> std::pair<btree_iterator, bool>;

    /*
     * Updates value if key exists, delegates to emplace.
     */
    auto insert_or_assign(const tree_data_type &data) -> btree_iterator;

    auto insert_or_assign(tree_data_type &&data) -> btree_iterator;

    template<typename... Args>
        requires std::constructible_from<tree_data_type, Args...>
    auto emplace_or_assign(Args &&... args) -> btree_iterator;

    /*
     * Return iterator to node next ro removed or end() if key not exists
     */
    auto erase(btree_iterator pos) -> btree_iterator;

    auto erase(btree_const_iterator pos) -> btree_iterator;

    auto erase(btree_iterator beg, btree_iterator en) -> btree_iterator;

    auto erase(btree_const_iterator beg, btree_const_iterator en) -> btree_iterator;

    auto erase(const TKey &key) -> btree_iterator;

    // endregion modifiers declaration

private:
    // region helpers declaration

    static auto is_leaf(const btree_node *node) noexcept -> bool;

    static auto is_node_full(const btree_node *node) noexcept -> bool;

    static auto is_node_small(const btree_node *node) noexcept -> bool;

    template <typename Self, typename BoundFunc>
    auto bound_search(this Self&& self, const TKey& key, BoundFunc bound_func, bool exact_match) -> select_iterator_t<Self>;

    static auto find_lower_bound_at_node(const btree_node *node,
                                  const TKey &key, Compare compare) noexcept -> std::size_t;

    static auto find_upper_bound_at_node(const btree_node *node,
                                  const TKey &key, Compare compare) noexcept -> std::size_t;

    auto
    find_position_for_insertion(const TKey &key,
                                btree_iterator::stack_type &path) -> std::pair<std::size_t, bool>;

    auto find_position_for_erasion(const TKey &key,
                                   btree_iterator::stack_type &path,
                                   std::size_t &index) noexcept -> std::size_t;

    void erase_key_at_node(btree_iterator::stack_type &path,
                           std::size_t &index) noexcept;

    std::size_t rebalance_on_erasion(btree_node *parent, std::size_t child_index);

    void split(btree_node *child, btree_node *parent, std::size_t child_index);

    void merge_left(btree_node *parent, std::size_t child_index);

    void merge_right(btree_node *parent, std::size_t child_index);

    void rotate_left(btree_node *parent, std::size_t child_index);

    void rotate_right(btree_node *parent, std::size_t child_index);

    auto deep_copy_tree(const btree_node *node) const -> btree_node*;

    void delete_subtree(btree_node *node) noexcept;

    // endregion helpers declaration
};


template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::btree_node::btree_node(tree_data_type data)
    : _keys{data} {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::btree_node::btree_node(keys_container keys, children_container children)
    : _keys(keys), _children(children) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::compare_pairs(
    const tree_data_type &lhs, const tree_data_type &rhs) const -> bool {
    return compare_keys(lhs.first, rhs.first);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::compare_keys(const TKey &lhs,
                                                    const TKey &rhs) const -> bool {
    return _compare(lhs, rhs);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::is_equals_keys(const TKey &lhs,
                                                      const TKey &rhs) const -> bool {
    return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::get_allocator() const noexcept -> pp_allocator<typename B_tree<TKey, TValue, Compare, t>::value_type> {
    return _allocator;
}

// region constructors implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::B_tree(const Compare &cmp,
                                         pp_allocator<value_type> alloc)
    : _compare(cmp), _allocator(alloc) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::B_tree(pp_allocator<value_type> alloc,
                                         const Compare &cmp)
    : _compare(cmp), _allocator(alloc) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<input_iterator_for_pair<TKey, TValue> iterator>
B_tree<TKey, TValue, Compare, t>::B_tree(iterator begin, iterator end,
                                         const Compare &cmp,
                                         pp_allocator<value_type> alloc)
    : _compare(cmp), _allocator(alloc) {
    for (auto it = begin; it != end; ++it) {
        insert(*it);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::B_tree(
    std::initializer_list<std::pair<TKey, TValue> > data, const Compare &cmp,
    pp_allocator<value_type> alloc)
    : _compare(cmp), _allocator(alloc) {
    for (const auto &item: data) {
        insert(item);
    }
}

// endregion constructors implementation

// region five implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::B_tree(const B_tree &other)
    : _compare(other._compare), _allocator(allocator_traits::select_on_container_copy_construction(other._allocator)),
      _size(other._size) {

    _root = deep_copy_tree(other._root);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::B_tree(B_tree &&other) noexcept
    : _compare(std::exchange(other._compare, Compare())),
      _allocator(std::exchange(other._allocator, pp_allocator<value_type>())),
      _root(std::exchange(other._root, nullptr)),
      _size(std::exchange(other._size, 0)) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::operator=(const B_tree &other) -> B_tree<TKey, TValue, Compare, t> & {
    if (this == &other) {
        return *this;
    }
    // copy and swap
    B_tree temp(other);

    std::swap(_compare, temp._compare);
    std::swap(_root, temp._root);
    std::swap(_size, temp._size);

    if (allocator_traits::propagate_on_container_copy_assignment::value) {
        std::swap(_allocator, temp._allocator);
    }
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::operator=(B_tree &&other) noexcept -> B_tree<TKey, TValue, Compare, t> & {
    if (this == &other) {
        return *this;
    }
    // move and swap
    B_tree temp(std::move(other));

    std::swap(_compare, temp._compare);
    std::swap(_root, temp._root);
    std::swap(_size, temp._size);

    if (allocator_traits::propagate_on_container_move_assignment::value) {
        std::swap(_allocator, temp._allocator);
    }
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
B_tree<TKey, TValue, Compare, t>::~B_tree() noexcept {
    clear();
}

// endregion five implementation

// region iterators implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator*()
const noexcept -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::reference {
    auto &kv = _path.top().first->_keys[_index];
    return {kv.first, kv.second};
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator->()
const noexcept -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::pointer {
    auto &kv = _path.top().first->_keys[_index];
    _proxy.emplace(proxy{kv.first, kv.second});
    return &_proxy.value();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator++() -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::self & {
    // InOrderTraversal
    if (_path.empty()) {
        return *this;
    }
    node_ptr_type node = _path.top().first;

    if (is_leaf(node) && _index + 1 < node->_keys.size()) {
        // go right in leaf
        ++_index;
    } else if (!is_leaf(node)) {
        // go down to leftmost child of right child
        std::size_t child_index = _index + 1;
        node = node->_children[child_index];
        _path.emplace(node, child_index);

        while (!is_leaf(node)) {
            node = node->_children.front();
            _path.emplace(node, 0);
        }
        _index = 0;
    } else {
        // go up until we come from left
        auto [cur_node, child_index] = _path.top();
        _path.pop();

        while (!_path.empty()) {
            node_ptr_type parent = _path.top().first;
            if (child_index < parent->_keys.size()) {
                _index = child_index;
                return *this;
            }
            child_index = _path.top().second;
            _path.pop();
        }
        _index = 0;
    }
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator++(
    int) -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::self {
    btree_iterator_base temp = *this;
    ++(*this);
    return temp;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator--() -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::self & {
    // InOrderReverseTraversal
    if (_path.empty()) {
        // --end() -> max_key
        if (_root == nullptr) {
            return *this;
        }
        auto *node = _root;

        while (!is_leaf(node)) {
            _path.emplace(node, node->_children.size() - 1);
            node = node->_children.back();
        }

        _path.emplace(node, node->_keys.size() - 1);
        return *this;
    }
    node_ptr_type node = _path.top().first;

    if (is_leaf(node) && _index > 0) {
        // go left in leaf
        --_index;
    } else if (!is_leaf(node)) {
        // go down to rightmost child of left child
        std::size_t child_index = _index;
        node = node->_children[child_index];
        _path.emplace(node, child_index);

        while (!is_leaf(node)) {
            node = node->_children.back();
            _path.emplace(node, node->_children.size() - 1);
        }
        _index = node->_keys.size() - 1;
    } else {
        // go up until we come from right
        auto [cur_node, child_index] = _path.top();
        _path.pop();

        while (!_path.empty()) {
            node_ptr_type parent = _path.top().first;
            if (child_index > 0) {
                _index = child_index - 1;
                return *this;
            }
            child_index = _path.top().second;
            _path.pop();
        }
        _index = 0;
    }
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator--(
    int) -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_iterator_base<IsConst>::self {
    btree_iterator_base temp = *this;
    --(*this);
    return temp;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator==(
    const self &other) const noexcept -> bool {
    if (_root != other._root) {
        return false;
    }
    if (_path.empty() && other._path.empty()) {
        return true;
    }
    return _path == other._path && _index == other._index;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::operator!=(
    const self &other) const noexcept -> bool {
    return !(*this == other);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::depth()
const noexcept -> std::size_t {
    if (_path.empty()) {
        return 0;
    }
    return _path.size() - 1;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto B_tree<TKey, TValue, Compare, t>::btree_iterator_base<
    IsConst>::current_node_keys_count() const noexcept -> std::size_t {
    return _path.top().first->_keys.size();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
auto
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<IsConst>::index()
const noexcept -> std::size_t {
    return _index;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<bool IsConst>
B_tree<TKey, TValue, Compare, t>::btree_iterator_base<
    IsConst>::btree_iterator_base(node_ptr_type root, const stack_type &path,
                                  std::size_t index) noexcept
    : _root(root), _path(path), _index(index) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<
    Iterator>::btree_reverse_iterator_base(const Iterator &it) noexcept
    : _base_iterator(it) {
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator*() const noexcept -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::reference {
    Iterator temp = _base_iterator;
    return *--temp;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator->() const noexcept -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::pointer {
    Iterator temp = _base_iterator;
    return --temp.operator->();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator++() -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::self & {
    --_base_iterator;
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator++(int) -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::self {
    self temp = *this;
    --_base_iterator;
    return temp;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator--() -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::self & {
    ++_base_iterator;
    return *this;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare,
    t>::btree_reverse_iterator_base<Iterator>::operator--(int) -> typename B_tree<TKey, TValue, Compare,
    t>::template btree_reverse_iterator_base<Iterator>::self {
    self temp = *this;
    ++_base_iterator;
    return temp;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<
    Iterator>::operator==(const self &other) const noexcept -> bool {
    return _base_iterator == other._base_iterator;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<
    Iterator>::operator!=(const self &other) const noexcept -> bool {
    return !(*this == other);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<Iterator>::depth()
const noexcept -> std::size_t {
    return _base_iterator.depth();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<
    Iterator>::current_node_keys_count() const noexcept -> std::size_t {
    return _base_iterator.current_node_keys_count();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<Iterator>::index()
const noexcept -> std::size_t {
    return _base_iterator.index();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Iterator>
auto
B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator_base<Iterator>::base()
const noexcept -> Iterator {
    return _base_iterator;
}

// endregion iterators implementation

// region element access implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Self>
auto B_tree<TKey, TValue, Compare, t>::at(this Self &&self, const TKey &key) -> auto && {
    auto it = self.find(key);
    if (it == self.end()) {
        throw std::out_of_range("B_tree::at: key not found");
    }
    return it->second;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::operator[](const TKey &key) -> TValue & {
    return emplace(key, TValue{}).first->second;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::operator[](TKey &&key) -> TValue & {
    return emplace(std::move(key), TValue{}).first->second;
}

// endregion element access implementation

// region iterator begins implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::begin() -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    if (!_root) {
        return end();
    }
    typename btree_iterator::stack_type path;
    path.emplace(_root, 0);

    while (!is_leaf(path.top().first)) {
        path.emplace(path.top().first->_children.front(), 0);
    }

    return btree_iterator(_root, path);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::end() -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    return btree_iterator(_root);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::begin() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_iterator {
    if (!_root) {
        return end();
    }
    typename btree_const_iterator::stack_type path;
    path.emplace(_root, 0);

    while (!is_leaf(path.top().first)) {
        path.emplace(path.top().first->_children.front(), 0);
    }

    return btree_const_iterator(_root, path);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::end() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_iterator {
    return btree_const_iterator(_root);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::cbegin() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_iterator {
    return begin();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::cend() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_iterator {
    return end();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::rbegin() -> typename B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator {
    return btree_reverse_iterator(end());
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::rend() -> typename B_tree<TKey, TValue, Compare, t>::btree_reverse_iterator {
    return btree_reverse_iterator(begin());
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::rbegin() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_reverse_iterator {
    return btree_const_reverse_iterator(end());
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::rend() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_reverse_iterator {
    return btree_const_reverse_iterator(begin());
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::crbegin() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_reverse_iterator {
    return rbegin();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::crend() const -> typename B_tree<TKey, TValue, Compare, t>::btree_const_reverse_iterator {
    return rend();
}

// endregion iterator begins implementation

// region lookup implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::size() const noexcept -> std::size_t {
    return _size;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::empty() const noexcept -> bool {
    return _size == 0;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Self>
auto B_tree<TKey, TValue, Compare, t>::find(this Self &&self, const TKey &key)
    -> select_iterator_t<Self> {
    return self.bound_search(key, &find_lower_bound_at_node, true);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Self>
auto B_tree<TKey, TValue, Compare, t>::lower_bound(this Self &&self,
                                                   const TKey &key)
    -> select_iterator_t<Self> {
    return self.bound_search(key, &find_lower_bound_at_node, false);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename Self>
auto B_tree<TKey, TValue, Compare, t>::upper_bound(this Self &&self,
                                                   const TKey &key)
    -> select_iterator_t<Self> {
    return self.bound_search(key, &find_upper_bound_at_node, false);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::contains(const TKey &key) const -> bool {
    return find(key) != cend();
}

// endregion lookup implementation

// region modifiers implementation

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::clear() noexcept {
    delete_subtree(_root);

    _root = nullptr;
    _size = 0;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::insert(
    const tree_data_type &data) -> std::pair<typename B_tree<TKey, TValue, Compare, t>::btree_iterator, bool> {
    return emplace(data);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::insert(
    tree_data_type &&data) -> std::pair<typename B_tree<TKey, TValue, Compare, t>::btree_iterator, bool> {
    return emplace(std::move(data));
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename... Args>
    requires std::constructible_from<std::pair<TKey, TValue>, Args...>
auto
B_tree<TKey, TValue, Compare, t>::emplace(
    Args &&... args) -> std::pair<typename B_tree<TKey, TValue, Compare, t>::btree_iterator, bool> {
    tree_data_type data(std::forward<Args>(args)...);

    typename btree_iterator::stack_type path;

    if (!_root) {
        node_allocator node_alloc(_allocator);

        btree_node *new_node = node_allocator_traits::allocate(node_alloc, 1);

        try {
            node_allocator_traits::construct(node_alloc, new_node, data);

            path.emplace(new_node, 0);

            _root = new_node;
            ++_size;
            return {btree_iterator{_root, path}, true};
        } catch (...) {
            node_allocator_traits::deallocate(node_alloc, new_node, 1);
            throw;
        }
    }

    auto [index, is_exists] = find_position_for_insertion(data.first, path);
    btree_node *node = path.top().first;

    if (is_exists) {
        return {btree_iterator{_root, path, index}, false};
    }
    node->_keys.insert(node->_keys.begin() + index, data);
    ++_size;
    return {btree_iterator{_root, path, index}, true};
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::insert_or_assign(
    const tree_data_type &data) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    return emplace_or_assign(data);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::insert_or_assign(
    tree_data_type &&data) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    return emplace_or_assign(std::move(data));
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
template<typename... Args>
    requires std::constructible_from<std::pair<TKey, TValue>, Args...>
auto
B_tree<TKey, TValue, Compare, t>::emplace_or_assign(
    Args &&... args) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    tree_data_type data(std::forward<Args>(args)...);

    auto [it, is_complete] = emplace(data);

    if (!is_complete) {
        // TODO check this
        it->second = data.second;
    }
    return it;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare,
    t>::erase(btree_iterator pos) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    erase_key_at_node(pos._path, pos._index);
    --_size;
    return pos;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::erase(
    btree_const_iterator pos) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    erase_key_at_node(pos._path, pos._index);
    --_size;
    return pos;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::erase(btree_iterator beg,
                                        btree_iterator en) -> typename B_tree<TKey, TValue, Compare,
    t>::btree_iterator {
    btree_iterator it = beg;

    while (it != end() && it != en) {
        it = erase(it);
    }
    return it;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::erase(btree_const_iterator beg,
                                        btree_const_iterator en) -> typename B_tree<TKey, TValue, Compare,
    t>::btree_iterator {
    btree_const_iterator it = beg;

    while (it != cend() && it != en) {
        it = erase(it);
    }
    return it;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto
B_tree<TKey, TValue, Compare, t>::erase(const TKey &key) -> typename B_tree<TKey, TValue, Compare, t>::btree_iterator {
    typename btree_iterator::stack_type path;
    std::size_t index = 0;

    bool found = find_position_for_erasion(key, path, index);

    if (!found)
        return end();

    erase_key_at_node(path, index);

    --_size;
    return btree_iterator(_root, path, index);
}

// endregion modifiers implementation

// region helpers implementation

// TODO maybe make them fields on nodes
template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::is_leaf(const btree_node *node) noexcept
    -> bool {
    return node->_children.empty();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::is_node_full(
    const btree_node *node) noexcept -> bool {
    return node->_keys.size() == maximum_keys_in_node;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::is_node_small(
    const btree_node *node) noexcept -> bool {
    return node->_keys.size() == minimum_keys_in_node;
}

template<typename TKey, typename TValue, comparator<TKey> Compare, std::size_t t>
template<typename Self, typename BoundFunc>
auto B_tree<TKey, TValue, Compare, t>::bound_search(this Self &&self, const TKey &key, BoundFunc bound_func,
    bool exact_match) -> select_iterator_t<Self> {
    if (!self._root) {
        return self.end();
    }
    typename select_iterator_t<Self>::stack_type path;
    typename select_iterator_t<Self>::stack_type answer_path;

    bool found = false;
    std::size_t answer_index = 0;
    btree_node *node = self._root;

    // root has no parent, store 0 by convention
    path.emplace(node, 0);

    while (true) {
        std::size_t index = bound_func(node, key, self._compare);

        if (index < node->_keys.size()) {
            answer_path = path;
            answer_index = index;
            found = true;

            if (exact_match && self.is_equals_keys(node->_keys[index].first, key)) {
                break;
            }
        }
        if (is_leaf(node)) {
            break;
        }
        std::size_t child_index = index;
        node = node->_children[child_index];

        path.emplace(node, child_index);
    }
    if (!found) {
        return self.end();
    }
    return select_iterator_t<Self>{self._root, answer_path, answer_index};
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::find_lower_bound_at_node(
    const btree_node *node, const TKey &key, Compare compare) noexcept -> std::size_t {
    return std::lower_bound(
               node->_keys.begin(), node->_keys.end(), key,
               [&compare](const tree_data_type &node_data, const TKey &key) -> auto {
                   return compare(node_data.first, key);
               }) -
           node->_keys.begin();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::find_upper_bound_at_node(
    const btree_node *node, const TKey &key, Compare compare) noexcept -> std::size_t {
    return std::upper_bound(
               node->_keys.begin(), node->_keys.end(), key,
               [&compare](const TKey &key, const tree_data_type &node_data) -> auto {
                   return compare(key, node_data.first);
               }) -
           node->_keys.begin();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::find_position_for_insertion(
    const TKey &key, typename btree_iterator::stack_type &path)
    -> std::pair<std::size_t, bool> {
    if (is_node_full(_root)) {
        split(_root, nullptr, 0);
    }

    path.emplace(_root, 0);

    while (true) {
        btree_node *node = path.top().first;

        std::size_t index = find_lower_bound_at_node(node, key, _compare);
        if (index < node->_keys.size() &&
            is_equals_keys(node->_keys[index].first, key)) {
            // key already exists, return position and flag
            return {index, true};
        }
        if (is_leaf(node)) {
            // key not found, return position for insertion and flag
            return {index, false};
        }

        btree_node *child = node->_children[index];

        if (is_node_full(child)) {
            // rebalance on insertion

            // split node and update node to the correct child
            split(child, node, index);

            if (index < node->_keys.size() && is_equals_keys(node->_keys[index].first, key)) {
                // key already exists in parent after split, return position and flag
                return {index, true};
            }
            if (compare_keys(node->_keys[index].first, key)) {
                ++index;
            }
        }

        node = node->_children[index];

        path.emplace(node, index);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::find_position_for_erasion(
    const TKey &key, typename btree_iterator::stack_type &path,
    std::size_t &index) noexcept -> std::size_t {
    path.emplace(_root, 0);

    while (true) {
        btree_node *node = path.top().first;

        index = find_lower_bound_at_node(node, key, _compare);
        if (index < node->_keys.size() &&
            is_equals_keys(node->_keys[index].first, key)) {
            // key found
            return true;
        }
        if (is_leaf(node)) {
            // key not found
            return false;
        }

        btree_node *child = node->_children[index];

        if (is_node_small(child)) {
            index = rebalance_on_erasion(node, index);

            if (index < node->_keys.size() && is_equals_keys(node->_keys[index].first, key)) {
                // key already exists in parent after split, return position and flag
                return true;
            }
        }

        node = node->_children[index];

        path.emplace(node, index);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::erase_key_at_node(
    typename btree_iterator::stack_type &path, std::size_t &index) noexcept {
    auto [node, node_index] = path.top();

    if (is_leaf(node)) {
        auto it = node->_keys.erase(node->_keys.begin() + index);

        if (it == node->_keys.end() && !path.empty()) {
            // if we erased last key in node, move iterator to next key in order
            path.pop();

            while (!path.empty() && node_index + 1 >= node->_keys.size()) {
                std::tie(node, node_index) = path.top();
                path.pop();
            }
            index = node_index + 1;
            // return btree_iterator{_root, path, node_index + 1};
        }
        // return btree_iterator{_root, path, index};
        return;
    }

    btree_node *left_child = node->_children[index];
    btree_node *right_child = node->_children[index + 1];

    if (left_child->_keys.size() > minimum_keys_in_node) {
        path.emplace(left_child, index);

        btree_node *child = left_child;
        while (!is_leaf(child)) {
            child = child->_children.back();
            path.emplace(child, child->_children.size() - 1);
        }

        std::size_t key_index = child->_keys.size() - 1;
        node->_keys[index] = child->_keys[key_index];
        index = key_index;

        erase_key_at_node(path, index);
        return;
    }

    if (right_child->_keys.size() > minimum_keys_in_node) {
        path.emplace(right_child, index + 1);

        btree_node *child = right_child;
        while (!is_leaf(child)) {
            child = child->_children.front();
            path.emplace(child, 0);
        }

        std::size_t key_index = 0;
        node->_keys[index] = child->_keys[key_index];
        index = key_index;

        erase_key_at_node(path, index);
        return;
    }

    merge_right(node, index);

    btree_node *merged = (node == _root && node->_keys.empty()) ? _root : node->_children[index];
    path.pop();
    path.emplace(merged, node == _root ? 0 : index);

    std::size_t key_index = minimum_keys_in_node;
    erase_key_at_node(path, key_index);
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
std::size_t B_tree<TKey, TValue, Compare, t>::rebalance_on_erasion(
    btree_node *parent, std::size_t child_index) {
    if (child_index + 1 < parent->_children.size() &&
        !is_node_small(parent->_children[child_index + 1])) {
        rotate_left(parent, child_index);
        return child_index;
    }
    if (child_index > 0 &&
        !is_node_small(parent->_children[child_index - 1])) {
        rotate_right(parent, child_index);
        return child_index;
    }
    if (child_index > 0) {
        merge_left(parent, child_index);
        return child_index - 1;
    }
    merge_right(parent, child_index);
    return child_index;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::split(btree_node *child,
                                             btree_node *parent,
                                             std::size_t child_index) {
    std::size_t size = child->_keys.size();

    std::size_t new_size = size / 2; // t - 1

    tree_data_type data = child->_keys[new_size];

    // create new containers with elements after split
    keys_container new_keys(child->_keys.begin() + new_size + 1, child->_keys.end());
    children_container new_children;

    if (!is_leaf(child)) {
        new_children.insert(new_children.end(), child->_children.begin() + new_size + 1, child->_children.end());
    }

    node_allocator node_alloc(_allocator);

    btree_node *new_child = node_allocator_traits::allocate(node_alloc, 1);
    try {
        node_allocator_traits::construct(node_alloc, new_child, new_keys, new_children);
    } catch (...) {
        node_allocator_traits::deallocate(node_alloc, new_child, 1);
        throw;
    }

    if (parent) {
        // guaranteed that parent size is valid for insertion
        parent->_keys.insert(parent->_keys.begin() + child_index, data);
        parent->_children.insert(parent->_children.begin() + child_index + 1,
                                 new_child);
    } else {
        // create new root
        keys_container root_keys;
        root_keys.push_back(data);

        children_container root_children;
        root_children.push_back(child);
        root_children.push_back(new_child);

        btree_node *new_root = node_allocator_traits::allocate(node_alloc, 1);
        try {
            node_allocator_traits::construct(node_alloc, new_root, root_keys,
                                             root_children);
        } catch (...) {
            node_allocator_traits::deallocate(node_alloc, new_root, 1);

            node_allocator_traits::destroy(node_alloc, new_child);
            node_allocator_traits::deallocate(node_alloc, new_child, 1);
            throw;
        }
        _root = new_root;
    }

    child->_keys.resize(new_size);

    if (!is_leaf(child)) {
        child->_children.resize(new_size + 1);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::merge_left(btree_node *parent,
                                                  std::size_t child_index) {
    btree_node *child = parent->_children[child_index];
    btree_node *left_child = parent->_children[child_index - 1];

    tree_data_type parent_data = parent->_keys[child_index - 1];

    left_child->_keys.push_back(parent_data);

    left_child->_keys.insert(left_child->_keys.end(), child->_keys.begin(),
                             child->_keys.end());
    left_child->_children.insert(left_child->_children.end(),
                                 child->_children.begin(),
                                 child->_children.end());

    parent->_keys.erase(parent->_keys.begin() + child_index - 1);
    parent->_children.erase(parent->_children.begin() + child_index);

    node_allocator node_alloc(_allocator);
    node_allocator_traits::destroy(node_alloc, child);
    node_allocator_traits::deallocate(node_alloc, child, 1);

    // shrink root if needed
    if (parent == _root && parent->_keys.empty()) {
        _root = left_child;

        node_allocator_traits::destroy(node_alloc, parent);
        node_allocator_traits::deallocate(node_alloc, parent, 1);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::merge_right(btree_node *parent,
                                                   std::size_t child_index) {
    btree_node *child = parent->_children[child_index];
    btree_node *right_child = parent->_children[child_index + 1];

    tree_data_type parent_data = parent->_keys[child_index];

    child->_keys.push_back(parent_data);

    child->_keys.insert(child->_keys.end(), right_child->_keys.begin(),
                        right_child->_keys.end());
    child->_children.insert(child->_children.end(),
                            right_child->_children.begin(),
                            right_child->_children.end());

    parent->_keys.erase(parent->_keys.begin() + child_index);
    parent->_children.erase(parent->_children.begin() + child_index + 1);

    node_allocator node_alloc(_allocator);
    node_allocator_traits::destroy(node_alloc, right_child);
    node_allocator_traits::deallocate(node_alloc, right_child, 1);

    // shrink root if needed
    if (parent == _root && parent->_keys.empty()) {
        _root = child;

        node_allocator_traits::destroy(node_alloc, parent);
        node_allocator_traits::deallocate(node_alloc, parent, 1);
    }
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::rotate_left(btree_node *parent,
                                                   std::size_t child_index) {
    btree_node *child = parent->_children[child_index];
    btree_node *right_child = parent->_children[child_index + 1];

    tree_data_type parent_data = parent->_keys[child_index];

    tree_data_type data = right_child->_keys.front();
    btree_node *subtree = nullptr;

    if (!is_leaf(right_child)) {
        subtree = right_child->_children.front();
        right_child->_children.erase(right_child->_children.begin());
    }

    right_child->_keys.erase(right_child->_keys.begin());

    child->_keys.push_back(parent_data);

    if (subtree) child->_children.push_back(subtree);

    parent->_keys[child_index] = data;
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::rotate_right(btree_node *parent,
                                                    std::size_t child_index) {
    btree_node *child = parent->_children[child_index];
    btree_node *left_child = parent->_children[child_index - 1];

    tree_data_type parent_data = parent->_keys[child_index - 1];

    tree_data_type data = left_child->_keys.back();
    btree_node *subtree = nullptr;

    if (!is_leaf(left_child)) {
        subtree = left_child->_children.back();
        left_child->_children.pop_back();
    }

    left_child->_keys.pop_back();

    child->_keys.insert(child->_keys.begin(), parent_data);

    if (subtree) child->_children.insert(child->_children.begin(), subtree);

    parent->_keys[child_index - 1] = data;
}

template<typename TKey, typename TValue, comparator<TKey> Compare, std::size_t t>
auto B_tree<TKey, TValue, Compare, t>::deep_copy_tree(const btree_node *node) const -> typename B_tree<TKey, TValue, Compare, t>::btree_node * {
    if (!node) return nullptr;

    node_allocator node_alloc(_allocator);

    btree_node *raw = node_allocator_traits::allocate(node_alloc, 1);
    try {
        node_allocator_traits::construct(node_alloc, raw, node->_keys);
    } catch (...) {
        node_allocator_traits::deallocate(node_alloc, raw, 1);
        throw;
    }

    struct subtree_deleter {
        const B_tree *self;
        void operator()(btree_node *ptr) const noexcept {
            if (ptr) {
                self->delete_subtree(ptr);
            }
        }
    };

    auto guard = std::unique_ptr<btree_node, subtree_deleter>(
        raw, subtree_deleter{this});

    for (const btree_node *child: node->_children) {
        raw->_children.push_back(deep_copy_tree(child));
    }
    return guard.release();
}

template<typename TKey, typename TValue, comparator<TKey> Compare,
    std::size_t t>
void B_tree<TKey, TValue, Compare, t>::delete_subtree(
    btree_node *node) noexcept {
    if (!node)
        return;

    if (!is_leaf(node)) {
        for (btree_node *child: node->_children) {
            delete_subtree(child);
        }
    }
    node_allocator node_alloc(_allocator);
    node_allocator_traits::destroy(node_alloc, node);
    node_allocator_traits::deallocate(node_alloc, node, 1);
}

// endregion helpers implementation


#endif
