#include <cubescript/cubescript.hh>

#include <cstdlib>
#include <algorithm>

#include "cs_thread.hh"
#include "cs_error.hh"

namespace cubescript {

LIBCUBESCRIPT_EXPORT stack_state::stack_state(
    state &cs, node *nd
):
    p_state{cs}, p_node{nd}
{}

LIBCUBESCRIPT_EXPORT stack_state::stack_state(stack_state &&st):
    p_state{st.p_state}, p_node{st.p_node}
{
    st.p_node = nullptr;
}

LIBCUBESCRIPT_EXPORT stack_state::~stack_state() {
    size_t len = 0;
    for (node const *nd = p_node; nd; nd = nd->next) {
        ++len;
    }
    state_p{p_state}.ts().istate->destroy_array(p_node, len);
}

LIBCUBESCRIPT_EXPORT stack_state &stack_state::operator=(stack_state &&st) {
    p_node = st.p_node;
    st.p_node = nullptr;
    return *this;
}

LIBCUBESCRIPT_EXPORT stack_state::node const *stack_state::get() const {
    return p_node;
}

static stack_state save_stack(state &cs) {
    auto &ts = state_p{cs}.ts();
    builtin_var *dalias = ts.istate->ivar_dbgalias;
    auto dval = std::clamp(
        dalias->value().get_integer(), integer_type(0), integer_type(1000)
    );
    if (!dval) {
        return stack_state{cs, nullptr};
    }
    int total = 0, depth = 0;
    for (ident_link *l = ts.callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return stack_state{cs, nullptr};
    }
    stack_state::node *st = ts.istate->create_array<stack_state::node>(
        std::min(total, dval)
    );
    stack_state::node *ret = st, *nd = st;
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
    return stack_state{cs, ret};
}

LIBCUBESCRIPT_EXPORT error::error(state &cs, std::string_view msg):
    p_errbeg{}, p_errend{}, p_stack{cs}
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
): p_errbeg{errbeg}, p_errend{errend}, p_stack{cs} {
    p_stack = save_stack(cs);
}

} /* namespace cubescript */
