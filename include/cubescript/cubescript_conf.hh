#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <limits.h>
#include <ostd/types.hh>
#include <ostd/memory.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/map.hh>
#include <ostd/stream.hh>

/* do not modify */
namespace cscript {
    struct CsState;
    struct CsIdent;
    struct CsValue;

    using CsValueRange      = ostd::PointerRange<CsValue>;
    using CsIdentRange      = ostd::PointerRange<CsIdent *>;
    using CsConstIdentRange = ostd::PointerRange<CsIdent const *>;
}

/* configurable section */
namespace cscript {
    using CsInt = int;
    using CsFloat = float;

    /* probably don't want to change these, but if you use a custom allocation
     * function for your state, keep in mind potential heap allocations in
     * these are not handled by it (as std::function has no allocator support)
     *
     * normally std::function is optimized not to do allocations for small
     * objects, so as long as you don't pass a lambda that captures by copy
     * or move or something similar, you should be fine - but if you really
     * need to make sure, override this with your own type
     */
    using CsVarCb     = std::function<void(CsState &, CsIdent &)>;
    using CsCommandCb = std::function<void(CsState &, CsValueRange, CsValue &)>;
    using CsHookCb    = std::function<void(CsState &)>;
    using CsAllocCb   = void *(*)(void *, void *, size_t, size_t);

    constexpr auto const IntFormat = "%d";
    constexpr auto const FloatFormat = "%.7g";
    constexpr auto const RoundFloatFormat = "%.1f";

    constexpr auto const IvarFormat = "%s = %d";
    constexpr auto const IvarHexFormat = "%s = 0x%X";
    constexpr auto const IvarHexColorFormat = "%s = 0x%.6X (%d, %d, %d)";
    constexpr auto const FvarFormat = "%s = %.7g";
    constexpr auto const FvarRoundFormat = "%s = %.1f";
    constexpr auto const SvarFormat = "%s = \"%s\"";
    constexpr auto const SvarQuotedFormat = "%s = [%s]";
} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */