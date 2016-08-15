#ifndef LIBCUBESCRIPT_CS_UTIL_HH
#define LIBCUBESCRIPT_CS_UTIL_HH

#include <ostd/string.hh>

namespace cscript {
namespace parser {

CsInt parse_int(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

CsFloat parse_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

} /* namespace parser */
} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
