#include <cubescript/cubescript.hh>

#include <cstdlib>
#include <algorithm>

#include "cs_thread.hh"
#include "cs_error.hh"

namespace cubescript {

static typename error::stack_node *save_stack(state &cs) {
    auto &ts = state_p{cs}.ts();
    builtin_var *dalias = ts.istate->ivar_dbgalias;
    auto dval = std::clamp(
        dalias->value().get_integer(), integer_type(0), integer_type(1000)
    );
    if (!dval) {
        return nullptr;
    }
    int total = 0, depth = 0;
    for (ident_link *l = ts.callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return nullptr;
    }
    auto *st = ts.istate->create_array<typename error::stack_node>(
        std::min(total, dval)
    );
    typename error::stack_node *ret = st, *nd = st;
    ++st;
    for (ident_link *l = ts.callstack; l; l = l->next) {
        ++depth;
        if (depth < dval) {
            nd->id = l->id;
            nd->index = total - depth + 1;
            if (!l->next) {
                nd->next = nullptr;
            } else {
                nd->next = st;
            }
            nd = st++;
        } else if (!l->next) {
            nd->id = l->id;
            nd->index = 1;
            nd->next = nullptr;
        }
    }
    return ret;
}

LIBCUBESCRIPT_EXPORT error::~error() {
    std::size_t slen = 0;
    for (stack_node const *nd = p_stack; nd; nd = nd->next) {
        ++slen;
    }
    state_p{*p_state}.ts().istate->destroy_array(p_stack, slen);
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
    p_stack = save_stack(cs);
}

LIBCUBESCRIPT_EXPORT error::error(
    state &cs, char const *errbeg, char const *errend
): p_errbeg{errbeg}, p_errend{errend}, p_state{&cs} {
    p_stack = save_stack(cs);
}

} /* namespace cubescript */
