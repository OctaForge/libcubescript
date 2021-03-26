#include <cubescript/cubescript.hh>

#include "cs_gen.hh"
#include "cs_thread.hh"

namespace cubescript {

LIBCUBESCRIPT_EXPORT char *error::request_buf(
    thread_state &ts, std::size_t bufs, char *&sp
) {
    charbuf &cb = ts.errbuf;
    codegen_state *gs = ts.cstate;
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
                throw internal_error{"format error"};
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

LIBCUBESCRIPT_EXPORT stack_state error::save_stack(thread_state &ts) {
    integer_var *dalias = static_cast<integer_var *>(
        ts.istate->identmap[ID_IDX_DBGALIAS]
    );
    if (!dalias->get_value()) {
        return stack_state{ts, nullptr, !!ts.callstack};
    }
    int total = 0, depth = 0;
    for (ident_link *l = ts.callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return stack_state{ts, nullptr, false};
    }
    stack_state::node *st = ts.istate->create_array<stack_state::node>(
        std::min(total, dalias->get_value())
    );
    stack_state::node *ret = st, *nd = st;
    ++st;
    for (ident_link *l = ts.callstack; l; l = l->next) {
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
    return stack_state{ts, ret, total > dalias->get_value()};
}

} /* namespace cubescript */
