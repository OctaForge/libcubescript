#ifndef LIBCUBESCRIPT_CUBESCRIPT_CONF_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CONF_HH

#include <limits.h>
#include <ostd/types.hh>
#include <ostd/memory.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/map.hh>
#include <ostd/stream.hh>

namespace cscript {
    using CsInt = int;
    using CsFloat = float;
    using CsString = ostd::String;

    template<typename K, typename V>
    using CsMap = ostd::Map<K, V>;

    template<typename T>
    using CsVector = ostd::Vector<T>;

    using CsStream = ostd::Stream;

    constexpr CsInt const CsIntMin = INT_MIN;
    constexpr CsInt const CsIntMax = INT_MAX;

    constexpr auto const IntFormat = "%d";
    constexpr auto const FloatFormat = "%.7g";
    constexpr auto const RoundFloatFormat = "%.1f";
} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */