#include <cubescript/cubescript.hh>

#include <cstdlib>
#include <algorithm>
#include <utility>

#include "cs_thread.hh"
#include "cs_error.hh"

namespace cubescript {

static void save_stack(
    state &cs, typename error::stack_node *&sbeg,
    typename error::stack_node *&send
) {
    auto &ts = state_p{cs}.ts();
    builtin_var *dalias = ts.istate->ivar_dbgalias;
    auto dval = std::size_t(std::clamp(
        dalias->value().get_integer(), integer_type(0), integer_type(1000)
    ));
    if (!dval) {
        sbeg = send = nullptr;
        return;
    }
    std::size_t depth = 0;
    std::size_t total = ts.callstack.size();
    if (!total) {
        sbeg = send = nullptr;
        return;
    }
    auto slen = std::min(total, dval);
    auto *st = static_cast<typename error::stack_node *>(ts.istate->alloc(
        nullptr, 0, sizeof(typename error::stack_node) * slen
    ));
    typename error::stack_node *ret = st, *nd = st;
    ++st;
    for (std::size_t i = total - 1;; --i) {
        auto &lev = ts.callstack[i];
        ++depth;
        if (depth < dval) {
            new (nd) typename error::stack_node{
                lev.id, total - depth + 1
            };
            if (i == 0) {
                break;
            }
            nd = st++;
        } else if (i == 0) {
            new (nd) typename error::stack_node{lev.id, 1};
            break;
        }
    }
    sbeg = ret;
    send = ret + slen;
}

LIBCUBESCRIPT_EXPORT error::error(error &&v):
    p_errbeg{v.p_errbeg}, p_errend{v.p_errend},
    p_sbeg{v.p_sbeg}, p_send{v.p_send}, p_state{v.p_state}
{
    v.p_sbeg = v.p_send = nullptr;
}

LIBCUBESCRIPT_EXPORT error &error::operator=(error &&v) {
    std::swap(p_errbeg, v.p_errbeg);
    std::swap(p_errend, v.p_errend);
    std::swap(p_sbeg, v.p_sbeg);
    std::swap(p_send, v.p_send);
    std::swap(p_state, v.p_state);
    return *this;
}

LIBCUBESCRIPT_EXPORT error::~error() {
    state_p{*p_state}.ts().istate->destroy_array(
        p_sbeg, std::size_t(p_send - p_sbeg)
    );
}

LIBCUBESCRIPT_EXPORT error::error(state &cs, std::string_view msg):
    p_errbeg{}, p_errend{}, p_state{&cs}
{
    char *sp;
    char *buf = state_p{cs}.ts().request_errbuf(msg.size(), sp);
    std::memcpy(buf, msg.data(), msg.size());
    buf[msg.size()] = '\0';
    p_errbeg = sp;
    p_errend = buf + msg.size();
    save_stack(cs, p_sbeg, p_send);
}

LIBCUBESCRIPT_EXPORT error::error(
    state &cs, char const *errbeg, char const *errend
): p_errbeg{errbeg}, p_errend{errend}, p_state{&cs} {
    save_stack(cs, p_sbeg, p_send);
}

std::string_view error::what() const {
    return std::string_view{p_errbeg, std::size_t(p_errend - p_errbeg)};
}

span_type<typename error::stack_node const> error::stack() const {
    return span_type<typename error::stack_node const>{
        p_sbeg, std::size_t(p_send - p_sbeg)
    };
}

} /* namespace cubescript */
