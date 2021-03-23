#ifndef LIBCUBESCRIPT_PARSER_HH
#define LIBCUBESCRIPT_PARSER_HH

#include <cubescript/cubescript.hh>

#include <string_view>

namespace cubescript {

integer_type parse_int(std::string_view input, std::string_view *end = nullptr);
float_type parse_float(std::string_view input, std::string_view *end = nullptr);

bool is_valid_name(std::string_view input);

} /* namespace cubescript */

#endif
