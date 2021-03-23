#ifndef LIBCUBESCRIPT_PARSER_HH
#define LIBCUBESCRIPT_PARSER_HH

#include <cubescript/cubescript.hh>

#include <string_view>

namespace cscript {

cs_int parse_int(std::string_view input, std::string_view *end = nullptr);
cs_float parse_float(std::string_view input, std::string_view *end = nullptr);

bool is_valid_name(std::string_view input);

} /* namespace cscript */

#endif
