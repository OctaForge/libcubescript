#include <cubescript/cubescript.hh>

#include "cs_vm.hh"

namespace cscript {

LIBCUBESCRIPT_EXPORT char *cs_error::request_buf(
    cs_state &cs, std::size_t bufs, char *&sp
) {
    cs_charbuf &cb = *static_cast<cs_charbuf *>(cs.p_errbuf);
    cs_gen_state *gs = cs.p_pstate;
    cb.clear();
    std::size_t sz = 0;
    if (gs) {
        /* we can attach line number */
        sz = gs->src_name.size() + 32;
        for (;;) {
            /* we are using so the buffer tracks the elements and therefore
             * does not wipe them when we attempt to reserve more capacity
             */
            cb.resize(sz);
            int nsz;
            if (!gs->src_name.empty()) {
                nsz = std::snprintf(
                    cb.data(), sz, "%.*s:%zu: ",
                    int(gs->src_name.size()), gs->src_name.data(),
                    gs->current_line
                );
            } else {
                nsz = std::snprintf(cb.data(), sz, "%zu: ", gs->current_line);
            }
            if (nsz <= 0) {
                throw cs_internal_error{"format error"};
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

LIBCUBESCRIPT_EXPORT cs_stack_state cs_error::save_stack(cs_state &cs) {
    cs_ivar *dalias = static_cast<cs_ivar *>(cs.p_state->identmap[DbgaliasIdx]);
    if (!dalias->get_value()) {
        return cs_stack_state(cs, nullptr, !!cs.p_callstack);
    }
    int total = 0, depth = 0;
    for (cs_ident_link *l = cs.p_callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return cs_stack_state(cs, nullptr, false);
    }
    cs_stack_state_node *st = cs.p_state->create_array<cs_stack_state_node>(
        std::min(total, dalias->get_value())
    );
    cs_stack_state_node *ret = st, *nd = st;
    ++st;
    for (cs_ident_link *l = cs.p_callstack; l; l = l->next) {
        ++depth;
        if (depth < dalias->get_value()) {
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
    return cs_stack_state(cs, ret, total > dalias->get_value());
}

} /* namespace cscript */
