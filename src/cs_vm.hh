#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

#include "cubescript/cubescript.hh"

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_gen.hh"

namespace cubescript {

struct break_exception {
};

struct continue_exception {
};

template<typename F>
static void call_with_args(state &cs, F body) {
    if (!cs.p_callstack) {
        body();
        return;
    }
    valarray<ident_stack, MAX_ARGUMENTS> argstack{cs};
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            static_cast<alias_impl *>(cs.p_state->identmap[i])->undo_arg(
                argstack[i]
            );
        }
    }
    ident_link *prevstack = cs.p_callstack->next;
    ident_link aliaslink = {
        cs.p_callstack->id, cs.p_callstack,
        prevstack ? prevstack->usedargs : ((1 << MAX_ARGUMENTS) - 1),
        prevstack ? prevstack->argstack : nullptr
    };
    cs.p_callstack = &aliaslink;
    call_with_cleanup(std::move(body), [&]() {
        if (prevstack) {
            prevstack->usedargs = aliaslink.usedargs;
        }
        cs.p_callstack = aliaslink.next;
        int argmask2 = cs.p_callstack->usedargs;
        for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
            if (argmask2 & 1) {
                static_cast<alias_impl *>(cs.p_state->identmap[i])->redo_arg(
                    argstack[i]
                );
            }
        }
    });
}

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_VM_HH */
