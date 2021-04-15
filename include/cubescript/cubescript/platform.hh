/** @file platform.hh
 *
 * @brief Utility macros and platform abstraction.
 *
 * Defines utility macros that you are not supposed to use yourself.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH
#define LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH

namespace cubescript {

#ifdef LIBCS_GENERATING_DOC

/** @brief Public API tag.
 *
 * All public API of the library is tagged like this.
 *
 * On Windows, the behavior of this is conditional. If `LIBCUBESCRIPT_DLL` is
 * not defined, it expands to no value (that means we're either building or
 * using a static library). If it is defined, it will tag the API with either
 * `dllexport` (when building the lib, defined with `LIBCUBESCRIPT_BUILD`)
 * or `dllimport` (when using the lib).
 *
 * On Unix-like systems with GCC-style compilers, this will mark the API as
 * externally visible. The library is by default built so that symbols are
 * normally hidden, so any external API needs to be tagged.
 *
 * @see LIBCUBESCRIPT_LOCAL
 */
#define LIBCUBESCRIPT_EXPORT

/** @brief Private API tag.
 *
 * Since symbols are private by default, this usually has no purpose. However,
 * when marking entire structures exported, this affects all methods inside;
 * in those cases this can be used to mark specific methods as for use only
 * inside of the library (private methods not called in any public header).
 *
 * @see LIBCUBESCRIPT_EXPORT
 */
#define LIBCUBESCRIPT_LOCAL

#else

#if defined(__CYGWIN__) || (defined(_WIN32) && !defined(_XBOX_VER))
#  ifdef LIBCUBESCRIPT_DLL
#    ifdef LIBCUBESCRIPT_BUILD
#      define LIBCUBESCRIPT_EXPORT __declspec(dllexport)
#    else
#      define LIBCUBESCRIPT_EXPORT __declspec(dllimport)
#    endif
#  else
#    define LIBCUBESCRIPT_EXPORT
#  endif
#  define LIBCUBESCRIPT_LOCAL
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LIBCUBESCRIPT_EXPORT __attribute__((visibility("default")))
#    define LIBCUBESCRIPT_LOCAL __attribute__((visibility("hidden")))
#  else
#    define LIBCUBESCRIPT_EXPORT
#    define LIBCUBESCRIPT_LOCAL
#  endif
#endif

#endif /* LIBCS_GENERATING_DOC */

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH */
