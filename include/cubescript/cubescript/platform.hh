#ifndef LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH
#define LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH

namespace cubescript {

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

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_PLATFORM_HH */
