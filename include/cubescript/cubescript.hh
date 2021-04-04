#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <cstring>
#include <cstddef>
#include <vector>
#include <optional>
#include <functional>
#include <type_traits>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <utility>
#include <span>
#include <new>

#include "cubescript_conf.hh"

#include "cubescript/callable.hh"

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

struct internal_error: std::runtime_error {
    using std::runtime_error::runtime_error;
};

using alloc_func   = void *(*)(void *, void *, size_t, size_t);

using hook_func    = callable<void, struct state &>;
using command_func = callable<
    void, struct state &, std::span<struct any_value>, struct any_value &
>;

struct internal_state;

struct LIBCUBESCRIPT_EXPORT bcode_ref {
    bcode_ref():
        p_code(nullptr)
    {}
    bcode_ref(struct bcode *v);
    bcode_ref(bcode_ref const &v);
    bcode_ref(bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~bcode_ref();

    bcode_ref &operator=(bcode_ref const &v);
    bcode_ref &operator=(bcode_ref &&v);

    bool empty() const;

    operator bool() const;
    operator struct bcode *() const;

private:
    struct bcode *p_code;
};

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

struct ident;

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
    void set_code(bcode_ref const &val);
    void set_ident(ident *val);

    string_ref get_str() const;
    integer_type get_int() const;
    float_type get_float() const;
    bcode_ref get_code() const;
    ident *get_ident() const;
    void get_val(any_value &r) const;

    bool get_bool() const;

    void force_none();
    float_type force_float();
    integer_type force_int();
    std::string_view force_str();
    bcode_ref force_code(state &cs);
    ident *force_ident(state &cs);

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

    bool is_overridden(state &cs) const;
    bool is_persistent(state &cs) const;

protected:
    ident() = default;

    friend struct internal_state;

    struct ident_impl *p_impl{};
};

enum class var_type {
    DEFAULT = 0,
    PERSISTENT,
    OVERRIDABLE
};

struct LIBCUBESCRIPT_EXPORT global_var: ident {
    bool is_read_only() const;
    bool is_overridable() const;

    var_type get_variable_type() const;

protected:
    global_var() = default;
};

struct LIBCUBESCRIPT_EXPORT integer_var: global_var {
    integer_type get_value() const;
    void set_value(integer_type val);

protected:
    integer_var() = default;
};

struct LIBCUBESCRIPT_EXPORT float_var: global_var {
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
    bool is_arg() const;

protected:
    alias() = default;
};

struct LIBCUBESCRIPT_EXPORT command: ident {
    std::string_view get_args() const;
    int get_num_args() const;

protected:
    command() = default;
};

enum {
    LIB_MATH   = 1 << 0,
    LIB_STRING = 1 << 1,
    LIB_LIST   = 1 << 2,
    LIB_ALL    = 0b111
};

enum class loop_state {
    NORMAL = 0, BREAK, CONTINUE
};

struct thread_state;

struct LIBCUBESCRIPT_EXPORT state {
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
    }

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

    integer_var *new_ivar(
        std::string_view n, integer_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    float_var *new_fvar(
        std::string_view n, float_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    string_var *new_svar(
        std::string_view n, std::string_view v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );
    void reset_var(std::string_view name);

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

    void run(bcode_ref const &code, any_value &ret);
    void run(std::string_view code, any_value &ret);
    void run(std::string_view code, any_value &ret, std::string_view source);
    void run(ident *id, std::span<any_value> args, any_value &ret);

    any_value run(bcode_ref const &code);
    any_value run(std::string_view code);
    any_value run(std::string_view code, std::string_view source);
    any_value run(ident *id, std::span<any_value> args);

    loop_state run_loop(bcode_ref const &code, any_value &ret);
    loop_state run_loop(bcode_ref const &code);

    bool is_in_loop() const;

    bool get_override_mode() const;
    bool set_override_mode(bool v);

    bool get_persist_mode() const;
    bool set_persist_mode(bool v);

    void set_alias(std::string_view name, any_value v);

    std::optional<string_ref> get_alias_val(std::string_view name);

    thread_state *thread_pointer() {
        return p_tstate;
    }

    thread_state const *thread_pointer() const {
        return p_tstate;
    }

private:
    hook_func set_call_hook(hook_func func);

    command *new_command(
        std::string_view name, std::string_view args, command_func func
    );

    static void *callable_alloc(
        void *data, void *p, std::size_t os, std::size_t ns
    ) {
        return static_cast<state *>(data)->alloc(p, os, ns);
    }

    LIBCUBESCRIPT_LOCAL state(internal_state *s);

    void *alloc(void *ptr, size_t olds, size_t news);

    thread_state *p_tstate = nullptr;
};

struct LIBCUBESCRIPT_EXPORT stack_state {
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
        p_errbeg{v.p_errbeg}, p_errend{v.p_errend},
        p_stack{std::move(v.p_stack)}
    {}

    std::string_view what() const {
        return std::string_view{p_errbeg, p_errend};
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
        p_errbeg{}, p_errend{}, p_stack{ts}
    {
        char *sp;
        char *buf = request_buf(ts, msg.size(), sp);
        std::memcpy(buf, msg.data(), msg.size());
        buf[msg.size()] = '\0';
        p_errbeg = sp;
        p_errend = buf + msg.size();
        p_stack = save_stack(ts);
    }

    template<typename ...A>
    error(thread_state &ts, std::string_view msg, A const &...args):
        p_errbeg{}, p_errend{}, p_stack{ts}
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
        p_errbeg = sp;
        p_errend = buf + sz;
        p_stack = save_stack(ts);
    }

private:
    stack_state save_stack(thread_state &ts);
    char *request_buf(thread_state &ts, std::size_t bufs, char *&sp);

    char const *p_errbeg, *p_errend;
    stack_state p_stack;
};

struct LIBCUBESCRIPT_EXPORT alias_local {
    alias_local(state &cs, ident *a);
    ~alias_local();

    alias_local(alias_local const &) = delete;
    alias_local(alias_local &&) = delete;

    alias_local &operator=(alias_local const &) = delete;
    alias_local &operator=(alias_local &&v) = delete;

    alias *get_alias() noexcept { return p_alias; }
    alias const *get_alias() const noexcept { return p_alias; }

    bool set(any_value val);

    explicit operator bool() const noexcept;

private:
    alias *p_alias;
    void *p_sp;
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

    std::string_view get_raw_item() const {
        return std::string_view{p_ibeg, p_iend};
    }
    std::string_view get_quoted_item() const {
        return std::string_view{p_qbeg, p_qend};
    }

    void skip_until_item();

private:
    state *p_state;
    char const *p_input_beg, *p_input_end;

    char const *p_ibeg{}, *p_iend{};
    char const *p_qbeg{}, *p_qend{};
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
