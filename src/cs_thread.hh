#ifndef LIBCUBESCRIPT_THREAD_HH
#define LIBCUBESCRIPT_THREAD_HH

#include <cubescript/cubescript.hh>

#include <utility>

#include "cs_std.hh"
#include "cs_state.hh"
#include "cs_ident.hh"

namespace cubescript {

struct codegen_state;

struct thread_state {
    using astack_allocator = std_allocator<std::pair<int const, alias_stack>>;
    /* thread call stack */
    ident_link *callstack{};
    /* the shared state pointer */
    internal_state *istate{};
    /* the public state interface */
    state *pstate{};
    /* current codegen state for diagnostics */
    codegen_state *cstate{};
    /* VM stack */
    valbuf<any_value> vmstack;
    /* ident stack */
    valbuf<ident_stack> idstack;
    /* per-alias stack pointer */
    std::unordered_map<
        int, alias_stack, std::hash<int>, std::equal_to<int>, astack_allocator
    > astacks;
    /* per-thread storage buffer for error messages */
    charbuf errbuf;
    /* we can attach a hook to vm events */
    hook_func call_hook{};
    /* whether we own the internal state (i.e. not a side thread */
    bool owner = false;
    /* thread ident flags */
    int ident_flags = 0;
    /* run depth limit */
    std::size_t max_run_depth = 0;
    /* current run depth */
    std::size_t run_depth = 0;
    /* loop nesting level */
    std::size_t loop_level = 0;

    thread_state(internal_state *cs);

    hook_func set_hook(hook_func f);

    hook_func &get_hook() { return call_hook; }
    hook_func const &get_hook() const { return call_hook; }

    alias_stack &get_astack(alias *a);
    alias_stack const &get_astack(alias const *a);
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_THREAD_HH */
