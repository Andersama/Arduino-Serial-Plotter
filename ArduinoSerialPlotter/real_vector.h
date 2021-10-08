#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace real {
template <typename Pointer> struct allocation_result {
    Pointer ptr = {};
    size_t count = {};
};

namespace details {
template <typename T> constexpr void destroy_at(T *const ptr) {
    if constexpr (!std::is_trivially_destructible_v<T>)
        ptr->~T();
}

template <typename Iterator> constexpr void destroy(Iterator first, Iterator last) {
    std::destroy(first, last);
    /*
    using iterator_traits = std::iterator_traits<Iterator>;
    if constexpr (!std::is_trivially_destructible_v<iterator_traits::value_type>) {
        for (; first != last; ++first)
            destroy_at(&(*first)); //::std::addressof(*first)
    }
    */
}

// ripped from llvm libcxx
struct default_init_tag {};
struct value_init_tag {};
struct zero_then_variadic_args_t {};
struct one_then_variadic_args_t {};

// propagate on container move assignment
template <typename Alloc> constexpr void pocma(Alloc &left, Alloc &right) noexcept {
    if constexpr (::std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value) {
        left = ::std::move(right);
    }
}

template <typename Alloc> constexpr void pocca(Alloc &left, Alloc &right) noexcept {
    if constexpr (::std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value) {
        left = right;
    }
}

template <typename Alloc> constexpr bool should_pocma(Alloc &) noexcept {
    return ::std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value;
}

template <typename Alloc> constexpr bool should_pocca(Alloc &) noexcept {
    return ::std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value;
}

template <typename T, bool> struct dependent_type : public T {};

// can optimize Ty1 away (empty base class optimization)
template <typename Ty1, typename Ty2, bool emptyBaseClass = ::std::is_empty_v<Ty1> && !::std::is_final_v<Ty1>>
struct compressed_pair final : private Ty1 {
    Ty2 _value2;

    template <class... Args1, class... Args2, size_t... Idxs1, size_t... Idxs2>
    constexpr explicit compressed_pair(std::tuple<Args1...> first_args, std::tuple<Args2...> second_args,
                                       std::index_sequence<Idxs1...>, std::index_sequence<Idxs2...>)
        : Ty1(::std::forward<Args1>(::std::get<Idxs1>(first_args))...),
          _value2(::std::forward<Args2>(::std::get<Idxs2>(second_args))...) {}

    template <class... Args1, class... Args2>
    constexpr explicit compressed_pair(::std::piecewise_construct_t t, std::tuple<Args1...> first_args,
                                       std::tuple<Args2...> second_args)
        : compressed_pair(first_args, second_args, std::make_index_sequence<sizeof...(Args1)>(),
                          std::make_index_sequence<sizeof...(Args2)>()) {}

    template <class... Args>
    constexpr explicit compressed_pair(zero_then_variadic_args_t, Args &&...args) noexcept(
        ::std::conjunction_v<::std::is_nothrow_default_constructible<Ty1>,
                             ::std::is_nothrow_constructible<Ty2, Args...>>)
        : Ty1(), _value2(::std::forward<Args>(args)...){};

    template <class Arg, class... Args>
    constexpr compressed_pair(one_then_variadic_args_t, Arg &&arg, Args &&...args) noexcept(
        ::std::conjunction_v<::std::is_nothrow_default_constructible<Ty1>,
                             ::std::is_nothrow_constructible<Ty2, Args...>>)
        : Ty1(::std::forward<Arg>(arg)), _value2(::std::forward<Args>(args)...){};

    constexpr Ty1 &first() noexcept { return *this; }
    constexpr const Ty1 &first() const noexcept { return *this; }
    constexpr Ty2 &second() noexcept { return _value2; }
    constexpr const Ty2 &second() const noexcept { return _value2; }
};

template <typename Ty1, typename Ty2> struct compressed_pair<Ty1, Ty2, false> final {
    Ty1 _value1;
    Ty2 _value2;

    template <class... Args1, class... Args2, size_t... Idxs1, size_t... Idxs2>
    constexpr explicit compressed_pair(std::tuple<Args1...> first_args, std::tuple<Args2...> second_args,
                                       std::index_sequence<Idxs1...>, std::index_sequence<Idxs2...>)
        : _value1(::std::forward<Args1>(::std::get<Idxs1>(first_args))...),
          _value2(::std::forward<Args2>(::std::get<Idxs2>(second_args))...) {}

    template <class... Args1, class... Args2>
    constexpr explicit compressed_pair(::std::piecewise_construct_t t, std::tuple<Args1...> first_args,
                                       std::tuple<Args2...> second_args)
        : compressed_pair(first_args, second_args, std::make_index_sequence<sizeof...(Args1)>(),
                          std::make_index_sequence<sizeof...(Args2)>()) {}

    template <class... Args>
    constexpr explicit compressed_pair(zero_then_variadic_args_t, Args &&...args) noexcept(
        ::std::conjunction_v<::std::is_nothrow_default_constructible<Ty1>,
                             ::std::is_nothrow_constructible<Ty2, Args...>>)
        : _value1(), _value2(::std::forward<Args>(args)...){};

    template <class Arg, class... Args>
    constexpr compressed_pair(one_then_variadic_args_t, Arg &&arg, Args &&...args) noexcept(
        ::std::conjunction_v<::std::is_nothrow_default_constructible<Ty1>,
                             ::std::is_nothrow_constructible<Ty2, Args...>>)
        : _value1(::std::forward<Arg>(arg)), _value2(::std::forward<Args>(args)...){};

    constexpr Ty1 &first() noexcept { return _value1; }
    constexpr const Ty1 &first() const noexcept { return _value1; }
    constexpr Ty2 &second() noexcept { return _value2; }
    constexpr const Ty2 &second() const noexcept { return _value2; }
};

template <typename Ty1, typename Ty2> struct easy_pair final {
    Ty1 _value1;
    Ty2 _value2;

    constexpr Ty1 &first() noexcept { return _value1; }
    constexpr const Ty1 &first() const noexcept { return _value1; }
    constexpr Ty2 &second() noexcept { return _value2; }
    constexpr const Ty2 &second() const noexcept { return _value2; }
};
} // namespace details

struct default_expansion_policy {
    [[nodiscard]] constexpr size_t grow_capacity(size_t size, size_t capacity, size_t required_capacity) noexcept {
        return required_capacity;
    }
};

template <size_t N> struct geometric_int_expansion_policy {
    [[nodiscard]] constexpr size_t grow_capacity(size_t size, size_t capacity, size_t required_capacity) noexcept {
        const size_t expanded_capacity = (capacity ? capacity : 1) * N;
        return (expanded_capacity < required_capacity ? required_capacity : expanded_capacity);
    }
};

template <double N> struct geometric_double_expansion_policy {
    [[nodiscard]] constexpr size_t grow_capacity(size_t size, size_t capacity, size_t required_capacity) noexcept {
        const size_t expanded_capacity = (capacity ? capacity : 1) * N;
        return (expanded_capacity < required_capacity ? required_capacity : expanded_capacity);
    }
};

template <typename T, typename Allocator = std::allocator<T>> class vector {
  public:
    using element_type = T;
    using value_type = typename ::std::remove_cv<T>::type;
    using const_reference = const value_type &;
    using size_type = ::std::size_t;
    using difference_type = ::std::ptrdiff_t;
    using pointer = element_type *;
    using const_pointer = const element_type *;
    using reference = element_type &;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = ::std::reverse_iterator<iterator>;
    using const_reverse_iterator = ::std::reverse_iterator<const_iterator>;
    using allocator_type = Allocator;

    using rebind_allocator_type = typename ::std::allocator_traits<allocator_type>::template rebind_alloc<value_type>;

  private: // data members
    T *_begin = {};
    T *_end = {};
    /* LLVM and MSVC make these a "compressed_pair"
    size_t    _capacity = {};
    //because we don't need this as a member (but it could have members of its own)
    Allocator _alloc    = {};
    */
    details::compressed_pair<Allocator, size_t> _capacity_allocator;

  private:
    constexpr void _cleanup() noexcept {
        // orphan iterators?
        if (_begin) {
            details::destroy(_begin, _end);
            get_allocator().deallocate(_begin, capacity());
            _begin = nullptr;
            _end = nullptr;
            _capacity_allocator.second() = 0ULL;
        }
    }

  public:
    constexpr ~vector() noexcept { _cleanup(); }

  private:
    template <typename Iterator, typename ExpansionPolicy>
    constexpr iterator insert_range(const_iterator pos, Iterator first, Iterator last, ExpansionPolicy) {
        size_type insert_idx = pos - cbegin();
        iterator ret_it = begin() + insert_idx;

        // insert input range [first, last) at _Where
        if (first == last) {
            return ret_it; // nothing to do
        }

        assert(pos >= cbegin() && pos <= cend() && "insert iterator is out of bounds");
        const size_type old_size = size();

        if constexpr (::std::is_same<::std::random_access_iterator_tag,
                                     ::std::iterator_traits<Iterator>::iterator_category>::value) {
            size_type insert_count = last - first;
            if (!can_store(insert_count)) {
                size_t target_capacity = ExpansionPolicy{}.grow_capacity(old_size, _capacity_allocator.second(),
                                                                         _capacity_allocator.second() + insert_count);
                reserve(target_capacity);
            }
            // already safe from check above*
            ::std::uninitialized_copy(first, last, end());
            _end += insert_count;
            //_size += insert_count;
        } else {
            // bounds check each emplace_back, saturating
            for (; first != last && size() < capacity(); ++first) {
                unchecked_emplace_back(*first);
            }
            for (; first != last; ++first) {
                emplace_back(*first);
            }
        }

        ::std::rotate(begin() + insert_idx, begin() + old_size, end());
        return ret_it;
    }

  public:
    [[nodiscard]] constexpr allocator_type get_allocator() const noexcept {
        return static_cast<allocator_type>(_capacity_allocator.first());
    }

    constexpr vector() noexcept(::std::is_nothrow_default_constructible_v<rebind_allocator_type>)
        : _capacity_allocator(details::zero_then_variadic_args_t{}) {}

    constexpr explicit vector(const Allocator &alloc) noexcept
        : _capacity_allocator(details::one_then_variadic_args_t{}, alloc) {}

    constexpr vector(size_type count, const value_type &value)
        : _capacity_allocator(details::zero_then_variadic_args_t{}) {
        if (count) {
            cleared_reserve(count);
            ::std::uninitialized_fill(_begin, _begin + count, value);
            _end = _begin + count;
        }
    }

    constexpr explicit vector(size_type count, const value_type &value, const allocator_type &alloc)
        : _capacity_allocator(details::one_then_variadic_args_t{}, alloc) {
        if (count) {
            cleared_reserve(count);
            ::std::uninitialized_fill(_begin, _begin + count, value);
            _end = _begin + count;
        }
    }

    template <class Iterator>
    constexpr vector(Iterator first, Iterator last) : _capacity_allocator(details::zero_then_variadic_args_t{}) {
        size_type count = std::distance(first, last);
        if (count) {
            cleared_reserve(count);
            ::std::uninitialized_copy(first, last, _begin);
            _end = _begin + count;
        }
    }

    constexpr vector(const vector &other)
        : _capacity_allocator(details::one_then_variadic_args_t{},
                              ::std::allocator_traits<allocator_type>::select_on_container_copy_construction(
                                  other._capacity_allocator.first())) {
        size_t count = other.size();
        if (count) {
            cleared_reserve(count);
            ::std::uninitialized_copy(other.begin(), other.end(), _begin);
            _end = _begin + count;
        }
    }

    constexpr vector(const vector &other, const allocator_type &alloc)
        : _capacity_allocator(details::one_then_variadic_args_t{}, alloc) {
        if (!other.empty())
            this->operator=(other);
    }

    constexpr vector(vector &&other) : _capacity_allocator(details::zero_then_variadic_args_t{}) {
        if (!other.empty())
            this->operator=(::std::move(other));
    }

    constexpr void set_vector(const pointer data, const size_type new_size, const size_type new_capacity) {
        T *old_begin = _begin;
        T *old_end = _end;
        size_t old_capacity = _capacity_allocator.second();
        size_t required_capacity = std::max(new_size, new_capacity);

        if (_begin) {
            details::destroy(old_begin, old_end);
            get_allocator().deallocate(_begin, static_cast<size_type>(_end - _begin));
        }

        _begin = data;
        _end = data + new_size;
        _capacity_allocator.second() = required_capacity;
    }
    // note: like shrink_to_fit
    constexpr void unchecked_reserve(size_type new_capacity) {
        T *old_begin = _begin;
        T *old_end = _end;
        size_t old_size = static_cast<size_type>(old_end - old_begin);
        size_t old_capacity = _capacity_allocator.second();
        size_t required_capacity = std::max(old_size, new_capacity);

        const pointer newdata = get_allocator().allocate(required_capacity);

        try {
            // move data over
            ::std::uninitialized_copy(::std::make_move_iterator(old_begin), ::std::make_move_iterator(old_end),
                                      newdata);
        } catch (...) {
            get_allocator().deallocate(newdata, required_capacity);
            throw;
        }

        if (old_begin) {
            // already moved, delete
            get_allocator().deallocate(old_begin, old_capacity);
        }

        _begin = newdata;
        _end = newdata + old_size;
        _capacity_allocator.second() = required_capacity;
    }

    constexpr void reserve(size_type new_capacity) {
        T *old_begin = _begin;
        T *old_end = _end;
        size_t old_size = static_cast<size_type>(old_end - old_begin);
        size_t old_capacity = _capacity_allocator.second();
        // size_t required_capacity = std::max(old_size, new_capacity);
        if (old_capacity < new_capacity) {
            if (new_capacity > max_size()) {
                throw std::length_error("cannot allocate larger than max_size");
            }

            const pointer newdata = get_allocator().allocate(new_capacity);
            try {
                // copy data over
                ::std::uninitialized_copy(std::make_move_iterator(old_begin), std::make_move_iterator(old_end),
                                          newdata);
            } catch (...) {
                get_allocator().deallocate(newdata, new_capacity);
                throw;
            }

            if (old_begin) {
                // already moved, delete
                get_allocator().deallocate(old_begin, old_capacity);
            }

            _begin = newdata;
            _end = newdata + old_size;
            _capacity_allocator.second() = new_capacity;
        }
    }
    // note: use only after clear();
    constexpr void cleared_reserve(size_type new_capacity) {
        const pointer newdata = get_allocator().allocate(new_capacity);
        if (_begin) {
            details::destroy(_begin, _end);
            get_allocator().deallocate(_begin, capacity());
        }
        _begin = newdata;
        _end = newdata;
        _capacity_allocator.second() = new_capacity;
    }
    //[]'s
    [[nodiscard]] constexpr reference operator[](size_type pos) {
        assert(pos < size());
        return _begin[pos];
    };
    [[nodiscard]] constexpr const_reference operator[](size_type pos) const {
        assert(pos < size());
        return _begin[pos];
    };
    // front
    [[nodiscard]] constexpr reference front() {
        assert(!empty());
        return _begin[0];
    };
    [[nodiscard]] constexpr const_reference front() const {
        assert(!empty());
        return _begin[0];
    };
    // back's
    [[nodiscard]] constexpr reference back() {
        assert(!empty());
        return _begin[static_cast<size_type>(_end - _begin) - 1];
    };
    [[nodiscard]] constexpr const_reference back() const {
        assert(!empty());
        return _begin[static_cast<size_type>(_end - _begin) - 1];
    };
    // data's
    [[nodiscard]] constexpr T *data() noexcept { return _begin; };
    [[nodiscard]] constexpr const T *data() const noexcept { return _begin; };
    // at's
    [[nodiscard]] constexpr reference at(size_type pos) {
        if (!(pos < size()))
            throw std::out_of_range("accessing index out of range of vector");
        return _begin[pos];
    };
    [[nodiscard]] constexpr const_reference at(size_type pos) const {
        if (!(pos < size()))
            throw std::out_of_range("accessing index out of range of vector");
        return _begin[pos];
    };

    // begin's
    [[nodiscard]] constexpr iterator begin() noexcept { return _begin; };
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return _begin; };
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return _begin; };
    // rbegin's
    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); };
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); };
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); };
    // end's
    [[nodiscard]] constexpr iterator end() noexcept { return &_begin[0] + size(); };
    [[nodiscard]] constexpr const_iterator end() const noexcept { return &_begin[0] + size(); };
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return &_begin[0] + size(); };
    // rend's
    [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); };
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); };
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); };
    // empty's
    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; };
    // full (non standard)
    [[nodiscard]] constexpr bool full() const noexcept { return size() >= capacity(); };
    [[nodiscard]] constexpr bool initialized() const noexcept { return _begin && _capacity_allocator.second(); };
    [[nodiscard]] constexpr bool uncorrupted() const noexcept {
        return (_begin == nullptr && _end == nullptr && _capacity_allocator.second() == 0) ||
               (_begin && _end >= _begin && (_end <= (_begin + _capacity_allocator.second())));
    }
    // can_store (non standard)
    [[nodiscard]] constexpr bool can_store(size_t count) const noexcept { return (capacity() - size()) >= count; }
    // size
    constexpr size_type size() const noexcept { return static_cast<size_type>(_end - _begin); };
    // capacity
    constexpr size_type capacity() const noexcept { return _capacity_allocator.second(); };
    // max_size (constant)
    constexpr size_type max_size() const noexcept {
        constexpr size_type system_max_size = ((~size_type{0}) / sizeof(T));
        const size_type allocator_max_size = std::allocator_traits<allocator_type>::max_size(get_allocator());
        const size_type result = (system_max_size < allocator_max_size) ? system_max_size : allocator_max_size;
        return result;
    };
    // emplace_back's
    template <class... Args> constexpr reference emplace_back(Args &&...args) {
        if (full()) {
            size_t target_capacity = geometric_int_expansion_policy<2>{}.grow_capacity(
                size(), _capacity_allocator.second(), _capacity_allocator.second() + 1);
            reserve(target_capacity);
        }
        iterator it = _end;
        _end += 1;
        ::new ((void *)it) value_type(::std::forward<Args>(args)...);
        return *it;
    };
    // emplace_back_with_policy
    template <typename... Args, typename ExpansionPolicy>
    constexpr reference emplace_back_with_policy(Args &&...args, ExpansionPolicy) {
        if (full()) {
            size_t target_capacity =
                ExpansionPolicy{}.grow_capacity(size(), _capacity_allocator.second(), _capacity_allocator.second() + 1);
            reserve(target_capacity);
        }
        iterator it = _end;
        _end += 1;
        ::new ((void *)it) T(::std::forward<Args>(args)...);
        return *it;
    }

    // unechecked_emplace_back (non-standard)
    template <typename... Args> constexpr reference unchecked_emplace_back(Args &&...args) {
        iterator it = _end;
        _end += 1;
        ::new ((void *)it) T(::std::forward<Args>(args)...);
        /*
        ::std::allocator_traits<allocator_type>::construct(_capacity_allocator.first(), ::std::to_address(it),
                                                           std::forward<Args>(args)...);
        */
        return *it;
    };
    // push_back's
    constexpr void push_back(const T &value) { emplace_back(::std::forward<const T &>(value)); }
    constexpr void push_back(T &&value) { emplace_back(::std::forward<T &&>(value)); };
    // push_back_with_policy
    template <typename ExpansionPolicy = geometric_int_expansion_policy<2>>
    constexpr void push_back_with_policy(const T &value) {
        emplace_back_with_policy(::std::forward<const T &>(value), ExpansionPolicy{});
    }

    template <typename ExpansionPolicy = geometric_int_expansion_policy<2>>
    constexpr void push_back_with_policy(T &&value) {
        emplace_back_with_policy(::std::forward<T &&>(value), ExpansionPolicy{});
    }
    // pop_back's
    constexpr void pop_back() {
        if (size()) [[likely]] {
            if constexpr (::std::is_trivially_destructible<element_type>::value) {
                _end -= 1;
            } else {
                _end -= 1;
                end()->~element_type(); // destroy the tailing value
            }
        } else {
            // error ?
        }
    };
    // clear
    constexpr void clear() noexcept {
        if constexpr (!::std::is_trivially_constructible<element_type>::value) {
            details::destroy(_begin, _end);
        }
        _end = _begin;
    }
    // insert's
    constexpr iterator insert(const_iterator pos, const T &value) { return emplace(pos, value); }
    constexpr iterator insert(const_iterator pos, T &&value) { return emplace(pos, ::std::move(value)); }
    constexpr iterator insert(const_iterator pos, size_type count, const T &value) {
        if (count) {
            size_type remaining_capacity = capacity() - size();
            // const bool     one_at_back        = (remaining_capacity == 1) && (pos == cend());
            const size_type insert_idx = cend() - pos;
            iterator insert_point = begin() + (cend() - pos);

            if (count > remaining_capacity) {
                // reserve(size()+count);
                const size_type new_capacity = size() + count;
                pointer newdata = get_allocator().allocate(new_capacity);
                try {
                    ::std::uninitialized_copy(::std::make_move_iterator(begin()),
                                              ::std::make_move_iterator(insert_point), newdata);
                    ::std::uninitialized_fill(newdata + insert_point, newdata + insert_point + count, value);
                    ::std::uninitialized_copy(::std::make_move_iterator(insert_point), ::std::make_move_iterator(end()),
                                              newdata + insert_point + count);
                } catch (...) {
                    get_allocator().deallocate(newdata, new_capacity);
                    throw;
                }

                if (_begin) {
                    // already moved, delete
                    get_allocator().deallocate(_begin, capacity());
                }

                _begin = newdata;
                _end = newdata + new_capacity;
                _capacity_allocator.second() = new_capacity;

            } else {
                iterator start = end();
                iterator last = start;
                iterator target = start + count;
                // uninitialized_move_backward
                for (; target != last; target--, start--) {
                    ::std::allocator_traits<allocator_type>::construct(_capacity_allocator.first(),
                                                                       ::std::to_address(target), ::std::move(*start));
                }
                ::std::uninitialized_fill(pos, pos + count, value);
                _end = target;
            }
        }
    }
    template <class InputIt> constexpr iterator insert(const_iterator pos, InputIt first, InputIt last) {
        return insert_range(pos, first, last);
    };
    constexpr iterator insert(const_iterator pos, ::std::initializer_list<T> ilist) {
        return insert(pos, ilist.begin(), ilist.end());
    };
    // emplace's
    template <class... Args> constexpr iterator emplace(const_iterator pos, Args &&...args) {
        size_type insert_idx = pos - cbegin();
        assert(pos >= cbegin() && pos <= cend() && "emplace iterator does not refer to this vector");
        if (size() < capacity()) {
            if (pos == cend()) {
                unchecked_emplace_back(::std::forward<Args>(args)...);
            } else {
                iterator start = end();
                iterator last = start;
                iterator target = start + 1;
                // uninitialized_move_backward
                for (; target != last; target--, start--) {
                    ::std::allocator_traits<allocator_type>::construct(_capacity_allocator.first(),
                                                                       ::std::to_address(target), ::std::move(*start));
                }
                ::std::allocator_traits<allocator_type>::construct(_capacity_allocator.first(), ::std::to_address(pos),
                                                                   std::forward<Args>(args)...);
                _end = target;
            }
        } else {
            // emplace_back(std::forward<Args>(args)...);
            if (pos == cend()) {
                emplace_back(::std::forward<Args>(args)...);
            } else {
                size_type new_capacity =
                    geometric_int_expansion_policy<2>{}.grow_capacity(size(), capacity(), capacity() + 1);
                const pointer newdata = _capacity_allocator.first().allocate(new_capacity);
                try {
                    ::std::allocator_traits<allocator_type>::construct(
                        _capacity_allocator.first(), newdata + insert_idx, std::forward<Args>(args)...);
                    ::std::uninitialized_copy(::std::make_move_iterator(begin()),
                                              ::std::make_move_iterator(begin() + insert_idx), newdata);

                    ::std::uninitialized_copy(::std::make_move_iterator(begin() + insert_idx),
                                              ::std::make_move_iterator(end()), newdata + insert_idx + 1);
                } catch (...) {
                    details::destroy(newdata, newdata + insert_idx + 1);
                    _capacity_allocator.first().deallocate(newdata, new_capacity);
                }

                if (_begin) {
                    _capacity_allocator.first().deallocate(_begin, capacity());
                }
                size_type old_size = size();
                _begin = newdata;
                _end = newdata + old_size + 1;
                _capacity_allocator.second() = new_capacity;
            }
        }
        return begin() + insert_idx;
    }

    // erase's
    constexpr iterator erase(const_iterator pos) noexcept(::std::is_nothrow_move_assignable_v<value_type>) {
        size_type erase_idx = pos - cbegin();

        assert(pos >= cbegin() && pos < cend() && "erase iterator is out of bounds of the vector");
        // move on top
        iterator first = begin() + erase_idx + 1;
        iterator last = end();
        iterator dest = begin() + erase_idx;
        for (; first != last; ++dest, (void)++first) {
            *dest = ::std::move(*first);
        }
        details::destroy_at(end() - 1);
        _end -= 1;
        return begin() + erase_idx;
    }
    constexpr iterator erase(const_iterator first,
                             const_iterator last) noexcept(::std::is_nothrow_move_assignable_v<value_type>) {
        size_type erase_idx = first - cbegin();

        assert(first >= cbegin() && first <= cend() && "first erase iterator is out of bounds of the vector");
        assert(last >= cbegin() && last <= cend() && "last erase iterator is out of bounds of the vector");

        if (first != last) {
            size_type erase_count = last - first;
            iterator _first = begin() + (erase_idx + erase_count);
            iterator _last = end();
            iterator dest = begin() + erase_idx;
            for (; _first != _last; ++dest, (void)++_first) {
                *dest = ::std::move(*_first);
            }
            details::destroy(end() - erase_count, end());
            _end -= erase_count;
        }
        return begin() + erase_idx;
    }

    // assign's
    constexpr void assign(size_type count, const T &value) {
        clear();
        if (count > capacity())
            cleared_reserve(count);
        ::std::uninitialized_fill(_begin, _begin + count, value);
        _end = _begin + count;
    };

    template <typename Iterator> constexpr void assign(Iterator first, Iterator last) {
        if constexpr (::std::is_same<::std::random_access_iterator_tag,
                                     ::std::iterator_traits<Iterator>::iterator_category>::value) {
            size_type count = static_cast<size_type>(last - first);
            clear();
            if (count > capacity())
                cleared_reserve(count);
            ::std::uninitialized_copy(first, last, end());
            _end = _begin + count;
        } else {
            size_type count = std::distance(first, last);
            clear();
            if (count > capacity())
                cleared_reserve(count);
            for (; first != last; ++first) {
                unchecked_emplace_back(*first);
            }
        }
    }
    constexpr void assign(::std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); };

    constexpr vector &operator=(vector &&other) noexcept(
        ::std::is_nothrow_move_assignable<details::compressed_pair<Allocator, size_t>>::value) {
        if (this != &other) {
            _cleanup();
            details::pocma(_capacity_allocator.first(), other._capacity_allocator.first());
            _begin = ::std::move(other._begin);
            _end = ::std::move(other._end);
            _capacity_allocator.second() = ::std::move(other._capacity_allocator.second());
        }
        return *this;
    }

    constexpr vector &operator=(const vector &other) {
        if (this != &other) {
            if constexpr (details::should_pocca(_capacity_allocator.first())) {
                if (!::std::allocator_traits<Allocator>::is_always_equal::value &&
                    _capacity_allocator.first() != other._capacity_allocator.first()) {
                    _cleanup();
                }
                details::pocca(_capacity_allocator.first(), other._capacity_allocator.first());
                assign(other.begin(), other.end());
            } else {
                assign(other.begin(), other.end());
            }
        }
        return *this;
    }

    constexpr vector &operator=(::std::initializer_list<T> ilist) {
        assign(ilist.begin(), ilist.end());
        return *this;
    }

    constexpr void shrink_to_fit() {
        if (size() != capacity()) {
            if (_begin == _end) {
                _cleanup();
            } else {
                unchecked_reserve(size());
            }
        }
    }
};

} // namespace real

namespace pmr {
namespace real {
template <class T> using vector = ::real::vector<T, ::std::pmr::polymorphic_allocator<T>>;
};
} // namespace pmr