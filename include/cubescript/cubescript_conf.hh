/** @file cubescript_conf.hh
 *
 * @brief Library configuration.
 *
 * While you can technically modify this directly, it is better if you use
 * a custom file `cubescript_conf_user.hh` in the same location. Most of the
 * time you will not want to override anything, but should you need to change
 * the integer, float or span types for a specific purpose, this allows you to.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <type_traits>

#if __has_include("cubescript_conf_user.hh")
#  include "cubescript_conf_user.hh"
#endif

#if !defined(LIBCUBESCRIPT_CONF_USER_SPAN)
#  include <span>
#endif

#ifndef LIBCUBESCRIPT_CONF_THREAD_SAFE
/** @brief Controls thread safety of the implementation.
 *
 * By default, libcubescript is thread safe. That means using locking where
 * necessary as well as atomic variables where necessary. If you do not need
 * that, you can disable this by overriding this macro to 0 in your user
 * configuration. This does not make any difference in behavior when used
 * in single-threaded scenarios, other than possibly better performance.
 */
#define LIBCUBESCRIPT_CONF_THREAD_SAFE 1
#endif

namespace cubescript {
#if !defined(LIBCUBESCRIPT_CONF_USER_INTEGER)
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
     * Define `LIBCUBESCRIPT_CONF_USER_INTEGER` in your custom conf file
     * to disable the builtin.
     *
     * @see float_type
     * @see INTEGER_FORMAT
     */
    using integer_type = int;
#endif

#if !defined(LIBCUBESCRIPT_CONF_USER_FLOAT)
    /** @brief The floating point type used.
     *
     * By default, this is `float`, which is on most platforms an IEEE754
     * binary32 data type.
     *
     * Define `LIBCUBESCRIPT_CONF_USER_FLOAT` in your custom conf file
     * to disable the builtin.
     *
     * Must be at most as large as the largest standard integer type.
     *
     * @see integer_type
     * @see FLOAT_FORMAT
     * @see ROUND_FLOAT_FORMAT
     */
    using float_type = float;
#endif

#if !defined(LIBCUBESCRIPT_CONF_USER_SPAN)
    /** @brief The span type used.
     *
     * By default, this is `std::span`. You will almost never want to override
     * this, but an alternative implementation can be supplied if your standard
     * library does not support it.
     *
     * Define `LIBCUBESCRIPT_CONF_USER_SPAN` in your custom conf file to
     * disable the builtin.
     */
    template<typename T>
    using span_type = std::span<T>;
#endif

#if !defined(LIBCUBESCRIPT_CONF_USER_INTEGER)
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
     * Define `LIBCUBESCRIPT_CONF_USER_INTEGER` in your custom conf file
     * to disable the builtin.
     *
     * @see integer_type
     * @see FLOAT_FORMAT
     */
    constexpr auto const INTEGER_FORMAT = "%d";
#endif

#if !defined(LIBCUBESCRIPT_CONF_USER_FLOAT)
    /** @brief The float format used.
     *
     * This is a formatting specifier as in `printf`, corresponding to the
     * `float_type` used. It is used to handle conversions from the type to
     * strings, as well as in the default float variable handler when printing.
     *
     * When the floating point value is equivalent to its integer value (i.e.
     * it has no decimal point), ROUND_FLOAT_FORMAT is used.
     *
     * Define `LIBCUBESCRIPT_CONF_USER_FLOAT` in your custom conf file
     * to disable the builtin.
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
#endif
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
static_assert(
    sizeof(float_type) <= sizeof(unsigned long long), "float_type is too large"
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */
