#ifndef LIBCUBESCRIPT_THREAD_HH
#define LIBCUBESCRIPT_THREAD_HH

#include <cubescript/cubescript.hh>

#include <utility>

#include "cs_std.hh"
#include "cs_state.hh"

namespace cubescript {

struct codegen_state;

struct thread_state {
    /* thread call stack */
    ident_link *callstack{};
    /* the shared state pointer */
    internal_state *istate{};
    /* the public state interface */
    state *pstate{};
    /* current codegen state for diagnostics */
    codegen_state *cstate{};
    /* value stack for VM */
    valbuf<any_value> vmstack;
    /* per-thread storage buffer for error messages */
    charbuf errbuf;
    /* we can attach a hook to vm events */
    hook_func call_hook{};
    /* loop nesting level */
    int loop_level = 0;

    thread_state(internal_state *cs);

    hook_func set_hook(hook_func f);

    hook_func &get_hook() { return call_hook; }
    hook_func const &get_hook() const { return call_hook; }
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_THREAD_HH */
