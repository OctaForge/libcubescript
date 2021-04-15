/** @file cubescript_conf.hh
 *
 * @brief Library configuration.
 *
 * This is the one file you are allowed to touch as a user - it contains
 * settings that are used when building the library, notably the integer
 * and floating point types used and their formats (used for conversions).
 *
 * Usually you will not want to touch this, but occasionally you might want
 * to, e.g. to make a build of the library that uses double precision floats
 * or larger integers.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <type_traits>

namespace cubescript {
    /** @brief The integer type used.
     *
     * While Cubescript is a stringly typed language, it uses integers and
     * floats internally in a transparent manner where possible, and allows
     * you to retrieve and pass integers and floats in commands and so on.
     *
     * This is the integer type used. By default, it's `int`, which is a
     * 32-bit signed integer on most platforms. Keep in mind that is is
     * necessary for this type to be a signed integer type.
     *
     * @see float_type
     * @see INTEGER_FORMAT
     */
    using integer_type = int;

    /** @brief The floating point type used.
     *
     * By default, this is `float`, which is on most platforms an IEEE754
     * binary32 data type.
     *
     * @see integer_type
     * @see FLOAT_FORMAT
     * @see ROUND_FLOAT_FORMAT
     */
    using float_type = float;

    /** @brief The integer format used.
     *
     * This is a formatting specifier as in `printf`, corresponding to the
     * `integer_type` used. It is used to handle conversions from the type
     * to strings, as well as in the default integer variable handler when
     * printing.
     *
     * There are no special restrictions imposed on the floating point type
     * other than that it actually has to be floating point.
     *
     * @see integer_type
     * @see FLOAT_FORMAT
     */
    constexpr auto const INTEGER_FORMAT = "%d";

    /** @brief The float format used.
     *
     * This is a formatting specifier as in `printf`, corresponding to the
     * `float_type` used. It is used to handle conversions from the type to
     * strings, as well as in the default float variable handler when printing.
     *
     * When the floating point value is equivalent to its integer value (i.e.
     * it has no decimal point), ROUND_FLOAT_FORMAT is used.
     *
     * @see float_type
     * @see ROUND_FLOAT_FORMAT
     * @see INTEGER_FORMAT
     */
    constexpr auto const FLOAT_FORMAT = "%.7g";

    /** @brief The round float format used.
     *
     * This is a formatting specifier as in `printf`, corresponding to the
     * `float_type` used. It's like `FLOAT_FORMAT` but used when the value
     * has no decimal point.
     *
     * @see float_type
     * @see FLOAT_FORMAT
     */
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
