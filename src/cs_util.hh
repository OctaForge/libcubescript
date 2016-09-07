#ifndef LIBCUBESCRIPT_CS_UTIL_HH
#define LIBCUBESCRIPT_CS_UTIL_HH

#include <ostd/string.hh>

namespace cscript {

CsInt cs_parse_int(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

CsFloat cs_parse_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
