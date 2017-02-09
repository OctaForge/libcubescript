#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <stdio.h>
#include <stdlib.h>

#include <optional>
#include <functional>
#include <type_traits>

#include "cubescript_conf.hh"

#include <ostd/platform.hh>
#include <ostd/types.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/range.hh>
#include <ostd/utility.hh>
#include <ostd/io.hh>
#include <ostd/format.hh>

namespace cscript {

using CsString = std::string;

static_assert(std::is_integral_v<CsInt>, "CsInt must be integral");
static_assert(std::is_signed_v<CsInt>, "CsInt must be signed");
static_assert(std::is_floating_point_v<CsFloat>, "CsFloat must be floating point");

enum {
    CsIdfPersist    = 1 << 0,
    CsIdfOverride   = 1 << 1,
    CsIdfHex        = 1 << 2,
    CsIdfReadOnly   = 1 << 3,
    CsIdfOverridden = 1 << 4,
    CsIdfUnknown    = 1 << 5,
    CsIdfArg        = 1 << 6
};

struct CsBytecode;

struct OSTD_EXPORT CsBytecodeRef {
    CsBytecodeRef():
        p_code(nullptr)
    {}
    CsBytecodeRef(CsBytecode *v);
    CsBytecodeRef(CsBytecodeRef const &v);
    CsBytecodeRef(CsBytecodeRef &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~CsBytecodeRef();

    CsBytecodeRef &operator=(CsBytecodeRef const &v);
    CsBytecodeRef &operator=(CsBytecodeRef &&v);

    operator bool() const { return p_code != nullptr; }
    operator CsBytecode *() const { return p_code; }

private:
    CsBytecode *p_code;
};

OSTD_EXPORT bool cs_code_is_empty(CsBytecode *code);

enum class CsValueType {
    Null = 0, Int, Float, String, Cstring, Code, Macro, Ident
};

struct OSTD_EXPORT CsValue {
    CsValue();
    ~CsValue();

    CsValue(CsValue const &);
    CsValue(CsValue &&);

    CsValue &operator=(CsValue const &v);
    CsValue &operator=(CsValue &&v);

    CsValueType get_type() const;

    void set_int(CsInt val);
    void set_float(CsFloat val);
    void set_str(CsString val);
    void set_null();
    void set_code(CsBytecode *val);
    void set_cstr(ostd::ConstCharRange val);
    void set_ident(CsIdent *val);
    void set_macro(ostd::ConstCharRange val);

    CsString get_str() const;
    ostd::ConstCharRange get_strr() const;
    CsInt get_int() const;
    CsFloat get_float() const;
    CsBytecode *get_code() const;
    CsIdent *get_ident() const;
    void get_val(CsValue &r) const;

    bool get_bool() const;

    void force_null();
    CsFloat force_float();
    CsInt force_int();
    ostd::ConstCharRange force_str();

    bool code_is_empty() const;

private:
    std::aligned_union_t<1, CsInt, CsFloat, void *> p_stor;
    size_t p_len;
    CsValueType p_type;
};

struct CsIdentStack {
    CsValue val_s;
    CsIdentStack *next;
};

struct CsSharedState;
struct CsErrorException;
struct GenState;

enum class CsIdentType {
    Ivar = 0, Fvar, Svar, Command, Alias, Special
};

struct CsVar;
struct CsIvar;
struct CsFvar;
struct CsSvar;
struct CsAlias;
struct CsCommand;

struct OSTD_EXPORT CsIdent {
    friend struct CsState;
    friend struct CsSharedState;

    CsIdent() = delete;
    CsIdent(CsIdent const &) = delete;
    CsIdent(CsIdent &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~CsIdent() {};

    CsIdent &operator=(CsIdent const &) = delete;
    CsIdent &operator=(CsIdent &&) = delete;

    CsIdentType get_type() const;
    ostd::ConstCharRange get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    CsAlias *get_alias();
    CsAlias const *get_alias() const;

    bool is_command() const;
    CsCommand *get_command();
    CsCommand const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    CsVar *get_var();
    CsVar const *get_var() const;

    bool is_ivar() const;
    CsIvar *get_ivar();
    CsIvar const *get_ivar() const;

    bool is_fvar() const;
    CsFvar *get_fvar();
    CsFvar const *get_fvar() const;

    bool is_svar() const;
    CsSvar *get_svar();
    CsSvar const *get_svar() const;

    int get_type_raw() const {
        return p_type;
    }

protected:
    CsIdent(CsIdentType tp, ostd::ConstCharRange name, int flags = 0);

    CsString p_name;
    /* represents the CsIdentType above, but internally it has a wider variety
     * of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

private:
    int p_index = -1;
};

struct OSTD_EXPORT CsVar: CsIdent {
    friend struct CsState;
    friend struct CsSharedState;

protected:
    CsVar(CsIdentType tp, ostd::ConstCharRange name, CsVarCb func, int flags = 0);

private:
    CsVarCb cb_var;

    virtual CsString to_printable() const = 0;

    void changed(CsState &cs) {
        if (cb_var) {
            cb_var(cs, *this);
        }
    }
};

struct OSTD_EXPORT CsIvar: CsVar {
    friend struct CsState;
    friend struct CsSharedState;

    CsInt get_val_min() const;
    CsInt get_val_max() const;

    CsInt get_value() const;
    void set_value(CsInt val);

    CsString to_printable() const final;

private:
    CsIvar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt v, CsVarCb f, int flags
    );

    CsInt p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT CsFvar: CsVar {
    friend struct CsState;
    friend struct CsSharedState;

    CsFloat get_val_min() const;
    CsFloat get_val_max() const;

    CsFloat get_value() const;
    void set_value(CsFloat val);

    CsString to_printable() const final;

private:
    CsFvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat v,
        CsVarCb f, int flags
    );

    CsFloat p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT CsSvar: CsVar {
    friend struct CsState;
    friend struct CsSharedState;

    ostd::ConstCharRange get_value() const;
    void set_value(CsString val);

    CsString to_printable() const final;

private:
    CsSvar(ostd::ConstCharRange n, CsString v, CsVarCb f, int flags);

    CsString p_storage, p_overrideval;
};

struct OSTD_EXPORT CsAlias: CsIdent {
    friend struct CsState;
    friend struct CsSharedState;
    friend struct CsAliasInternal;

    CsValue const &get_value() const {
        return p_val;
    }

    CsValue &get_value() {
        return p_val;
    }

    void get_cstr(CsValue &v) const;
    void get_cval(CsValue &v) const;
private:
    CsAlias(ostd::ConstCharRange n, CsString a, int flags);
    CsAlias(ostd::ConstCharRange n, CsInt a, int flags);
    CsAlias(ostd::ConstCharRange n, CsFloat a, int flags);
    CsAlias(ostd::ConstCharRange n, int flags);
    CsAlias(ostd::ConstCharRange n, CsValue v, int flags);

    CsBytecode *p_acode;
    CsIdentStack *p_astack;
    CsValue p_val;
};

struct CsCommand: CsIdent {
    friend struct CsState;
    friend struct CsSharedState;
    friend struct CsCommandInternal;

    ostd::ConstCharRange get_args() const;
    int get_num_args() const;

private:
    CsCommand(
        ostd::ConstCharRange name, ostd::ConstCharRange args,
        int numargs, CsCommandCb func
    );

    CsString p_cargs;
    CsCommandCb p_cb_cftv;
    int p_numargs;
};

struct CsIdentLink;

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

struct OSTD_EXPORT CsState {
    friend struct CsErrorException;
    friend struct GenState;

    CsSharedState *p_state;
    CsIdentLink *p_callstack = nullptr;

    int identflags = 0;

    CsState(CsAllocCb func = cs_default_alloc, void *data = nullptr);
    virtual ~CsState();

    CsHookCb set_call_hook(CsHookCb func);
    CsHookCb const &get_call_hook() const;
    CsHookCb &get_call_hook();

    void init_libs(int libs = CsLibAll);

    void clear_override(CsIdent &id);
    void clear_overrides();

    CsIdent *new_ident(ostd::ConstCharRange name, int flags = CsIdfUnknown);
    CsIdent *force_ident(CsValue &v);

    CsIvar *new_ivar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt v,
        CsVarCb f = CsVarCb(), int flags = 0
    );
    CsFvar *new_fvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat v,
        CsVarCb f = CsVarCb(), int flags = 0
    );
    CsSvar *new_svar(
        ostd::ConstCharRange n, CsString v,
        CsVarCb f = CsVarCb(), int flags = 0
    );

    CsCommand *new_command(
        ostd::ConstCharRange name, ostd::ConstCharRange args, CsCommandCb func
    );

    CsIdent *get_ident(ostd::ConstCharRange name);
    CsAlias *get_alias(ostd::ConstCharRange name);
    bool have_ident(ostd::ConstCharRange name);

    CsIdentRange get_idents();
    CsConstIdentRange get_idents() const;

    void reset_var(ostd::ConstCharRange name);
    void touch_var(ostd::ConstCharRange name);

    CsString run_str(CsBytecode *code);
    CsString run_str(ostd::ConstCharRange code);
    CsString run_str(CsIdent *id, CsValueRange args);

    CsInt run_int(CsBytecode *code);
    CsInt run_int(ostd::ConstCharRange code);
    CsInt run_int(CsIdent *id, CsValueRange args);

    CsFloat run_float(CsBytecode *code);
    CsFloat run_float(ostd::ConstCharRange code);
    CsFloat run_float(CsIdent *id, CsValueRange args);

    bool run_bool(CsBytecode *code);
    bool run_bool(ostd::ConstCharRange code);
    bool run_bool(CsIdent *id, CsValueRange args);

    void run(CsBytecode *code, CsValue &ret);
    void run(ostd::ConstCharRange code, CsValue &ret);
    void run(CsIdent *id, CsValueRange args, CsValue &ret);

    void run(CsBytecode *code);
    void run(ostd::ConstCharRange code);
    void run(CsIdent *id, CsValueRange args);

    CsLoopState run_loop(CsBytecode *code, CsValue &ret);
    CsLoopState run_loop(CsBytecode *code);

    bool is_in_loop() const {
        return p_inloop;
    }

    std::optional<CsString> run_file_str(ostd::ConstCharRange fname);
    std::optional<CsInt> run_file_int(ostd::ConstCharRange fname);
    std::optional<CsFloat> run_file_float(ostd::ConstCharRange fname);
    std::optional<bool> run_file_bool(ostd::ConstCharRange fname);
    bool run_file(ostd::ConstCharRange fname, CsValue &ret);
    bool run_file(ostd::ConstCharRange fname);

    void set_alias(ostd::ConstCharRange name, CsValue v);

    void set_var_int(
        ostd::ConstCharRange name, CsInt v,
        bool dofunc = true, bool doclamp = true
    );
    void set_var_float(
        ostd::ConstCharRange name, CsFloat v,
        bool dofunc  = true, bool doclamp = true
    );
    void set_var_str(
        ostd::ConstCharRange name, ostd::ConstCharRange v, bool dofunc = true
    );

    void set_var_int_checked(CsIvar *iv, CsInt v);
    void set_var_int_checked(CsIvar *iv, CsValueRange args);
    void set_var_float_checked(CsFvar *fv, CsFloat v);
    void set_var_str_checked(CsSvar *fv, ostd::ConstCharRange v);

    std::optional<CsInt> get_var_int(ostd::ConstCharRange name);
    std::optional<CsFloat> get_var_float(ostd::ConstCharRange name);
    std::optional<CsString> get_var_str(ostd::ConstCharRange name);

    std::optional<CsInt> get_var_min_int(ostd::ConstCharRange name);
    std::optional<CsInt> get_var_max_int(ostd::ConstCharRange name);

    std::optional<CsFloat> get_var_min_float(ostd::ConstCharRange name);
    std::optional<CsFloat> get_var_max_float(ostd::ConstCharRange name);

    std::optional<CsString> get_alias_val(ostd::ConstCharRange name);

    virtual void print_var(CsVar *v);

private:
    CsIdent *add_ident(CsIdent *id);

    void *alloc(void *ptr, size_t olds, size_t news);

    GenState *p_pstate = nullptr;
    int p_inloop = 0;

    char p_errbuf[512];

    CsHookCb p_callhook;
};

struct CsStackStateNode {
    CsStackStateNode const *next;
    CsIdent const *id;
    int index;
};

struct CsStackState {
    CsStackState() = delete;
    CsStackState(CsState &cs, CsStackStateNode *nd = nullptr, bool gap = false);
    CsStackState(CsStackState const &) = delete;
    CsStackState(CsStackState &&st);
    ~CsStackState();

    CsStackState &operator=(CsStackState const &) = delete;
    CsStackState &operator=(CsStackState &&);

    CsStackStateNode const *get() const;
    bool gap() const;

private:
    CsState &p_state;
    CsStackStateNode *p_node;
    bool p_gap;
};

struct CsErrorException {
    friend struct CsState;

    CsErrorException() = delete;
    CsErrorException(CsErrorException const &) = delete;
    CsErrorException(CsErrorException &&v):
        p_errmsg(v.p_errmsg), p_stack(std::move(v.p_stack))
    {}

    ostd::ConstCharRange what() const {
        return p_errmsg;
    }

    CsStackState &get_stack() {
        return p_stack;
    }

    CsStackState const &get_stack() const {
        return p_stack;
    }

    CsErrorException(CsState &cs, ostd::ConstCharRange msg):
        p_errmsg(), p_stack(cs)
    {
        p_errmsg = save_msg(cs, msg);
        p_stack = save_stack(cs);
    }

    template<typename ...A>
    CsErrorException(CsState &cs, ostd::ConstCharRange msg, A &&...args):
        p_errmsg(), p_stack(cs)
    {
        char fbuf[512];
        auto ret = ostd::format(
            ostd::CharRange(fbuf, fbuf + sizeof(fbuf)), msg,
            std::forward<A>(args)...
        );
        if ((ret < 0) || (size_t(ret) > sizeof(fbuf))) {
            p_errmsg = save_msg(cs, msg);
        } else {
            p_errmsg = save_msg(cs, ostd::CharRange(fbuf, fbuf + ret));
        }
        p_stack = save_stack(cs);
    }

private:
    CsStackState save_stack(CsState &cs);
    ostd::ConstCharRange save_msg(CsState &cs, ostd::ConstCharRange v);

    ostd::ConstCharRange p_errmsg;
    CsStackState p_stack;
};

struct OSTD_EXPORT CsStackedValue: CsValue {
    CsStackedValue(CsIdent *id = nullptr);
    ~CsStackedValue();

    CsStackedValue(CsStackedValue const &) = delete;
    CsStackedValue(CsStackedValue &&) = delete;

    CsStackedValue &operator=(CsStackedValue const &) = delete;
    CsStackedValue &operator=(CsStackedValue &&v) = delete;

    CsStackedValue &operator=(CsValue const &v);
    CsStackedValue &operator=(CsValue &&v);

    bool set_alias(CsIdent *id);
    CsAlias *get_alias() const;
    bool has_alias() const;

    bool push();
    bool pop();

private:
    CsAlias *p_a;
    CsIdentStack p_stack;
    bool p_pushed;
};

namespace util {
    template<typename R>
    inline size_t escape_string(R &&writer, ostd::ConstCharRange str) {
        size_t ret = 2;
        writer.put('"');
        for (; !str.empty(); str.pop_front()) {
            switch (str.front()) {
                case '\n':
                    ret += writer.put_n("^n", 2);
                    break;
                case '\t':
                    ret += writer.put_n("^t", 2);
                    break;
                case '\f':
                    ret += writer.put_n("^f", 2);
                    break;
                case '"':
                    ret += writer.put_n("^\"", 2);
                    break;
                case '^':
                    ret += writer.put_n("^^", 2);
                    break;
                default:
                    ret += writer.put(str.front());
                    break;
            }
        }
        writer.put('"');
        return ret;
    }

    template<typename R>
    inline size_t unescape_string(R &&writer, ostd::ConstCharRange str) {
        size_t ret = 0;
        for (; !str.empty(); str.pop_front()) {
            if (str.front() == '^') {
                str.pop_front();
                if (str.empty()) {
                    break;
                }
                switch (str.front()) {
                    case 'n':
                        ret += writer.put('\n');
                        break;
                    case 't':
                        ret += writer.put('\r');
                        break;
                    case 'f':
                        ret += writer.put('\f');
                        break;
                    case '"':
                        ret += writer.put('"');
                        break;
                    case '^':
                        ret += writer.put('^');
                        break;
                    default:
                        ret += writer.put(str.front());
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
                ret += writer.put('\\');
            } else {
                ret += writer.put(str.front());
            }
        }
        return ret;
    }

    OSTD_EXPORT ostd::ConstCharRange parse_string(
        CsState &cs, ostd::ConstCharRange str, size_t &nlines
    );

    inline ostd::ConstCharRange parse_string(
        CsState &cs, ostd::ConstCharRange str
    ) {
        size_t nlines;
        return parse_string(cs, str, nlines);
    }

    OSTD_EXPORT ostd::ConstCharRange parse_word(
        CsState &cs, ostd::ConstCharRange str
    );

    struct OSTD_EXPORT ListParser {
        ListParser() = delete;
        ListParser(CsState &cs, ostd::ConstCharRange src):
            p_state(cs), p_input(src)
        {}

        void skip();
        bool parse();
        size_t count();

        template<typename R>
        size_t get_item(R &&writer) const {
            if (!p_quote.empty() && (*p_quote == '"')) {
                return unescape_string(std::forward<R>(writer), p_item);
            } else {
                return writer.put_n(p_item.data(), p_item.size());
            }
        }

        CsString get_item() const {
            auto app = ostd::appender<CsString>();
            get_item(app);
            return std::move(app.get());
        }

        ostd::ConstCharRange &get_raw_item(bool quoted = false) {
            return quoted ? p_quote : p_item;
        }

        ostd::ConstCharRange const &get_raw_item(bool quoted = false) const {
            return quoted ? p_quote : p_item;
        }

        ostd::ConstCharRange &get_input() {
            return p_input;
        }

private:
        ostd::ConstCharRange p_quote = ostd::ConstCharRange();
        ostd::ConstCharRange p_item = ostd::ConstCharRange();
        CsState &p_state;
        ostd::ConstCharRange p_input;
    };

    template<typename R>
    inline std::ptrdiff_t format_int(R &&writer, CsInt val) {
        return ostd::format(std::forward<R>(writer), IntFormat, val);
    }

    template<typename R>
    inline std::ptrdiff_t format_float(R &&writer, CsFloat val) {
        return ostd::format(
            std::forward<R>(writer),
            (val == CsInt(val)) ? RoundFloatFormat : FloatFormat, val
        );
    }

    template<typename R>
    inline size_t tvals_concat(
        R &&writer, CsValueRange vals,
        ostd::ConstCharRange sep = ostd::ConstCharRange()
    ) {
        size_t ret = 0;
        for (size_t i = 0; i < vals.size(); ++i) {
            auto s = ostd::appender<CsString>();
            switch (vals[i].get_type()) {
                case CsValueType::Int: {
                    auto r = format_int(
                        std::forward<R>(writer), vals[i].get_int()
                    );
                    if (r > 0) {
                        ret += size_t(r);
                    }
                    break;
                }
                case CsValueType::Float: {
                    auto r = format_float(
                        std::forward<R>(writer), vals[i].get_float()
                    );
                    if (r > 0) {
                        ret += size_t(r);
                    }
                    break;
                }
                case CsValueType::String:
                case CsValueType::Cstring:
                case CsValueType::Macro: {
                    auto sv = vals[i].get_strr();
                    ret += writer.put_n(sv.data(), sv.size());
                    break;
                }
                default:
                    break;
            }
            if (i == (vals.size() - 1)) {
                break;
            }
            ret += writer.put_n(sep.data(), sep.size());
        }
        return ret;
    }

    template<typename R>
    inline size_t print_stack(R &&writer, CsStackState const &st) {
        size_t ret = 0;
        auto nd = st.get();
        while (nd) {
            auto rt = ostd::format(
                writer,
                ((nd->index == 1) && st.gap())
                    ? "  ..%d) %s" : "  %d) %s",
                nd->index, nd->id->get_name()
            );
            if (rt > 0) {
                ret += size_t(rt);
            } else {
                return ret;
            }
            nd = nd->next;
            if (nd) {
                ret += writer.put('\n');
            }
        }
        return ret;
    }
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
