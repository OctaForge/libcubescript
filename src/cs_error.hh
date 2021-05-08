#ifndef LIBCUBESCRIPT_ERROR_HH
#define LIBCUBESCRIPT_ERROR_HH

#include <cubescript/cubescript.hh>

#include <cstdio>
#include <cstddef>

namespace cubescript {

struct error_p {
    template<typename ...A>
    static error make(state &cs, std::string_view msg, A const &...args) {
        std::size_t sz = msg.size() + 64;
        char *buf, *sp;
        for (;;) {
            buf = state_p{cs}.ts().request_errbuf(sz, sp);
            int written = std::snprintf(buf, sz, msg.data(), args...);
            if (written <= 0) {
                throw error{cs, "malformed format string"};
            } else if (std::size_t(written) <= sz) {
                break;
            }
            sz = std::size_t(written);
        }
        return error{cs, sp, buf + sz};
    }
};

} /* namespace cubescript */

#endif
