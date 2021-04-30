#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"

namespace cubescript {

struct break_exception {
};

struct continue_exception {
};

struct call_depth_guard {
    call_depth_guard() = delete;
    call_depth_guard(thread_state &ts);
    call_depth_guard(call_depth_guard const &) = delete;
    call_depth_guard(call_depth_guard &&) = delete;
    ~call_depth_guard();

    thread_state *tsp;
};

struct stack_guard {
    thread_state *tsp;
    std::size_t oldtop;

    stack_guard() = delete;
    stack_guard(thread_state &ts):
        tsp{&ts}, oldtop{ts.vmstack.size()}
    {}

    ~stack_guard() {
        tsp->vmstack.resize(oldtop);
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
    auto mask = ts.callstack->usedargs;
    std::size_t noff = ts.idstack.size();
    for (std::size_t i = 0; mask.any(); ++i) {
        if (mask[0]) {
            auto &ast = ts.get_astack(
                static_cast<alias *>(ts.istate->identmap[i])
            );
            auto &st = ts.idstack.emplace_back();
            st.next = ast.node;
            ast.node = ast.node->next;
        }
        mask >>= 1;
    }
    ident_link *prevstack = ts.callstack->next;
    ident_link aliaslink = {
        ts.callstack->id, ts.callstack,
        prevstack ? prevstack->usedargs : argset{}
    };
    if (!prevstack) {
        aliaslink.usedargs.set();
    }
    ts.callstack = &aliaslink;
    auto cleanup = [&]() {
        if (prevstack) {
            prevstack->usedargs = aliaslink.usedargs;
        }
        ts.callstack = aliaslink.next;
        auto mask2 = ts.callstack->usedargs;
        for (std::size_t i = 0, nredo = 0; mask2.any(); ++i) {
            if (mask2[0]) {
                ts.get_astack(
                    static_cast<alias *>(ts.istate->identmap[i])
                ).node = ts.idstack[noff + nredo++].next;
            }
            mask2 >>= 1;
        }
        ts.idstack.resize(noff);
    };
    try {
        body();
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup = false
);

bool exec_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs, std::size_t offset,
    std::size_t skip, std::uint32_t op, bool ncheck = false
);

std::uint32_t *vm_exec(
    thread_state &ts, std::uint32_t *code, any_value &result
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_VM_HH */
