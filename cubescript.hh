#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <stdio.h>
#include <stdlib.h>

#include "cubescript_conf.hh"

#include <ostd/platform.hh>
#include <ostd/types.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/map.hh>
#include <ostd/range.hh>
#include <ostd/utility.hh>
#include <ostd/maybe.hh>
#include <ostd/io.hh>
#include <ostd/functional.hh>
#include <ostd/format.hh>

namespace cscript {

enum {
    VAL_NULL = 0, VAL_INT, VAL_FLOAT, VAL_STR,
    VAL_ANY, VAL_CODE, VAL_MACRO, VAL_IDENT, VAL_CSTR,
    VAL_CANY, VAL_WORD, VAL_POP, VAL_COND
};

enum {
    IDF_PERSIST    = 1 << 0,
    IDF_OVERRIDE   = 1 << 1,
    IDF_HEX        = 1 << 2,
    IDF_READONLY   = 1 << 3,
    IDF_OVERRIDDEN = 1 << 4,
    IDF_UNKNOWN    = 1 << 5,
    IDF_ARG        = 1 << 6
};

struct Bytecode;

struct OSTD_EXPORT BytecodeRef {
    BytecodeRef():
        p_code(nullptr)
    {}
    BytecodeRef(Bytecode *v);
    BytecodeRef(BytecodeRef const &v);
    BytecodeRef(BytecodeRef &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~BytecodeRef();

    BytecodeRef &operator=(BytecodeRef const &v);
    BytecodeRef &operator=(BytecodeRef &&v);

    operator bool() const { return p_code != nullptr; }
    operator Bytecode *() const { return p_code; }

private:
    Bytecode *p_code;
};

OSTD_EXPORT bool code_is_empty(Bytecode const *code);

struct Ident;

struct OSTD_EXPORT CsValue {
    union {
        CsInt i;      /* ID_IVAR, VAL_INT */
        CsFloat f;    /* ID_FVAR, VAL_FLOAT */
        Bytecode const *code; /* VAL_CODE */
        Ident *id;  /* VAL_IDENT */
        char *s;    /* ID_SVAR, VAL_STR */
        char const *cstr; /* VAL_CSTR */
    };
    ostd::Size len;

    int get_type() const {
        return p_type;
    }

    void set_int(CsInt val) {
        p_type = VAL_INT;
        i = val;
    }
    void set_float(CsFloat val) {
        p_type = VAL_FLOAT;
        f = val;
    }
    void set_str(ostd::String val) {
        if (val.size() == 0) {
            /* ostd zero length strings cannot be disowned */
            char *buf = new char[1];
            buf[0] = '\0';
            set_mstr(buf);
            return;
        }
        ostd::CharRange cr = val.iter();
        val.disown();
        set_mstr(cr);
    }
    void set_null() {
        p_type = VAL_NULL;
        i = 0;
    }
    void set_code(Bytecode const *val) {
        p_type = VAL_CODE;
        code = val;
    }
    void set_cstr(ostd::ConstCharRange val) {
        p_type = VAL_CSTR;
        len = val.size();
        cstr = val.data();
    }
    void set_mstr(ostd::CharRange val) {
        p_type = VAL_STR;
        len = val.size();
        s = val.data();
    }
    void set_ident(Ident *val) {
        p_type = VAL_IDENT;
        id = val;
    }
    void set_macro(Bytecode const *val, ostd::Size ln) {
        p_type = VAL_MACRO;
        len = ln;
        code = val;
    }

    void set(CsValue &tv) {
        *this = tv;
        tv.p_type = VAL_NULL;
    }

    ostd::String get_str() const;
    ostd::ConstCharRange get_strr() const;
    CsInt get_int() const;
    CsFloat get_float() const;
    Bytecode *get_code() const;
    Ident *get_ident() const;
    void get_val(CsValue &r) const;

    bool get_bool() const;

    void force_null();
    CsFloat force_float();
    CsInt force_int();
    ostd::ConstCharRange force_str();

    bool code_is_empty() const;

    void cleanup();
    void copy_arg(CsValue &r) const;

private:
    int p_type;
};

using CsValueRange = ostd::PointerRange<CsValue>;

struct IdentStack {
    CsValue val_s;
    IdentStack *next;
};

struct CsState;

enum class IdentType {
    ivar = 0, fvar, svar, command, alias, special
};

struct Var;
struct Ivar;
struct Fvar;
struct Svar;
struct Alias;

struct OSTD_EXPORT Ident {
    friend struct CsState;

    IdentType get_type() const;
    ostd::ConstCharRange get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    Alias *get_alias();
    Alias const *get_alias() const;

    bool is_command() const;
    bool is_special() const;

    bool is_var() const;
    Var *get_var();
    Var const *get_var() const;

    bool is_ivar() const;
    Ivar *get_ivar();
    Ivar const *get_ivar() const;

    bool is_fvar() const;
    Fvar *get_fvar();
    Fvar const *get_fvar() const;

    bool is_svar() const;
    Svar *get_svar();
    Svar const *get_svar() const;

    int get_type_raw() const {
        return p_type;
    }

protected:
    Ident(IdentType tp, ostd::ConstCharRange name, int flags = 0);

    ostd::String p_name;
    /* represents the IdentType above, but internally it has a wider variety
     * of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

private:
    int p_index = -1;
};

using VarCb = ostd::Function<void(Ident &)>;

struct OSTD_EXPORT Var: Ident {
    friend struct CsState;

protected:
    Var(IdentType tp, ostd::ConstCharRange name, VarCb func, int flags = 0);

private:
    VarCb cb_var;

    void changed() {
        if (cb_var) {
            cb_var(*this);
        }
    }
};

struct OSTD_EXPORT Ivar: Var {
    friend struct CsState;

    CsInt get_val_min() const;
    CsInt get_val_max() const;

    CsInt get_var_value() const;

    Ivar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt *s,
        VarCb f = VarCb(), int flags = 0
    );

private:
    CsInt *p_storage;
    CsInt p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT Fvar: Var {
    friend struct CsState;

    CsFloat get_val_min() const;
    CsFloat get_val_max() const;

    CsFloat get_var_value() const;

    Fvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat *s,
        VarCb f = VarCb(), int flags = 0
    );

private:
    CsFloat *p_storage;
    CsFloat p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT Svar: Var {
    friend struct CsState;

    ostd::ConstCharRange get_var_value() const;

    Svar(
        ostd::ConstCharRange n, char **s, VarCb f = VarCb(),
        int flags = 0
    );

private:
    char **p_storage;
    char *p_overrideval;
};

struct OSTD_EXPORT Alias: Ident {
    CsValue val_v;

    Alias(ostd::ConstCharRange n, char *a, int flags);
    Alias(ostd::ConstCharRange n, CsInt a, int flags);
    Alias(ostd::ConstCharRange n, CsFloat a, int flags);
    Alias(ostd::ConstCharRange n, int flags);
    Alias(ostd::ConstCharRange n, CsValue const &v, int flags);

    void set_value(CsValue const &v) {
        val_v = v;
    }

    void set_value(IdentStack const &v) {
        val_v = v.val_s;
    }

    void set_value_cstr(ostd::ConstCharRange val) {
        val_v.set_cstr(val);
    }

    void set_value_mstr(ostd::CharRange val) {
        val_v.set_mstr(val);
    }

    void set_value_str(ostd::String val) {
        val_v.set_str(ostd::move(val));
    }

    void cleanup_value() {
        val_v.cleanup();
    }

    void get_cstr(CsValue &v) const;
    void get_cval(CsValue &v) const;

    void push_arg(CsValue const &v, IdentStack &st, bool um = true);
    void pop_arg();
    void undo_arg(IdentStack &st);
    void redo_arg(IdentStack const &st);

    void set_arg(CsState &cs, CsValue &v);
    void set_alias(CsState &cs, CsValue &v);

    void clean_code();
    Bytecode *compile_code(CsState &cs);

    void force_null() {
        cleanup_value();
        val_v.set_null();
    }

private:
    Bytecode *p_acode;
    IdentStack *p_astack;
};

struct IdentLink {
    Ident *id;
    IdentLink *next;
    int usedargs;
    IdentStack *argstack;
};

enum {
    CS_LIB_IO     = 1 << 0,
    CS_LIB_MATH   = 1 << 1,
    CS_LIB_STRING = 1 << 2,
    CS_LIB_LIST   = 1 << 3,
    CS_LIB_ALL    = 0b1111
};

using CmdFunc = ostd::Function<void(CsValueRange, CsValue &)>;

struct OSTD_EXPORT CsState {
    ostd::Map<ostd::ConstCharRange, Ident *> idents;
    ostd::Vector<Ident *> identmap;

    Ident *dummy = nullptr;

    IdentLink noalias;
    IdentLink *p_stack = &noalias;

    ostd::ConstCharRange src_file;
    ostd::ConstCharRange src_str;

    int identflags = 0;
    int nodebug = 0;
    CsInt numargs = 0;
    CsInt dbgalias = 4;

    CsState();
    ~CsState();

    void init_libs(int libs = CS_LIB_ALL);

    void clear_override(Ident &id);
    void clear_overrides();

    Ident *add_ident(Ident *id);
    Ident *new_ident(ostd::ConstCharRange name, int flags = IDF_UNKNOWN);
    Ident *force_ident(CsValue &v);

    template<typename T, typename ...A>
    T *add_ident(A &&...args) {
        return static_cast<T *>(add_ident(new T(ostd::forward<A>(args)...)));
    }

    Ident *get_ident(ostd::ConstCharRange name) {
        Ident **id = idents.at(name);
        if (!id) {
            return nullptr;
        }
        return *id;
    }

    Alias *get_alias(ostd::ConstCharRange name) {
        Ident *id = get_ident(name);
        if (!id->is_alias()) {
            return nullptr;
        }
        return static_cast<Alias *>(id);
    }

    bool have_ident(ostd::ConstCharRange name) {
        return idents.at(name) != nullptr;
    }

    bool reset_var(ostd::ConstCharRange name);
    void touch_var(ostd::ConstCharRange name);

    bool add_command(
        ostd::ConstCharRange name, ostd::ConstCharRange args, CmdFunc func
    );

    ostd::String run_str(Bytecode const *code);
    ostd::String run_str(ostd::ConstCharRange code);
    ostd::String run_str(Ident *id, CsValueRange args);

    CsInt run_int(Bytecode const *code);
    CsInt run_int(ostd::ConstCharRange code);
    CsInt run_int(Ident *id, CsValueRange args);

    CsFloat run_float(Bytecode const *code);
    CsFloat run_float(ostd::ConstCharRange code);
    CsFloat run_float(Ident *id, CsValueRange args);

    bool run_bool(Bytecode const *code);
    bool run_bool(ostd::ConstCharRange code);
    bool run_bool(Ident *id, CsValueRange args);

    void run_ret(Bytecode const *code, CsValue &ret);
    void run_ret(ostd::ConstCharRange code, CsValue &ret);
    void run_ret(Ident *id, CsValueRange args, CsValue &ret);

    void run(Bytecode const *code);
    void run(ostd::ConstCharRange code);
    void run(Ident *id, CsValueRange args);

    ostd::Maybe<ostd::String> run_file_str(ostd::ConstCharRange fname);
    ostd::Maybe<CsInt> run_file_int(ostd::ConstCharRange fname);
    ostd::Maybe<CsFloat> run_file_float(ostd::ConstCharRange fname);
    ostd::Maybe<bool> run_file_bool(ostd::ConstCharRange fname);
    bool run_file_ret(ostd::ConstCharRange fname, CsValue &ret);
    bool run_file(ostd::ConstCharRange fname);

    void set_alias(ostd::ConstCharRange name, CsValue &v);

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

    void set_var_int_checked(Ivar *iv, CsInt v);
    void set_var_int_checked(Ivar *iv, CsValueRange args);
    void set_var_float_checked(Fvar *fv, CsFloat v);
    void set_var_str_checked(Svar *fv, ostd::ConstCharRange v);

    ostd::Maybe<CsInt> get_var_int(ostd::ConstCharRange name);
    ostd::Maybe<CsFloat> get_var_float(ostd::ConstCharRange name);
    ostd::Maybe<ostd::String> get_var_str(ostd::ConstCharRange name);

    ostd::Maybe<CsInt> get_var_min_int(ostd::ConstCharRange name);
    ostd::Maybe<CsInt> get_var_max_int(ostd::ConstCharRange name);

    ostd::Maybe<CsFloat> get_var_min_float(ostd::ConstCharRange name);
    ostd::Maybe<CsFloat> get_var_max_float(ostd::ConstCharRange name);

    ostd::Maybe<ostd::String> get_alias_val(ostd::ConstCharRange name);

    void print_var(Var *v);
    void print_var_int(Ivar *iv, CsInt i);
    void print_var_float(Fvar *fv, CsFloat f);
    void print_var_str(Svar *sv, ostd::ConstCharRange s);
};

struct OSTD_EXPORT StackedValue: CsValue {
    StackedValue(Ident *id = nullptr):
        CsValue(), p_a(nullptr), p_stack(), p_pushed(false)
    {
        set_alias(id);
    }

    ~StackedValue() {
        pop();
    }

    bool set_alias(Ident *id) {
        if (!id || !id->is_alias()) {
            return false;
        }
        p_a = static_cast<Alias *>(id);
        return true;
    }

    Alias *get_alias() const {
        return p_a;
    }

    bool has_alias() const {
        return p_a != nullptr;
    }

    bool push() {
        if (p_pushed || !p_a) {
            return false;
        }
        p_a->push_arg(*this, p_stack);
        p_pushed = true;
        return true;
    }

    bool pop() {
        if (!p_pushed || !p_a) {
            return false;
        }
        p_a->pop_arg();
        p_pushed = false;
        return true;
    }

private:
    Alias *p_a;
    IdentStack p_stack;
    bool p_pushed;
};

namespace util {
    template<typename R>
    inline ostd::Size escape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 2;
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
    inline ostd::Size unescape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 0;
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
            } else {
                ret += writer.put(str.front());
            }
        }
        return ret;
    }

    struct ListParser {
        ostd::ConstCharRange input;
        ostd::ConstCharRange quote = ostd::ConstCharRange();
        ostd::ConstCharRange item = ostd::ConstCharRange();

        ListParser() = delete;
        ListParser(ostd::ConstCharRange src): input(src) {}

        void skip();
        bool parse();

        ostd::String element();
    };

    ostd::Size list_length(ostd::ConstCharRange s);
    ostd::Maybe<ostd::String> list_index(
        ostd::ConstCharRange s, ostd::Size idx
    );
    ostd::Vector<ostd::String> list_explode(
        ostd::ConstCharRange s, ostd::Size limit = -1
    );

    template<typename R>
    inline ostd::Ptrdiff format_int(R &&writer, CsInt val) {
        return ostd::format(ostd::forward<R>(writer), IntFormat, val);
    }

    template<typename R>
    inline ostd::Ptrdiff format_float(R &&writer, CsFloat val) {
        return ostd::format(
            ostd::forward<R>(writer),
            (val == CsInt(val)) ? RoundFloatFormat : FloatFormat, val
        );
    }

    template<typename R>
    inline ostd::Size tvals_concat(
        R &&writer, CsValueRange vals,
        ostd::ConstCharRange sep = ostd::ConstCharRange()
    ) {
        ostd::Size ret = 0;
        for (ostd::Size i = 0; i < vals.size(); ++i) {
            auto s = ostd::appender<ostd::String>();
            switch (vals[i].get_type()) {
                case VAL_INT: {
                    auto r = format_int(ostd::forward<R>(writer), vals[i].i);
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case VAL_FLOAT: {
                    auto r = format_float(ostd::forward<R>(writer), vals[i].f);
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case VAL_STR:
                case VAL_CSTR:
                case VAL_MACRO:
                    ret += writer.put_n(vals[i].s, vals[i].len);
                    break;
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
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
