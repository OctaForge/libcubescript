#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <cstring>
#include <cstddef>
#include <vector>
#include <optional>
#include <functional>
#include <type_traits>
#include <algorithm>
#include <memory>
#include <utility>
#include <span>
#include <new>

#include "cubescript_conf.hh"

#if defined(__CYGWIN__) || (defined(_WIN32) && !defined(_XBOX_VER))
#  ifdef LIBCUBESCRIPT_DLL
#    ifdef LIBCUBESCRIPT_BUILD
#      define LIBCUBESCRIPT_EXPORT __declspec(dllexport)
#    else
#      define LIBCUBESCRIPT_EXPORT __declspec(dllimport)
#    endif
#  else
#    define LIBCUBESCRIPT_EXPORT
#  endif
#  define LIBCUBESCRIPT_LOCAL
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LIBCUBESCRIPT_EXPORT __attribute__((visibility("default")))
#    define LIBCUBESCRIPT_LOCAL __attribute__((visibility("hidden")))
#  else
#    define LIBCUBESCRIPT_EXPORT
#    define LIBCUBESCRIPT_LOCAL
#  endif
#endif

namespace cubescript {

static_assert(std::is_integral_v<integer_type>, "integer_type must be integral");
static_assert(std::is_signed_v<integer_type>, "integer_type must be signed");
static_assert(std::is_floating_point_v<float_type>, "float_type must be floating point");

struct internal_error: std::runtime_error {
    using std::runtime_error::runtime_error;
};

template<typename R, typename ...A>
struct callable {
private:
    struct base {
        base(base const &);
        base &operator=(base const &);

    public:
        base() {}
        virtual ~base() {}
        virtual void move_to(base *) = 0;
        virtual R operator()(A &&...args) = 0;
    };

    template<typename F>
    struct store: base {
        explicit store(F &&f): p_stor{std::move(f)} {}

        virtual void move_to(base *p) {
            ::new (p) store{std::move(p_stor)};
        }

        virtual R operator()(A &&...args) {
            return std::invoke(*std::launder(
                reinterpret_cast<F *>(&p_stor)
            ), std::forward<A>(args)...);
        }

    private:
        F p_stor;
    };

    using alloc_f = void *(*)(void *, void *, std::size_t, std::size_t);

    struct f_alloc {
        alloc_f af;
        void *ud;
        size_t asize;
    };

    std::aligned_storage_t<sizeof(void *) * 4> p_stor;
    base *p_func;

    static inline base *as_base(void *p) {
        return static_cast<base *>(p);
    }

    template<typename T>
    static inline bool f_not_null(T const &) { return true; }

    template<typename T>
    static inline bool f_not_null(T *p) { return !!p; }

    template<typename CR, typename C>
    static inline bool f_not_null(CR C::*p) { return !!p; }

    template<typename T>
    static inline bool f_not_null(callable<T> const &f) { return !!f; }

    bool small_storage() {
        return (static_cast<void *>(p_func) == &p_stor);
    }

    void cleanup() {
        if (!p_func) {
            return;
        }
        p_func->~base();
        if (!small_storage()) {
            auto &ad = *std::launder(reinterpret_cast<f_alloc *>(&p_stor));
            ad.af(ad.ud, p_func, ad.asize, 0);
        }
    }

public:
    callable() noexcept: p_func{nullptr} {}
    callable(std::nullptr_t) noexcept: p_func{nullptr} {}
    callable(std::nullptr_t, alloc_f, void *) noexcept: p_func{nullptr} {}

    callable(callable &&f) noexcept {
        if (!f.p_func) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
    }

    template<typename F>
    callable(F func, alloc_f af, void *ud) {
        if (!f_not_null(func)) {
            return;
        }
        if constexpr (sizeof(store<F>) <= sizeof(p_stor)) {
            auto *p = static_cast<void *>(&p_stor);
            p_func = ::new (p) store<F>{std::move(func)};
        } else {
            auto &ad = *std::launder(reinterpret_cast<f_alloc *>(&p_stor));
            ad.af = af;
            ad.ud = ud;
            ad.asize = sizeof(store<F>);
            p_func = static_cast<store<F> *>(
                af(ud, nullptr, 0, sizeof(store<F>))
            );
            try {
                new (p_func) store<F>{std::move(func)};
            } catch (...) {
                af(ud, p_func, sizeof(store<F>), 0);
                throw;
            }
        }
    }

    callable &operator=(callable const &) = delete;

    callable &operator=(callable &&f) noexcept {
        cleanup();
        if (f.p_func == nullptr) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
        return *this;
    }

    callable &operator=(std::nullptr_t) noexcept {
        cleanup();
        p_func = nullptr;
        return *this;
    }

    template<typename F>
    callable &operator=(F &&func) {
        callable{std::forward<F>(func)}.swap(*this);
        return *this;
    }

    ~callable() {
        cleanup();
    }

    void swap(callable &f) noexcept {
        std::aligned_storage_t<sizeof(p_stor)> tmp_stor;
        if (small_storage() && f.small_storage()) {
            auto *t = as_base(&tmp_stor);
            p_func->move_to(t);
            p_func->~base();
            p_func = nullptr;
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = nullptr;
            p_func = as_base(&p_stor);
            t->move_to(as_base(&f.p_stor));
            t->~base();
            f.p_func = as_base(&f.p_stor);
        } else if (small_storage()) {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &f.p_stor, sizeof(tmp_stor));
            p_func->move_to(as_base(&f.p_stor));
            p_func->~base();
            p_func = f.p_func;
            f.p_func = as_base(&f.p_stor);
            memcpy(&p_stor, &tmp_stor, sizeof(tmp_stor));
        } else if (f.small_storage()) {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = p_func;
            p_func = as_base(&p_stor);
            memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
        } else {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            memcpy(&p_stor, &f.p_stor, sizeof(tmp_stor));
            memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
            std::swap(p_func, f.p_func);
        }
    }

    explicit operator bool() const noexcept {
        return !!p_func;
    }

    R operator()(A ...args) {
        return (*p_func)(std::forward<A>(args)...);
    }
};

using alloc_func = void *(*)(void *, void *, size_t, size_t);

struct state;
struct ident;
struct any_value;
struct global_var;

using hook_func      = callable<void, state &>;
using var_cb_func    = callable<void, state &, ident &>;
using var_print_func = callable<void, state const &, global_var const &>;
using command_func   = callable<
    void, state &, std::span<any_value>, any_value &
>;

enum {
    IDENT_FLAG_PERSIST    = 1 << 0,
    IDENT_FLAG_OVERRIDE   = 1 << 1,
    IDENT_FLAG_HEX        = 1 << 2,
    IDENT_FLAG_READONLY   = 1 << 3,
    IDENT_FLAG_OVERRIDDEN = 1 << 4,
    IDENT_FLAG_UNKNOWN    = 1 << 5,
    IDENT_FLAG_ARG        = 1 << 6
};

struct bcode;
struct internal_state;
struct thread_state;
struct ident_impl;

struct LIBCUBESCRIPT_EXPORT bcode_ref {
    bcode_ref():
        p_code(nullptr)
    {}
    bcode_ref(bcode *v);
    bcode_ref(bcode_ref const &v);
    bcode_ref(bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~bcode_ref();

    bcode_ref &operator=(bcode_ref const &v);
    bcode_ref &operator=(bcode_ref &&v);

    operator bool() const { return p_code != nullptr; }
    operator bcode *() const { return p_code; }

private:
    bcode *p_code;
};

LIBCUBESCRIPT_EXPORT bool code_is_empty(bcode *code);

struct LIBCUBESCRIPT_EXPORT string_ref {
    friend struct any_value;
    friend struct string_pool;

    string_ref() = delete;
    string_ref(internal_state *cs, std::string_view str);
    string_ref(state &cs, std::string_view str);

    string_ref(string_ref const &ref);

    ~string_ref();

    string_ref &operator=(string_ref const &ref);

    operator std::string_view() const;

    std::size_t size() const {
        return std::string_view{*this}.size();
    }
    std::size_t length() const {
        return std::string_view{*this}.length();
    }

    char const *data() const {
        return std::string_view{*this}.data();
    }

    bool operator==(string_ref const &s) const;

private:
    /* for internal use only */
    string_ref(char const *p, internal_state *cs);

    internal_state *p_state;
    char const *p_str;
};

enum class value_type {
    NONE = 0, INT, FLOAT, STRING, CODE, IDENT
};

struct LIBCUBESCRIPT_EXPORT any_value {
    any_value() = delete;
    ~any_value();

    any_value(state &);
    any_value(internal_state &);

    any_value(any_value const &);
    any_value(any_value &&v);

    any_value &operator=(any_value const &);
    any_value &operator=(any_value &&);

    value_type get_type() const;

    void set_int(integer_type val);
    void set_float(float_type val);
    void set_str(std::string_view val);
    void set_str(string_ref const &val);
    void set_none();
    void set_code(bcode *val);
    void set_ident(ident *val);

    string_ref get_str() const;
    integer_type get_int() const;
    float_type get_float() const;
    bcode *get_code() const;
    ident *get_ident() const;
    void get_val(any_value &r) const;

    bool get_bool() const;

    void force_none();
    float_type force_float();
    integer_type force_int();
    std::string_view force_str();
    bcode *force_code(state &cs);
    ident *force_ident(state &cs);

    bool code_is_empty() const;

private:
    template<typename T>
    struct stor_t {
        internal_state *state;
        T val;
    };

    internal_state *get_state() const {
        return std::launder(
            reinterpret_cast<stor_t<void *> const *>(&p_stor)
        )->state;
    }

    std::aligned_union_t<1,
        stor_t<integer_type>,
        stor_t<float_type>,
        stor_t<void *>,
        string_ref
    > p_stor;
    value_type p_type;
};

struct ident_stack {
    any_value val_s;
    ident_stack *next;

    ident_stack(state &cs): val_s{cs}, next{nullptr} {}
};

struct error;
struct codegen_state;

enum class ident_type {
    IVAR = 0, FVAR, SVAR, COMMAND, ALIAS, SPECIAL
};

struct global_var;
struct integer_var;
struct float_var;
struct string_var;
struct alias;
struct command;

struct LIBCUBESCRIPT_EXPORT ident {
    int get_raw_type() const;
    ident_type get_type() const;
    std::string_view get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    alias *get_alias();
    alias const *get_alias() const;

    bool is_command() const;
    command *get_command();
    command const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    global_var *get_var();
    global_var const *get_var() const;

    bool is_ivar() const;
    integer_var *get_ivar();
    integer_var const *get_ivar() const;

    bool is_fvar() const;
    float_var *get_fvar();
    float_var const *get_fvar() const;

    bool is_svar() const;
    string_var *get_svar();
    string_var const *get_svar() const;

protected:
    ident() = default;

private:
    friend struct state;

    ident_impl *p_impl{};
};

struct LIBCUBESCRIPT_EXPORT global_var: ident {
protected:
    global_var() = default;
};

struct LIBCUBESCRIPT_EXPORT integer_var: global_var {
    integer_type get_val_min() const;
    integer_type get_val_max() const;

    integer_type get_value() const;
    void set_value(integer_type val);

protected:
    integer_var() = default;
};

struct LIBCUBESCRIPT_EXPORT float_var: global_var {
    float_type get_val_min() const;
    float_type get_val_max() const;

    float_type get_value() const;
    void set_value(float_type val);

protected:
    float_var() = default;
};

struct LIBCUBESCRIPT_EXPORT string_var: global_var {
    string_ref get_value() const;
    void set_value(string_ref val);

protected:
    string_var() = default;
};

struct LIBCUBESCRIPT_EXPORT alias: ident {
    any_value get_value() const;

protected:
    alias() = default;
};

struct command: ident {
    std::string_view get_args() const;
    int get_num_args() const;

protected:
    command() = default;
};

struct ident_link;

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
    int identflags = 0;

    state();
    state(alloc_func func, void *data);
    virtual ~state();

    state(state const &) = delete;
    state(state &&s) {
        swap(s);
    }

    state &operator=(state const &) = delete;
    state &operator=(state &&s) {
        swap(s);
        s.destroy();
        return *this;
    }

    void destroy();

    void swap(state &s) {
        std::swap(p_tstate, s.p_tstate);
        std::swap(identflags, s.identflags);
    }

    state new_thread();

    template<typename F>
    hook_func set_call_hook(F &&f) {
        return std::move(set_call_hook(
            hook_func{std::forward<F>(f), callable_alloc, this}
        ));
    }
    hook_func const &get_call_hook() const;
    hook_func &get_call_hook();

    template<typename F>
    var_print_func set_var_printer(F &&f) {
        return std::move(set_var_printer(
            var_print_func{std::forward<F>(f), callable_alloc, this}
        ));
    }
    var_print_func const &get_var_printer() const;

    void init_libs(int libs = LIB_ALL);

    void clear_override(ident &id);
    void clear_overrides();

    ident *new_ident(std::string_view name, int flags = IDENT_FLAG_UNKNOWN);

    template<typename F>
    integer_var *new_ivar(
        std::string_view name, integer_type m, integer_type x, integer_type v,
        F &&f, int flags = 0
    ) {
        return new_ivar(
            name, m, x, v,
            var_cb_func{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    integer_var *new_ivar(
        std::string_view name, integer_type m, integer_type x, integer_type v
    ) {
        return new_ivar(name, m, x, v, var_cb_func{}, 0);
    }

    template<typename F>
    float_var *new_fvar(
        std::string_view name, float_type m, float_type x, float_type v,
        F &&f, int flags = 0
    ) {
        return new_fvar(
            name, m, x, v,
            var_cb_func{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    float_var *new_fvar(
        std::string_view name, float_type m, float_type x, float_type v
    ) {
        return new_fvar(name, m, x, v, var_cb_func{}, 0);
    }

    template<typename F>
    string_var *new_svar(
        std::string_view name, std::string_view v, F &&f, int flags = 0
    ) {
        return new_svar(
            name, v,
            var_cb_func{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    string_var *new_svar(std::string_view name, std::string_view v) {
        return new_svar(name, v, var_cb_func{}, 0);
    }

    template<typename F>
    command *new_command(
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

    void reset_var(std::string_view name);
    void touch_var(std::string_view name);

    void run(bcode *code, any_value &ret);
    void run(std::string_view code, any_value &ret);
    void run(std::string_view code, any_value &ret, std::string_view source);
    void run(ident *id, std::span<any_value> args, any_value &ret);

    any_value run(bcode *code);
    any_value run(std::string_view code);
    any_value run(std::string_view code, std::string_view source);
    any_value run(ident *id, std::span<any_value> args);

    loop_state run_loop(bcode *code, any_value &ret);
    loop_state run_loop(bcode *code);

    bool is_in_loop() const;

    void set_alias(std::string_view name, any_value v);

    void set_var_int(
        std::string_view name, integer_type v,
        bool dofunc = true, bool doclamp = true
    );
    void set_var_float(
        std::string_view name, float_type v,
        bool dofunc  = true, bool doclamp = true
    );
    void set_var_str(
        std::string_view name, std::string_view v, bool dofunc = true
    );

    void set_var_int_checked(integer_var *iv, integer_type v);
    void set_var_int_checked(integer_var *iv, std::span<any_value> args);
    void set_var_float_checked(float_var *fv, float_type v);
    void set_var_str_checked(string_var *fv, std::string_view v);

    std::optional<integer_type> get_var_int(std::string_view name);
    std::optional<float_type> get_var_float(std::string_view name);
    std::optional<string_ref> get_var_str(std::string_view name);

    std::optional<integer_type> get_var_min_int(std::string_view name);
    std::optional<integer_type> get_var_max_int(std::string_view name);

    std::optional<float_type> get_var_min_float(std::string_view name);
    std::optional<float_type> get_var_max_float(std::string_view name);

    std::optional<string_ref> get_alias_val(std::string_view name);

    void print_var(global_var const &v) const;

    thread_state *thread_pointer() {
        return p_tstate;
    }

    thread_state const *thread_pointer() const {
        return p_tstate;
    }

private:
    hook_func set_call_hook(hook_func func);
    var_print_func set_var_printer(var_print_func func);

    integer_var *new_ivar(
        std::string_view n, integer_type m, integer_type x, integer_type v,
        var_cb_func f, int flags
    );
    float_var *new_fvar(
        std::string_view n, float_type m, float_type x, float_type v,
        var_cb_func f, int flags
    );
    string_var *new_svar(
        std::string_view n, std::string_view v, var_cb_func f, int flags
    );

    command *new_command(
        std::string_view name, std::string_view args, command_func func
    );

    static void *callable_alloc(
        void *data, void *p, std::size_t os, std::size_t ns
    ) {
        return static_cast<state *>(data)->alloc(p, os, ns);
    }

    LIBCUBESCRIPT_LOCAL state(internal_state *s);

    ident *add_ident(ident *id, ident_impl *impl);

    void *alloc(void *ptr, size_t olds, size_t news);

    thread_state *p_tstate = nullptr;
};

struct stack_state {
    struct node {
        node const *next;
        ident const *id;
        int index;
    };

    stack_state() = delete;
    stack_state(thread_state &ts, node *nd = nullptr, bool gap = false);
    stack_state(stack_state const &) = delete;
    stack_state(stack_state &&st);
    ~stack_state();

    stack_state &operator=(stack_state const &) = delete;
    stack_state &operator=(stack_state &&);

    node const *get() const;
    bool gap() const;

private:
    thread_state &p_state;
    node *p_node;
    bool p_gap;
};

struct LIBCUBESCRIPT_EXPORT error {
    friend struct state;

    error() = delete;
    error(error const &) = delete;
    error(error &&v):
        p_errmsg(v.p_errmsg), p_stack(std::move(v.p_stack))
    {}

    std::string_view what() const {
        return p_errmsg;
    }

    stack_state &get_stack() {
        return p_stack;
    }

    stack_state const &get_stack() const {
        return p_stack;
    }

    template<typename ...A>
    error(state &cs, std::string_view msg, A const &...args):
        error{*cs.thread_pointer(), msg, args...}
    {}

    error(thread_state &ts, std::string_view msg):
        p_errmsg{}, p_stack{ts}
    {
        char *sp;
        char *buf = request_buf(ts, msg.size(), sp);
        std::memcpy(buf, msg.data(), msg.size());
        buf[msg.size()] = '\0';
        p_errmsg = std::string_view{sp, buf + msg.size()};
        p_stack = save_stack(ts);
    }

    template<typename ...A>
    error(thread_state &ts, std::string_view msg, A const &...args):
        p_errmsg{}, p_stack{ts}
    {
        std::size_t sz = msg.size() + 64;
        char *buf, *sp;
        for (;;) {
            buf = request_buf(ts, sz, sp);
            int written = std::snprintf(buf, sz, msg.data(), args...);
            if (written <= 0) {
                throw internal_error{"format error"};
            } else if (std::size_t(written) <= sz) {
                break;
            }
            sz = std::size_t(written);
        }
        p_errmsg = std::string_view{sp, buf + sz};
        p_stack = save_stack(ts);
    }

private:
    stack_state save_stack(thread_state &ts);
    char *request_buf(thread_state &ts, std::size_t bufs, char *&sp);

    std::string_view p_errmsg;
    stack_state p_stack;
};

struct LIBCUBESCRIPT_EXPORT stacked_value: any_value {
    stacked_value(state &cs, ident *id = nullptr);
    ~stacked_value();

    stacked_value(stacked_value const &) = delete;
    stacked_value(stacked_value &&) = delete;

    stacked_value &operator=(stacked_value const &) = delete;
    stacked_value &operator=(stacked_value &&v) = delete;

    stacked_value &operator=(any_value const &v);
    stacked_value &operator=(any_value &&v);

    bool set_alias(ident *id);
    alias *get_alias() const;
    bool has_alias() const;

    bool push();
    bool pop();

private:
    alias *p_a;
    ident_stack p_stack;
    bool p_pushed;
};

struct LIBCUBESCRIPT_EXPORT list_parser {
    list_parser(state &cs, std::string_view s = std::string_view{}):
        p_state{&cs}, p_input_beg{s.data()}, p_input_end{s.data() + s.size()}
     {}

    void set_input(std::string_view s) {
        p_input_beg = s.data();
        p_input_end = s.data() + s.size();
    }

    std::string_view get_input() const {
        return std::string_view{p_input_beg, p_input_end};
    }

    bool parse();
    std::size_t count();

    string_ref get_item() const;

    std::string_view get_raw_item() const { return p_item; }
    std::string_view get_quoted_item() const { return p_quoted_item; }

    void skip_until_item();

private:
    state *p_state;
    char const *p_input_beg, *p_input_end;

    std::string_view p_item{};
    std::string_view p_quoted_item{};
};


LIBCUBESCRIPT_EXPORT char const *parse_string(
    state &cs, std::string_view str, size_t &nlines
);

inline char const *parse_string(
    state &cs, std::string_view str
) {
    size_t nlines;
    return parse_string(cs, str, nlines);
}

LIBCUBESCRIPT_EXPORT char const *parse_word(
    state &cs, std::string_view str
);

LIBCUBESCRIPT_EXPORT string_ref concat_values(
    state &cs, std::span<any_value> vals,
    std::string_view sep = std::string_view{}
);

template<typename R>
inline R escape_string(R writer, std::string_view str) {
    *writer++ = '"';
    for (auto c: str) {
        switch (c) {
            case '\n': *writer++ = '^'; *writer++ = 'n'; break;
            case '\t': *writer++ = '^'; *writer++ = 't'; break;
            case '\f': *writer++ = '^'; *writer++ = 'f'; break;
            case  '"': *writer++ = '^'; *writer++ = '"'; break;
            case  '^': *writer++ = '^'; *writer++ = '^'; break;
            default: *writer++ = c; break;
        }
    }
    *writer++ = '"';
    return writer;
}

template<typename R>
inline R unescape_string(R writer, std::string_view str) {
    for (auto it = str.begin(); it != str.end(); ++it) {
        if (*it == '^') {
            ++it;
            if (it == str.end()) {
                break;
            }
            switch (*it) {
                case 'n': *writer++ = '\n'; break;
                case 't': *writer++ = '\r'; break;
                case 'f': *writer++ = '\f'; break;
                case '"': *writer++ = '"'; break;
                case '^': *writer++ = '^'; break;
                default: *writer++ = *it; break;
            }
        } else if (*it == '\\') {
            ++it;
            if (it == str.end()) {
                break;
            }
            char c = *it;
            if ((c == '\r') || (c == '\n')) {
                if ((c == '\r') && ((it + 1) != str.end())) {
                    if (it[1] == '\n') {
                        ++it;
                    }
                }
                continue;
            }
            *writer++ = '\\';
        } else {
            *writer++ = *it;
        }
    }
    return writer;
}

template<typename R>
inline R print_stack(R writer, stack_state const &st) {
    char buf[32] = {0};
    auto nd = st.get();
    while (nd) {
        auto name = nd->id->get_name();
        *writer++ = ' ';
        *writer++ = ' ';
        if ((nd->index == 1) && st.gap()) {
            *writer++ = '.';
            *writer++ = '.';
        }
        snprintf(buf, sizeof(buf), "%d", nd->index);
        char const *p = buf;
        std::copy(p, p + strlen(p), writer);
        *writer++ = ')';
        std::copy(name.begin(), name.end(), writer);
        nd = nd->next;
        if (nd) {
            *writer++ = '\n';
        }
    }
    return writer;
}

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
