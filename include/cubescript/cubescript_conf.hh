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
    template<typename T>
    using CsAllocator = ostd::Allocator<T>;

    using CsInt = int;
    using CsFloat = float;
    using CsString = ostd::StringBase<char, CsAllocator<char>>;

    template<typename K, typename V>
    using CsMap = ostd::Map<
        K, V, ostd::ToHash<K>, ostd::EqualWithCstr<K>,
        CsAllocator<ostd::Pair<K const, V const>>
    >;

    template<typename T>
    using CsVector = ostd::Vector<T, CsAllocator<T>>;

    using CsStream = ostd::Stream;

    constexpr CsInt const CsIntMin = INT_MIN;
    constexpr CsInt const CsIntMax = INT_MAX;

    constexpr auto const IntFormat = "%d";
    constexpr auto const FloatFormat = "%.7g";
    constexpr auto const RoundFloatFormat = "%.1f";
} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CONF_HH */