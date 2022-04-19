#ifndef LIBCUBESCRIPT_STRMAN_HH
#define LIBCUBESCRIPT_STRMAN_HH

#include <cubescript/cubescript.hh>

#include <unordered_map>
#include <string_view>
#include <mutex>

#include "cs_std.hh"
#include "cs_state.hh"

namespace cubescript {

struct string_ref_state;

char const *str_managed_ref(char const *str);
void str_managed_unref(char const *str);
std::string_view str_managed_view(char const *str);

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
 * the string manager is thread-safe, so it should be usable in any context
 */

struct string_pool {
    using allocator_type = std_allocator<
        std::pair<std::string_view const, string_ref_state *>
    >;
    string_pool() = delete;
    string_pool(internal_state *cs): cstate{cs}, counts{allocator_type{cs}} {}
    ~string_pool() {}

    string_pool(string_pool const &) = delete;
    string_pool(string_pool &&) = delete;

    string_pool &operator=(string_pool const &) = delete;
    string_pool &operator=(string_pool &&) = delete;

    /* adds a string into the manager using any source, and returns a managed
     * version; this is "slow" as it has to hash the string and potentially
     * allocate fresh memory for it, but is perfectly safe at any time
     */
    char const *add(std::string_view str);

    /* this simply increments the reference count of an existing managed
     * string, this is only safe when you know the pointer you are passing
     * is already managed the system
     */
    char const *internal_ref(char const *ptr);

    /* this will use the provided memory, assuming it is a fresh string that
     * is yet to be added; the memory must be allocated with alloc_buf()
     */
    string_ref steal(char *ptr);

    /* decrements the reference count and removes it from the system if
     * that reaches zero; likewise, only safe with pointers that are managed
     */
    void internal_unref(char const *ptr);

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

    internal_state *cstate;
    mutable std::mutex p_mtx{};
    std::unordered_map<
        std::string_view, string_ref_state *,
        std::hash<std::string_view>,
        std::equal_to<std::string_view>,
        allocator_type
    > counts;
};

} /* namespace cubescript */

#endif
