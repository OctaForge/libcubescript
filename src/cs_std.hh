#ifndef LIBCUBESCRIPT_STD_HH
#define LIBCUBESCRIPT_STD_HH

#include <cstddef>
#include <utility>
#include <type_traits>

#include "cs_state.hh"

namespace cscript {

/* run func, call the second one after finishing */

template<typename F>
struct CsScopeExit {
    template<typename FF>
    CsScopeExit(FF &&f): func(std::forward<FF>(f)) {}
    ~CsScopeExit() {
        func();
    }
    std::decay_t<F> func;
};

template<typename F1, typename F2>
inline void call_with_cleanup(F1 &&dof, F2 &&clf) {
    CsScopeExit<F2> cleanup(std::forward<F2>(clf));
    dof();
}

/* a simple static array with elements constructed using ctor args */

template<typename T, std::size_t N>
struct cs_valarray {
    template<typename ...A>
    cs_valarray(A &&...args) {
        for (std::size_t i = 0; i < N; ++i) {
            new (&stor[i]) T{std::forward<A>(args)...};
        }
    }

    ~cs_valarray() {
        for (std::size_t i = 0; i < N; ++i) {
            reinterpret_cast<T *>(&stor[i])->~T();
        }
    }

    T &operator[](std::size_t i) {
        return *reinterpret_cast<T *>(&stor[i]);
    }

    std::aligned_storage_t<sizeof(T), alignof(T)> stor[N];
};

/* a value buffer */

template<typename T>
struct cs_valbuf {
    cs_valbuf() = delete;

    cs_valbuf(cs_shared_state *cs): buf{cs_allocator<T>{cs}} {}
    cs_valbuf(cs_state &cs): buf{cs_allocator<T>{cs}} {}

    using size_type = std::size_t;
    using value_type = T;
    using reference = T &;
    using const_reference = T const &;

    void reserve(std::size_t s) { buf.reserve(s); }
    void resize(std::size_t s) { buf.resize(s); }

    void append(T const *beg, T const *end) {
        buf.insert(buf.end(), beg, end);
    }

    void push_back(T const &v) { buf.push_back(v); }
    void pop_back() { buf.pop_back(); }

    T &back() { return buf.back(); }
    T const &back() const { return buf.back(); }

    std::size_t size() const { return buf.size(); }
    std::size_t capacity() const { return buf.capacity(); }

    bool empty() const { return buf.empty(); }

    void clear() { buf.clear(); }

    T &operator[](std::size_t i) { return buf[i]; }
    T const &operator[](std::size_t i) const { return buf[i]; }

    T *data() { return &buf[0]; }
    T const *data() const { return &buf[0]; }

    std::vector<T, cs_allocator<T>> buf;
};

/* specialization of value buffer for bytes */

struct cs_charbuf: cs_valbuf<char> {
    cs_charbuf(cs_shared_state *cs): cs_valbuf<char>{cs} {}
    cs_charbuf(cs_state &cs): cs_valbuf<char>{cs} {}

    void append(char const *beg, char const *end) {
        cs_valbuf<char>::append(beg, end);
    }

    void append(std::string_view v) {
        append(&v[0], &v[v.size()]);
    }

    std::string_view str() {
        return std::string_view{buf.data(), buf.size()};
    }

    std::string_view str_term() {
        return std::string_view{buf.data(), buf.size() - 1};
    }
};

} /* namespace cscript */

#endif
