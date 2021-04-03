#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <type_traits>

namespace cubescript {
    using integer_type = int;
    using float_type = float;

    constexpr auto const INT_FORMAT = "%d";
    constexpr auto const FLOAT_FORMAT = "%.7g";
    constexpr auto const ROUND_FLOAT_FORMAT = "%.1f";
} /* namespace cubescript */

/* conf verification */

namespace cubescript {

static_assert(
    std::is_integral_v<integer_type>, "integer_type must be integral"
);
static_assert(
    std::is_signed_v<integer_type>, "integer_type must be signed"
);
static_assert(
    std::is_floating_point_v<float_type>, "float_type must be floating point"
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */
