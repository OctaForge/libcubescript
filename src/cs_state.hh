#ifndef LIBCUBESCRIPT_STATE_HH
#define LIBCUBESCRIPT_STATE_HH

#include <cubescript/cubescript.hh>

#include <unordered_map>
#include <string>

#include "cs_bcode.hh"

namespace cscript {

struct cs_state;
struct cs_shared_state;
struct cs_strman;

template<typename T>
struct cs_allocator {
    using value_type = T;

    inline cs_allocator(cs_shared_state *s);
    inline cs_allocator(cs_state &cs);

    template<typename U>
    cs_allocator(cs_allocator<U> const &a): state{a.state} {};

    inline T *allocate(std::size_t n);
    inline void deallocate(T *p, std::size_t n);

    template<typename U>
    bool operator==(cs_allocator<U> const &a) {
        return state == a.state;
    }

    cs_shared_state *state;
};

struct cs_shared_state {
    using allocator_type = cs_allocator<
        std::pair<std::string_view const, cs_ident *>
    >;
    cs_alloc_cb allocf;
    void *aptr;

    std::unordered_map<
        std::string_view, cs_ident *,
        std::hash<std::string_view>,
        std::equal_to<std::string_view>,
        allocator_type
    > idents;
    std::vector<cs_ident *, cs_allocator<cs_ident *>> identmap;

    cs_vprint_cb varprintf;
    cs_strman *strman;
    empty_block *empty;

    cs_shared_state() = delete;

    cs_shared_state(cs_alloc_cb af, void *data);

    ~cs_shared_state();

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

inline cs_shared_state *cs_get_sstate(cs_state &cs) {
    return cs.p_state;
}

template<typename T>
inline cs_allocator<T>::cs_allocator(cs_shared_state *s): state{s} {}

template<typename T>
inline cs_allocator<T>::cs_allocator(cs_state &s): state{cs_get_sstate(s)} {}

template<typename T>
inline T *cs_allocator<T>::allocate(std::size_t n) {
    return static_cast<T *>(state->alloc(nullptr, 0, n * sizeof(T)));
}

template<typename T>
inline void cs_allocator<T>::deallocate(T *p, std::size_t n) {
    state->alloc(p, n, 0);
}

} /* namespace cscript */

#endif
