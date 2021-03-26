#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_gen.hh"
#include "cs_thread.hh"

namespace cubescript {

struct break_exception {
};

struct continue_exception {
};

struct run_depth_guard {
    run_depth_guard() = delete;
    run_depth_guard(thread_state &ts);
    run_depth_guard(run_depth_guard const &) = delete;
    run_depth_guard(run_depth_guard &&) = delete;
    ~run_depth_guard();
};

struct stack_guard {
    thread_state *tsp;
    std::size_t oldtop;

    stack_guard() = delete;
    stack_guard(thread_state &ts):
        tsp{&ts}, oldtop{ts.vmstack.size()}
    {}

    ~stack_guard() {
        tsp->vmstack.resize(oldtop, any_value{*tsp->pstate});
    }

    stack_guard(stack_guard const &) = delete;
    stack_guard(stack_guard &&) = delete;
};

template<typename F>
static void call_with_args(thread_state &ts, F body) {
    if (!ts.callstack) {
        body();
        return;
    }
    valarray<ident_stack, MAX_ARGUMENTS> argstack{*ts.pstate};
    int argmask1 = ts.callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            static_cast<alias_impl *>(ts.istate->identmap[i])->undo_arg(
                argstack[i]
            );
        }
    }
    ident_link *prevstack = ts.callstack->next;
    ident_link aliaslink = {
        ts.callstack->id, ts.callstack,
        prevstack ? prevstack->usedargs : ((1 << MAX_ARGUMENTS) - 1),
        prevstack ? prevstack->argstack : nullptr
    };
    ts.callstack = &aliaslink;
    call_with_cleanup(std::move(body), [&]() {
        if (prevstack) {
            prevstack->usedargs = aliaslink.usedargs;
        }
        ts.callstack = aliaslink.next;
        int argmask2 = ts.callstack->usedargs;
        for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
            if (argmask2 & 1) {
                static_cast<alias_impl *>(ts.istate->identmap[i])->redo_arg(
                    argstack[i]
                );
            }
        }
    });
}

void exec_command(
    thread_state &ts, command_impl *id, any_value *args, any_value &res,
    std::size_t nargs, bool lookup = false
);

void exec_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs,
    std::size_t offset, std::size_t skip, std::uint32_t op
);

std::uint32_t *vm_exec(
    thread_state &ts, std::uint32_t *code, any_value &result
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_VM_HH */
