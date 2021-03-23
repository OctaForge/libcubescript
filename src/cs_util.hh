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

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
