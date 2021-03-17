#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <limits.h>
#include <functional>
#include <ostd/range.hh>

/* do not modify */
namespace cscript {
    struct cs_state;
    struct cs_ident;
    struct cs_value;
    struct cs_var;

    using cs_value_r       = ostd::iterator_range<cs_value *>;
    using cs_ident_r       = ostd::iterator_range<cs_ident **>;
    using cs_const_ident_r = ostd::iterator_range<cs_ident const **>;
}

/* configurable section */
namespace cscript {
    using cs_int = int;
    using cs_float = float;

    /* probably don't want to change these, but if you use a custom allocation
     * function for your state, keep in mind potential heap allocations in
     * these are not handled by it (as std::function has no allocator support)
     *
     * normally std::function is optimized not to do allocations for small
     * objects, so as long as you don't pass a lambda that captures by copy
     * or move or something similar, you should be fine - but if you really
     * need to make sure, override this with your own type
     */
    using cs_var_cb     = std::function<void(cs_state &, cs_ident &)>;
    using cs_vprint_cb  = std::function<void(cs_state const &, cs_var const &)>;
    using cs_command_cb = std::function<void(cs_state &, cs_value_r, cs_value &)>;
    using cs_hook_cb    = std::function<void(cs_state &)>;
    using cs_alloc_cb   = void *(*)(void *, void *, size_t, size_t);

    constexpr auto const IntFormat = "%d";
    constexpr auto const FloatFormat = "%.7g";
    constexpr auto const RoundFloatFormat = "%.1f";
} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */