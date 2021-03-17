#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <stdio.h>
#include <stdlib.h>

#include <vector>
#include <optional>
#include <functional>
#include <type_traits>
#include <utility>

#include "cubescript_conf.hh"

#include <ostd/platform.hh>
#include <ostd/string.hh>
#include <ostd/range.hh>
#include <ostd/io.hh>
#include <ostd/format.hh>

namespace cscript {

using cs_string = std::string;

static_assert(std::is_integral_v<cs_int>, "cs_int must be integral");
static_assert(std::is_signed_v<cs_int>, "cs_int must be signed");
static_assert(std::is_floating_point_v<cs_float>, "cs_float must be floating point");

struct cs_internal_error: std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum {
    CS_IDF_PERSIST    = 1 << 0,
    CS_IDF_OVERRIDE   = 1 << 1,
    CS_IDF_HEX        = 1 << 2,
    CS_IDF_READONLY   = 1 << 3,
    CS_IDF_OVERRIDDEN = 1 << 4,
    CS_IDF_UNKNOWN    = 1 << 5,
    CS_IDF_ARG        = 1 << 6
};

struct cs_bcode;
struct cs_value;
struct cs_state;
struct cs_shared_state;

struct OSTD_EXPORT cs_bcode_ref {
    cs_bcode_ref():
        p_code(nullptr)
    {}
    cs_bcode_ref(cs_bcode *v);
    cs_bcode_ref(cs_bcode_ref const &v);
    cs_bcode_ref(cs_bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~cs_bcode_ref();

    cs_bcode_ref &operator=(cs_bcode_ref const &v);
    cs_bcode_ref &operator=(cs_bcode_ref &&v);

    operator bool() const { return p_code != nullptr; }
    operator cs_bcode *() const { return p_code; }

private:
    cs_bcode *p_code;
};

OSTD_EXPORT bool cs_code_is_empty(cs_bcode *code);

struct OSTD_EXPORT cs_strref {
    friend struct cs_value;

    cs_strref() = delete;
    cs_strref(cs_shared_state &cs, ostd::string_range str);

    cs_strref(cs_strref const &ref);

    ~cs_strref();

    cs_strref &operator=(cs_strref const &ref);

    operator ostd::string_range() const;

private:
    /* for internal use only */
    cs_strref(char const *p, cs_shared_state &cs);

    cs_shared_state *p_state;
    char const *p_str;
};

enum class cs_value_type {
    Null = 0, Int, Float, String, Code, Ident
};

struct OSTD_EXPORT cs_value {
    cs_value() = delete;
    ~cs_value();

    cs_value(cs_state &);
    cs_value(cs_shared_state &);

    cs_value(cs_value const &);

    cs_value &operator=(cs_value const &);

    cs_value_type get_type() const;

    void set_int(cs_int val);
    void set_float(cs_float val);
    void set_str(ostd::string_range val);
    void set_null();
    void set_code(cs_bcode *val);
    void set_ident(cs_ident *val);

    cs_string get_str() const;
    ostd::string_range get_strr() const;
    cs_int get_int() const;
    cs_float get_float() const;
    cs_bcode *get_code() const;
    cs_ident *get_ident() const;
    void get_val(cs_value &r) const;

    bool get_bool() const;

    void force_null();
    cs_float force_float();
    cs_int force_int();
    ostd::string_range force_str();

    bool code_is_empty() const;

private:
    template<typename T>
    struct stor_t {
        cs_shared_state *state;
        T val;
    };

    cs_shared_state *state() const {
        return reinterpret_cast<stor_t<void *> const *>(&p_stor)->state;
    }

    std::aligned_union_t<1,
        stor_t<cs_int>,
        stor_t<cs_float>,
        stor_t<void *>,
        cs_strref
    > p_stor;
    size_t p_len;
    cs_value_type p_type;
};

struct cs_ident_stack {
    cs_value val_s;
    cs_ident_stack *next;

    cs_ident_stack(cs_state &cs): val_s{cs}, next{nullptr} {}
};

struct cs_error;
struct cs_gen_state;

enum class cs_ident_type {
    Ivar = 0, Fvar, Svar, Command, Alias, Special
};

struct cs_var;
struct cs_ivar;
struct cs_fvar;
struct cs_svar;
struct cs_alias;
struct cs_command;

struct OSTD_EXPORT cs_ident {
    friend struct cs_state;
    friend struct cs_shared_state;

    cs_ident() = delete;
    cs_ident(cs_ident const &) = delete;
    cs_ident(cs_ident &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~cs_ident() {};

    cs_ident &operator=(cs_ident const &) = delete;
    cs_ident &operator=(cs_ident &&) = delete;

    cs_ident_type get_type() const;
    ostd::string_range get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    cs_alias *get_alias();
    cs_alias const *get_alias() const;

    bool is_command() const;
    cs_command *get_command();
    cs_command const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    cs_var *get_var();
    cs_var const *get_var() const;

    bool is_ivar() const;
    cs_ivar *get_ivar();
    cs_ivar const *get_ivar() const;

    bool is_fvar() const;
    cs_fvar *get_fvar();
    cs_fvar const *get_fvar() const;

    bool is_svar() const;
    cs_svar *get_svar();
    cs_svar const *get_svar() const;

    int get_type_raw() const {
        return p_type;
    }

protected:
    cs_ident(cs_ident_type tp, ostd::string_range name, int flags = 0);

    cs_string p_name;
    /* represents the cs_ident_type above, but internally it has a wider variety
     * of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

private:
    int p_index = -1;
};

struct OSTD_EXPORT cs_var: cs_ident {
    friend struct cs_state;
    friend struct cs_shared_state;

protected:
    cs_var(cs_ident_type tp, ostd::string_range name, cs_var_cb func, int flags = 0);

private:
    cs_var_cb cb_var;

    virtual cs_string to_printable() const = 0;

    void changed(cs_state &cs) {
        if (cb_var) {
            cb_var(cs, *this);
        }
    }
};

struct OSTD_EXPORT cs_ivar: cs_var {
    friend struct cs_state;
    friend struct cs_shared_state;

    cs_int get_val_min() const;
    cs_int get_val_max() const;

    cs_int get_value() const;
    void set_value(cs_int val);

    cs_string to_printable() const final;

private:
    cs_ivar(
        ostd::string_range n, cs_int m, cs_int x, cs_int v, cs_var_cb f, int flags
    );

    cs_int p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT cs_fvar: cs_var {
    friend struct cs_state;
    friend struct cs_shared_state;

    cs_float get_val_min() const;
    cs_float get_val_max() const;

    cs_float get_value() const;
    void set_value(cs_float val);

    cs_string to_printable() const final;

private:
    cs_fvar(
        ostd::string_range n, cs_float m, cs_float x, cs_float v,
        cs_var_cb f, int flags
    );

    cs_float p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT cs_svar: cs_var {
    friend struct cs_state;
    friend struct cs_shared_state;

    ostd::string_range get_value() const;
    void set_value(cs_string val);

    cs_string to_printable() const final;

private:
    cs_svar(ostd::string_range n, cs_string v, cs_var_cb f, int flags);

    cs_string p_storage, p_overrideval;
};

struct OSTD_EXPORT cs_alias: cs_ident {
    friend struct cs_state;
    friend struct cs_shared_state;
    friend struct cs_alias_internal;

    cs_value get_value() const {
        return p_val;
    }

    void get_cstr(cs_value &v) const;
    void get_cval(cs_value &v) const;
private:
    cs_alias(cs_state &cs, ostd::string_range n, cs_string a, int flags);
    cs_alias(cs_state &cs, ostd::string_range n, cs_int a, int flags);
    cs_alias(cs_state &cs, ostd::string_range n, cs_float a, int flags);
    cs_alias(cs_state &cs, ostd::string_range n, int flags);
    cs_alias(cs_state &cs, ostd::string_range n, cs_value v, int flags);

    cs_bcode *p_acode;
    cs_ident_stack *p_astack;
    cs_value p_val;
};

struct cs_command: cs_ident {
    friend struct cs_state;
    friend struct cs_shared_state;
    friend struct cs_cmd_internal;

    ostd::string_range get_args() const;
    int get_num_args() const;

private:
    cs_command(
        ostd::string_range name, ostd::string_range args,
        int numargs, cs_command_cb func
    );

    cs_string p_cargs;
    cs_command_cb p_cb_cftv;
    int p_numargs;
};

struct cs_identLink;

enum {
    CsLibMath   = 1 << 0,
    CsLibString = 1 << 1,
    CsLibList   = 1 << 2,
    CsLibAll    = 0b111
};

enum class CsLoopState {
    Normal = 0, Break, Continue
};

static inline void *cs_default_alloc(void *, void *p, size_t, size_t ns) {
    if (!ns) {
        delete[] static_cast<unsigned char *>(p);
        return nullptr;
    }
    return new unsigned char[ns];
}

struct OSTD_EXPORT cs_state {
    friend struct cs_error;
    friend struct cs_strman;
    friend struct cs_value;
    friend struct cs_gen_state;

    cs_shared_state *p_state;
    cs_identLink *p_callstack = nullptr;

    int identflags = 0;

    cs_state(cs_alloc_cb func = cs_default_alloc, void *data = nullptr);
    virtual ~cs_state();

    cs_state(cs_state const &) = delete;
    cs_state(cs_state &&s) {
        swap(s);
    }

    cs_state &operator=(cs_state const &) = delete;
    cs_state &operator=(cs_state &&s) {
        swap(s);
        s.destroy();
        return *this;
    }

    void destroy();

    void swap(cs_state &s) {
        std::swap(p_state, s.p_state);
        std::swap(p_callstack, s.p_callstack);
        std::swap(identflags, s.identflags);
        std::swap(p_pstate, s.p_pstate);
        std::swap(p_inloop, s.p_inloop);
        std::swap(p_owner, s.p_owner);
        std::swap(p_callhook, s.p_callhook);
    }

    cs_state new_thread();

    cs_hook_cb set_call_hook(cs_hook_cb func);
    cs_hook_cb const &get_call_hook() const;
    cs_hook_cb &get_call_hook();

    void init_libs(int libs = CsLibAll);

    void clear_override(cs_ident &id);
    void clear_overrides();

    cs_ident *new_ident(ostd::string_range name, int flags = CS_IDF_UNKNOWN);
    cs_ident *force_ident(cs_value &v);

    cs_ivar *new_ivar(
        ostd::string_range n, cs_int m, cs_int x, cs_int v,
        cs_var_cb f = cs_var_cb(), int flags = 0
    );
    cs_fvar *new_fvar(
        ostd::string_range n, cs_float m, cs_float x, cs_float v,
        cs_var_cb f = cs_var_cb(), int flags = 0
    );
    cs_svar *new_svar(
        ostd::string_range n, cs_string v,
        cs_var_cb f = cs_var_cb(), int flags = 0
    );

    cs_command *new_command(
        ostd::string_range name, ostd::string_range args, cs_command_cb func
    );

    cs_ident *get_ident(ostd::string_range name);
    cs_alias *get_alias(ostd::string_range name);
    bool have_ident(ostd::string_range name);

    cs_ident_r get_idents();
    cs_const_ident_r get_idents() const;

    void reset_var(ostd::string_range name);
    void touch_var(ostd::string_range name);

    cs_string run_str(cs_bcode *code);
    cs_string run_str(ostd::string_range code);
    cs_string run_str(cs_ident *id, cs_value_r args);

    cs_int run_int(cs_bcode *code);
    cs_int run_int(ostd::string_range code);
    cs_int run_int(cs_ident *id, cs_value_r args);

    cs_float run_float(cs_bcode *code);
    cs_float run_float(ostd::string_range code);
    cs_float run_float(cs_ident *id, cs_value_r args);

    bool run_bool(cs_bcode *code);
    bool run_bool(ostd::string_range code);
    bool run_bool(cs_ident *id, cs_value_r args);

    void run(cs_bcode *code, cs_value &ret);
    void run(ostd::string_range code, cs_value &ret);
    void run(cs_ident *id, cs_value_r args, cs_value &ret);

    void run(cs_bcode *code);
    void run(ostd::string_range code);
    void run(cs_ident *id, cs_value_r args);

    CsLoopState run_loop(cs_bcode *code, cs_value &ret);
    CsLoopState run_loop(cs_bcode *code);

    bool is_in_loop() const {
        return p_inloop;
    }

    std::optional<cs_string> run_file_str(ostd::string_range fname);
    std::optional<cs_int> run_file_int(ostd::string_range fname);
    std::optional<cs_float> run_file_float(ostd::string_range fname);
    std::optional<bool> run_file_bool(ostd::string_range fname);
    bool run_file(ostd::string_range fname, cs_value &ret);
    bool run_file(ostd::string_range fname);

    void set_alias(ostd::string_range name, cs_value v);

    void set_var_int(
        ostd::string_range name, cs_int v,
        bool dofunc = true, bool doclamp = true
    );
    void set_var_float(
        ostd::string_range name, cs_float v,
        bool dofunc  = true, bool doclamp = true
    );
    void set_var_str(
        ostd::string_range name, ostd::string_range v, bool dofunc = true
    );

    void set_var_int_checked(cs_ivar *iv, cs_int v);
    void set_var_int_checked(cs_ivar *iv, cs_value_r args);
    void set_var_float_checked(cs_fvar *fv, cs_float v);
    void set_var_str_checked(cs_svar *fv, ostd::string_range v);

    std::optional<cs_int> get_var_int(ostd::string_range name);
    std::optional<cs_float> get_var_float(ostd::string_range name);
    std::optional<cs_string> get_var_str(ostd::string_range name);

    std::optional<cs_int> get_var_min_int(ostd::string_range name);
    std::optional<cs_int> get_var_max_int(ostd::string_range name);

    std::optional<cs_float> get_var_min_float(ostd::string_range name);
    std::optional<cs_float> get_var_max_float(ostd::string_range name);

    std::optional<cs_string> get_alias_val(ostd::string_range name);

    virtual void print_var(cs_var *v);

private:
    OSTD_LOCAL cs_state(cs_shared_state *s);

    cs_ident *add_ident(cs_ident *id);

    OSTD_LOCAL void *alloc(void *ptr, size_t olds, size_t news);

    cs_gen_state *p_pstate = nullptr;
    int p_inloop = 0;
    bool p_owner = false;

    char p_errbuf[512];

    cs_hook_cb p_callhook;
};

struct cs_stack_state_node {
    cs_stack_state_node const *next;
    cs_ident const *id;
    int index;
};

struct cs_stack_state {
    cs_stack_state() = delete;
    cs_stack_state(cs_state &cs, cs_stack_state_node *nd = nullptr, bool gap = false);
    cs_stack_state(cs_stack_state const &) = delete;
    cs_stack_state(cs_stack_state &&st);
    ~cs_stack_state();

    cs_stack_state &operator=(cs_stack_state const &) = delete;
    cs_stack_state &operator=(cs_stack_state &&);

    cs_stack_state_node const *get() const;
    bool gap() const;

private:
    cs_state &p_state;
    cs_stack_state_node *p_node;
    bool p_gap;
};

struct cs_error {
    friend struct cs_state;

    cs_error() = delete;
    cs_error(cs_error const &) = delete;
    cs_error(cs_error &&v):
        p_errmsg(v.p_errmsg), p_stack(std::move(v.p_stack))
    {}

    ostd::string_range what() const {
        return p_errmsg;
    }

    cs_stack_state &get_stack() {
        return p_stack;
    }

    cs_stack_state const &get_stack() const {
        return p_stack;
    }

    cs_error(cs_state &cs, ostd::string_range msg):
        p_errmsg(), p_stack(cs)
    {
        p_errmsg = save_msg(cs, msg);
        p_stack = save_stack(cs);
    }

    template<typename ...A>
    cs_error(cs_state &cs, ostd::string_range msg, A &&...args):
        p_errmsg(), p_stack(cs)
    {
        try {
            char fbuf[512];
            auto ret = ostd::format(
                ostd::counting_sink(ostd::char_range(fbuf, fbuf + sizeof(fbuf))),
                msg, std::forward<A>(args)...
            ).get_written();
            p_errmsg = save_msg(cs, ostd::char_range(fbuf, fbuf + ret));
        } catch (...) {
            p_errmsg = save_msg(cs, msg);
        }
        p_stack = save_stack(cs);
    }

private:
    cs_stack_state save_stack(cs_state &cs);
    ostd::string_range save_msg(cs_state &cs, ostd::string_range v);

    ostd::string_range p_errmsg;
    cs_stack_state p_stack;
};

struct OSTD_EXPORT cs_stacked_value: cs_value {
    cs_stacked_value(cs_state &cs, cs_ident *id = nullptr);
    ~cs_stacked_value();

    cs_stacked_value(cs_stacked_value const &) = delete;
    cs_stacked_value(cs_stacked_value &&) = delete;

    cs_stacked_value &operator=(cs_stacked_value const &) = delete;
    cs_stacked_value &operator=(cs_stacked_value &&v) = delete;

    cs_stacked_value &operator=(cs_value const &v);
    cs_stacked_value &operator=(cs_value &&v);

    bool set_alias(cs_ident *id);
    cs_alias *get_alias() const;
    bool has_alias() const;

    bool push();
    bool pop();

private:
    cs_alias *p_a;
    cs_ident_stack p_stack;
    bool p_pushed;
};

namespace util {
    template<typename R>
    inline R &&escape_string(R &&writer, ostd::string_range str) {
        using namespace ostd::string_literals;
        writer.put('"');
        for (; !str.empty(); str.pop_front()) {
            switch (str.front()) {
                case '\n':
                    ostd::range_put_all(writer, "^n"_sr);
                    break;
                case '\t':
                    ostd::range_put_all(writer, "^t"_sr);
                    break;
                case '\f':
                    ostd::range_put_all(writer, "^f"_sr);
                    break;
                case '"':
                    ostd::range_put_all(writer, "^\""_sr);
                    break;
                case '^':
                    ostd::range_put_all(writer, "^^"_sr);
                    break;
                default:
                    writer.put(str.front());
                    break;
            }
        }
        writer.put('"');
        return std::forward<R>(writer);
    }

    template<typename R>
    inline R &&unescape_string(R &&writer, ostd::string_range str) {
        for (; !str.empty(); str.pop_front()) {
            if (str.front() == '^') {
                str.pop_front();
                if (str.empty()) {
                    break;
                }
                switch (str.front()) {
                    case 'n':
                        writer.put('\n');
                        break;
                    case 't':
                        writer.put('\r');
                        break;
                    case 'f':
                        writer.put('\f');
                        break;
                    case '"':
                        writer.put('"');
                        break;
                    case '^':
                        writer.put('^');
                        break;
                    default:
                        writer.put(str.front());
                        break;
                }
            } else if (str.front() == '\\') {
                str.pop_front();
                if (str.empty()) {
                    break;
                }
                char c = str.front();
                if ((c == '\r') || (c == '\n')) {
                    if (!str.empty() && (c == '\r') && (str.front() == '\n')) {
                        str.pop_front();
                    }
                    continue;
                }
                writer.put('\\');
            } else {
                writer.put(str.front());
            }
        }
        return std::forward<R>(writer);
    }

    OSTD_EXPORT ostd::string_range parse_string(
        cs_state &cs, ostd::string_range str, size_t &nlines
    );

    inline ostd::string_range parse_string(
        cs_state &cs, ostd::string_range str
    ) {
        size_t nlines;
        return parse_string(cs, str, nlines);
    }

    OSTD_EXPORT ostd::string_range parse_word(
        cs_state &cs, ostd::string_range str
    );

    struct list_range;

    struct OSTD_EXPORT list_parser {
        list_parser() = delete;
        list_parser(cs_state &cs, ostd::string_range src):
            p_state(cs), p_input(src)
        {}

        void skip();
        bool parse();
        size_t count();

        template<typename R>
        R &&get_item(R &&writer) const {
            if (!p_quote.empty() && (*p_quote == '"')) {
                return unescape_string(std::forward<R>(writer), p_item);
            } else {
                ostd::range_put_all(writer, p_item);
                return std::forward<R>(writer);
            }
        }

        cs_string get_item() const {
            return std::move(get_item(ostd::appender<cs_string>()).get());
        }

        ostd::string_range &get_raw_item(bool quoted = false) {
            return quoted ? p_quote : p_item;
        }

        ostd::string_range const &get_raw_item(bool quoted = false) const {
            return quoted ? p_quote : p_item;
        }

        ostd::string_range &get_input() {
            return p_input;
        }

        list_range iter() noexcept;

private:
        ostd::string_range p_quote = ostd::string_range();
        ostd::string_range p_item = ostd::string_range();
        cs_state &p_state;
        ostd::string_range p_input;
    };

    struct list_range: ostd::input_range<list_range> {
        using range_category = ostd::forward_range_tag;
        using value_type     = ostd::string_range;
        using reference      = ostd::string_range;
        using size_type      = std::size_t;

        list_range() = delete;

        list_range(list_parser &p) noexcept: p_parser(&p) {
            pop_front();
        }

        bool empty() const noexcept {
            return !bool(p_item);
        }

        void pop_front() noexcept {
            if (p_parser->parse()) {
                p_item = p_parser->get_item();
            } else {
                p_item.reset();
            }
        }

        ostd::string_range front() const noexcept {
            return *p_item;
        }

    private:
        list_parser *p_parser;
        std::optional<cs_string> p_item{};
    };

    inline list_range list_parser::iter() noexcept {
        return list_range{*this};
    }

    template<typename R>
    inline void format_int(R &&writer, cs_int val) {
        try {
            ostd::format(std::forward<R>(writer), IntFormat, val);
        } catch (ostd::format_error const &e) {
            throw cs_internal_error{e.what()};
        }
    }

    template<typename R>
    inline void format_float(R &&writer, cs_float val) {
        try {
            ostd::format(
                std::forward<R>(writer),
                (val == cs_int(val)) ? RoundFloatFormat : FloatFormat, val
            );
        } catch (ostd::format_error const &e) {
            throw cs_internal_error{e.what()};
        }
    }

    template<typename R>
    inline void tvals_concat(
        R &&writer, cs_value_r vals,
        ostd::string_range sep = ostd::string_range()
    ) {
        for (size_t i = 0; i < vals.size(); ++i) {
            switch (vals[i].get_type()) {
                case cs_value_type::Int: {
                    format_int(
                        std::forward<R>(writer), vals[i].get_int()
                    );
                    break;
                }
                case cs_value_type::Float: {
                    format_float(
                        std::forward<R>(writer), vals[i].get_float()
                    );
                    break;
                }
                case cs_value_type::String: {
                    ostd::range_put_all(writer, vals[i].get_strr());
                    break;
                }
                default:
                    break;
            }
            if (i == (vals.size() - 1)) {
                break;
            }
            ostd::range_put_all(writer, sep);
        }
    }

    template<typename R>
    inline void print_stack(R &&writer, cs_stack_state const &st) {
        auto nd = st.get();
        while (nd) {
            try {
                ostd::format(
                    std::forward<R>(writer),
                    ((nd->index == 1) && st.gap())
                        ? "  ..%d) %s" : "  %d) %s",
                    nd->index, nd->id->get_name()
                );
            } catch (ostd::format_error const &e) {
                throw cs_internal_error{e.what()};
            }
            nd = nd->next;
            if (nd) {
                writer.put('\n');
            }
        }
    }
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
