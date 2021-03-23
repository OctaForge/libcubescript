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

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
