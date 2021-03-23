#ifndef LIBCUBESCRIPT_CS_UTIL_HH
#define LIBCUBESCRIPT_CS_UTIL_HH

#include <type_traits>
#include <unordered_map>
#include <vector>

#include "cs_bcode.hh"
#include "cs_state.hh"

namespace cscript {

cs_int cs_parse_int(
    std::string_view input, std::string_view *end = nullptr
);

cs_float cs_parse_float(
    std::string_view input, std::string_view *end = nullptr
);

struct cs_strman;
struct cs_shared_state;

inline cs_strref cs_make_strref(char const *p, cs_shared_state *cs) {
    return cs_strref{p, cs};
}

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
    using allocator_type = cs_allocator<
        std::pair<std::string_view const, cs_strref_state *>
    >;
    cs_strman() = delete;
    cs_strman(cs_shared_state *cs): cstate{cs}, counts{allocator_type{cs}} {}
    ~cs_strman() {}

    cs_strman(cs_strman const &) = delete;
    cs_strman(cs_strman &&) = delete;

    cs_strman &operator=(cs_strman const &) = delete;
    cs_strman &operator=(cs_strman &&) = delete;

    /* adds a string into the manager using any source, and returns a managed
     * version; this is "slow" as it has to hash the string and potentially
     * allocate fresh memory for it, but is perfectly safe at any time
     */
    char const *add(std::string_view str);

    /* this simply increments the reference count of an existing managed
     * string, this is only safe when you know the pointer you are passing
     * is already managed the system
     */
    char const *ref(char const *ptr);

    /* this will use the provided memory, assuming it is a fresh string that
     * is yet to be added; the memory must be allocated with alloc_buf()
     */
    char const *steal(char *ptr);

    /* decrements the reference count and removes it from the system if
     * that reaches zero; likewise, only safe with pointers that are managed
     */
    void unref(char const *ptr);

    /* just finds a managed pointer with the same contents
     * as the input, if not found then a null pointer is returned
     */
    char const *find(std::string_view str) const;

    /* a quick helper to make a proper string view out of a ptr */
    std::string_view get(char const *ptr) const;

    /* this will allocate a buffer of the given length (plus one for
     * terminating zero) so you can fill it; use steal() to write it
     */
    char *alloc_buf(std::size_t len) const;

    cs_shared_state *cstate;
    std::unordered_map<
        std::string_view, cs_strref_state *,
        std::hash<std::string_view>,
        std::equal_to<std::string_view>,
        allocator_type
    > counts;
};

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
