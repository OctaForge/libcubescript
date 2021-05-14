#ifndef LIBCUBESCRIPT_THREAD_HH
#define LIBCUBESCRIPT_THREAD_HH

#include <cubescript/cubescript.hh>

#include <utility>

#include "cs_std.hh"
#include "cs_state.hh"
#include "cs_ident.hh"

namespace cubescript {

struct ident_level {
    ident &id;
    std::bitset<MAX_ARGUMENTS> usedargs{};

    ident_level(ident &i): id{i} {};
};

struct thread_state {
    using astack_allocator = std_allocator<std::pair<int const, alias_stack>>;
    /* the shared state pointer */
    internal_state *istate{};
    /* the public state interface */
    state *pstate{};
    /* VM stack */
    valbuf<any_value> vmstack;
    /* ident stack */
    valbuf<ident_stack> idstack;
    /* call stack */
    valbuf<ident_level> callstack;
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
    /* call depth limit */
    std::size_t max_call_depth = 1024;
    /* current call depth */
    std::size_t call_depth = 0;
    /* loop nesting level */
    std::size_t loop_level = 0;
    /* debug info */
    std::string_view source{};
    std::size_t *current_line = nullptr;

    thread_state(internal_state *cs);

    hook_func set_hook(hook_func f);

    hook_func &get_hook() { return call_hook; }
    hook_func const &get_hook() const { return call_hook; }

    alias_stack &get_astack(alias const *a);

    char *request_errbuf(std::size_t bufs, char *&sp);
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_THREAD_HH */
