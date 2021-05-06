#include <cubescript/cubescript.hh>

#include <cstdlib>
#include <algorithm>

#include "cs_thread.hh"

namespace cubescript {

LIBCUBESCRIPT_EXPORT stack_state::stack_state(
    state &cs, node *nd, bool gap
):
    p_state{cs}, p_node{nd}, p_gap{gap}
{}

LIBCUBESCRIPT_EXPORT stack_state::stack_state(stack_state &&st):
    p_state{st.p_state}, p_node{st.p_node}, p_gap{st.p_gap}
{
    st.p_node = nullptr;
    st.p_gap = false;
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
    p_gap = st.p_gap;
    st.p_node = nullptr;
    st.p_gap = false;
    return *this;
}

LIBCUBESCRIPT_EXPORT stack_state::node const *stack_state::get() const {
    return p_node;
}

LIBCUBESCRIPT_EXPORT bool stack_state::gap() const {
    return p_gap;
}

LIBCUBESCRIPT_EXPORT char *error::request_buf(
    state &cs, std::size_t bufs, char *&sp
) {
    auto &ts = state_p{cs}.ts();
    charbuf &cb = ts.errbuf;
    cb.clear();
    std::size_t sz = 0;
    if (ts.current_line) {
        /* we can attach line number */
        sz = ts.source.size() + 32;
        for (;;) {
            /* we are using so the buffer tracks the elements and therefore
             * does not wipe them when we attempt to reserve more capacity
             */
            cb.resize(sz);
            int nsz;
            if (!ts.source.empty()) {
                nsz = std::snprintf(
                    cb.data(), sz, "%.*s:%zu: ",
                    int(ts.source.size()), ts.source.data(),
                    *ts.current_line
                );
            } else {
                nsz = std::snprintf(cb.data(), sz, "%zu: ", *ts.current_line);
            }
            if (nsz <= 0) {
                abort(); /* should be unreachable */
            } else if (std::size_t(nsz) < sz) {
                sz = std::size_t(nsz);
                break;
            }
            sz = std::size_t(nsz + 1);
        }
    }
    cb.resize(sz + bufs + 1);
    sp = cb.data();
    return &cb[sz];
}

LIBCUBESCRIPT_EXPORT stack_state error::save_stack(state &cs) {
    auto &ts = state_p{cs}.ts();
    integer_var *dalias = ts.istate->ivar_dbgalias;
    auto dval = std::clamp(
        dalias->value().get_integer(), integer_type(0), integer_type(1000)
    );
    if (!dval) {
        return stack_state{cs, nullptr, !!ts.callstack};
    }
    int total = 0, depth = 0;
    for (ident_link *l = ts.callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return stack_state{cs, nullptr, false};
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
    return stack_state{cs, ret, total > dval};
}

} /* namespace cubescript */
