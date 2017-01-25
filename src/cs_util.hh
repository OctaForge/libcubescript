#ifndef LIBCUBESCRIPT_CS_UTIL_HH
#define LIBCUBESCRIPT_CS_UTIL_HH

#include <ostd/string.hh>
#include <ostd/utility.hh>
#include <ostd/type_traits.hh>

namespace cscript {

template<typename K, typename V>
using CsMap = ostd::Map<K, V>;

template<typename T>
using CsVector = std::vector<T>;

CsInt cs_parse_int(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

CsFloat cs_parse_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end = nullptr
);

template<typename F>
struct CsScopeExit {
    template<typename FF>
    CsScopeExit(FF &&f): func(std::forward<FF>(f)) {}
    ~CsScopeExit() {
        func();
    }
    ostd::Decay<F> func;
};

template<typename F1, typename F2>
inline void cs_do_and_cleanup(F1 &&dof, F2 &&clf) {
    CsScopeExit<F2> cleanup(std::forward<F2>(clf));
    dof();
}

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_UTIL_HH */
