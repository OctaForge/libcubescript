#ifndef LIBCUBESCRIPT_STATE_HH
#define LIBCUBESCRIPT_STATE_HH

#include <cubescript/cubescript.hh>

#include <unordered_map>
#include <string>

#include "cs_bcode.hh"

namespace cubescript {

struct state;
struct internal_state;
struct string_pool;

template<typename T>
struct std_allocator {
    using value_type = T;

    inline std_allocator(internal_state *s);
    inline std_allocator(state &cs);

    template<typename U>
    std_allocator(std_allocator<U> const &a): istate{a.istate} {};

    inline T *allocate(std::size_t n);
    inline void deallocate(T *p, std::size_t n);

    template<typename U>
    bool operator==(std_allocator<U> const &a) {
        return istate == a.istate;
    }

    internal_state *istate;
};

struct internal_state {
    using allocator_type = std_allocator<
        std::pair<std::string_view const, ident *>
    >;
    alloc_func allocf;
    void *aptr;

    std::unordered_map<
        std::string_view, ident *,
        std::hash<std::string_view>,
        std::equal_to<std::string_view>,
        allocator_type
    > idents;
    std::vector<ident *, std_allocator<ident *>> identmap;

    var_print_func varprintf;
    string_pool *strman;
    empty_block *empty;

    internal_state() = delete;

    internal_state(alloc_func af, void *data);

    ~internal_state();

    void *alloc(void *ptr, size_t os, size_t ns);

    template<typename T, typename ...A>
    T *create(A &&...args) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, sizeof(T)));
        new (ret) T(std::forward<A>(args)...);
        return ret;
    }

    template<typename T>
    T *create_array(size_t len) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, len * sizeof(T)));
        for (size_t i = 0; i < len; ++i) {
            new (&ret[i]) T();
        }
        return ret;
    }

    template<typename T>
    void destroy(T *v) noexcept {
        v->~T();
        alloc(v, sizeof(T), 0);
    }

    template<typename T>
    void destroy_array(T *v, size_t len) noexcept {
        v->~T();
        alloc(v, len * sizeof(T), 0);
    }
};

inline internal_state *state_get_internal(state &cs) {
    return cs.p_state;
}

template<typename T>
inline std_allocator<T>::std_allocator(internal_state *s): istate{s} {}

template<typename T>
inline std_allocator<T>::std_allocator(state &s): istate{state_get_internal(s)} {}

template<typename T>
inline T *std_allocator<T>::allocate(std::size_t n) {
    return static_cast<T *>(istate->alloc(nullptr, 0, n * sizeof(T)));
}

template<typename T>
inline void std_allocator<T>::deallocate(T *p, std::size_t n) {
    istate->alloc(p, n, 0);
}

} /* namespace cubescript */

#endif
