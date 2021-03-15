#ifndef LIBCUBESCRIPT_CS_UTIL_HH
#define LIBCUBESCRIPT_CS_UTIL_HH

#include <type_traits>
#include <unordered_map>

#include <ostd/string.hh>

namespace cscript {

template<typename K, typename V>
using cs_map = std::unordered_map<K, V>;

template<typename T>
using cs_vector = std::vector<T>;

cs_int cs_parse_int(
    ostd::string_range input, ostd::string_range *end = nullptr
);

cs_float cs_parse_float(
    ostd::string_range input, ostd::string_range *end = nullptr
);

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
inline void cs_do_and_cleanup(F1 &&dof, F2 &&clf) {
    CsScopeExit<F2> cleanup(std::forward<F2>(clf));
    dof();
}

struct cs_shared_state;

/* string manager
 *
 * the purpose of this is to handle interning of strings; each string within
 * a libcs state is represented (and allocated) exactly once, and reference
 * counted; that both helps save resources, and potentially provide a means
 * to reliably represent returned strings in places that is compatible with
 * multiple threads and eliminate the chance of dangling pointers
 *
 * strings are allocated in a manner where the refcount and length are stored
 * as a part of the string's memory, so it can be easily accessed using just
 * the pointer to the string, but also this is transparent for usage
 *
 * this is not thread-safe yet, and later on it should be made that,
 * for now we don't bother...
 */

struct cs_strref_state {
    size_t length;
    size_t refcount;
};

struct cs_strman {
    cs_strman() = delete;
    cs_strman(cs_shared_state *cs): cstate{cs} {}
    ~cs_strman() {}

    cs_strman(cs_strman const &) = delete;
    cs_strman(cs_strman &&) = delete;

    cs_strman &operator=(cs_strman const &) = delete;
    cs_strman &operator=(cs_strman &&) = delete;

    /* adds a string into the manager using any source, and returns a managed
     * version; this is "slow" as it has to hash the string and potentially
     * allocate fresh memory for it, but is perfectly safe at any time
     */
    char const *add(ostd::string_range str);

    /* this simply increments the reference count of an existing managed
     * string, this is only safe when you know the pointer you are passing
     * is already managed the system
     */
    char const *ref(char const *ptr);

    /* decrements the reference count and removes it from the system if
     * that reaches zero; likewise, only safe with pointers that are managed
     */
    void unref(char const *ptr);

    /* just finds a managed pointer with the same contents
     * as the input, if not found then a null pointer is returned
     */
    char const *find(ostd::string_range str) const;

    /* a quick helper to make a proper ostd string range out of a ptr */
    ostd::string_range get(char const *ptr) const;

    cs_shared_state *cstate;
    /* FIXME: use main allocator */
    std::unordered_map<ostd::string_range, cs_strref_state *> counts{};
};

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
