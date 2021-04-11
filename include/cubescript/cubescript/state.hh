#ifndef LIBCUBESCRIPT_CUBESCRIPT_STATE_HH
#define LIBCUBESCRIPT_CUBESCRIPT_STATE_HH

#include <cstddef>
#include <span>
#include <utility>
#include <string_view>

#include "callable.hh"
#include "ident.hh"
#include "value.hh"

namespace cubescript {

struct state;

using alloc_func   = void *(*)(void *, void *, size_t, size_t);

using hook_func    = internal::callable<void, struct state &>;
using command_func = internal::callable<
    void, state &, std::span<any_value>, any_value &
>;

enum {
    LIB_MATH   = 1 << 0,
    LIB_STRING = 1 << 1,
    LIB_LIST   = 1 << 2,
    LIB_ALL    = 0b111
};

enum class loop_state {
    NORMAL = 0, BREAK, CONTINUE
};

struct LIBCUBESCRIPT_EXPORT state {
    state();
    state(alloc_func func, void *data);
    virtual ~state();

    state(state const &) = delete;
    state(state &&s);

    state &operator=(state const &) = delete;
    state &operator=(state &&s);

    void swap(state &s);

    state new_thread();

    template<typename F>
    hook_func set_call_hook(F &&f) {
        return set_call_hook(
            hook_func{std::forward<F>(f), callable_alloc, this}
        );
    }
    hook_func const &get_call_hook() const;
    hook_func &get_call_hook();

    void init_libs(int libs = LIB_ALL);

    void clear_override(ident &id);
    void clear_overrides();

    integer_var &new_ivar(
        std::string_view n, integer_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    float_var &new_fvar(
        std::string_view n, float_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    string_var &new_svar(
        std::string_view n, std::string_view v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    ident &new_ident(std::string_view n);

    void reset_var(std::string_view name);
    void touch_var(std::string_view name);

    template<typename F>
    command &new_command(
        std::string_view name, std::string_view args, F &&f
    ) {
        return new_command(
            name, args,
            command_func{std::forward<F>(f), callable_alloc, this}
        );
    }

    ident *get_ident(std::string_view name);
    alias *get_alias(std::string_view name);
    bool have_ident(std::string_view name);

    std::span<ident *> get_idents();
    std::span<ident const *> get_idents() const;

    any_value run(bcode_ref const &code);
    any_value run(std::string_view code);
    any_value run(std::string_view code, std::string_view source);
    any_value run(ident &id, std::span<any_value> args);

    loop_state run_loop(bcode_ref const &code, any_value &ret);
    loop_state run_loop(bcode_ref const &code);

    bool get_override_mode() const;
    bool set_override_mode(bool v);

    bool get_persist_mode() const;
    bool set_persist_mode(bool v);

    std::size_t get_max_run_depth() const;
    std::size_t set_max_run_depth(std::size_t v);

    void set_alias(std::string_view name, any_value v);

private:
    friend struct state_p;

    LIBCUBESCRIPT_LOCAL state(void *is);

    hook_func set_call_hook(hook_func func);

    command &new_command(
        std::string_view name, std::string_view args, command_func func
    );

    static void *callable_alloc(
        void *data, void *p, std::size_t os, std::size_t ns
    ) {
        return static_cast<state *>(data)->alloc(p, os, ns);
    }

    void *alloc(void *ptr, size_t olds, size_t news);

    struct thread_state *p_tstate = nullptr;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_STATE_HH */
