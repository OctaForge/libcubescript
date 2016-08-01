#ifndef CUBESCRIPT_HH
#define CUBESCRIPT_HH

#include <stdio.h>
#include <stdlib.h>

#include <ostd/platform.hh>
#include <ostd/types.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/keyset.hh>
#include <ostd/range.hh>
#include <ostd/utility.hh>
#include <ostd/maybe.hh>
#include <ostd/io.hh>
#include <ostd/functional.hh>

namespace cscript {

enum {
    VAL_NULL = 0, VAL_INT, VAL_FLOAT, VAL_STR,
    VAL_ANY, VAL_CODE, VAL_MACRO, VAL_IDENT, VAL_CSTR,
    VAL_CANY, VAL_WORD, VAL_POP, VAL_COND
};

enum {
    ID_UNKNOWN = -1, ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_RESULT, ID_NOT, ID_AND, ID_OR
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

struct OSTD_EXPORT Bytecode {
    Bytecode(): p_code(nullptr) {}
    Bytecode(ostd::Uint32 *v);
    Bytecode(Bytecode const &v);
    Bytecode(Bytecode &&v): p_code(v.p_code) { v.p_code = nullptr; }

    ~Bytecode();

    Bytecode &operator=(Bytecode const &v);
    Bytecode &operator=(Bytecode &&v);

    operator bool() const { return p_code != nullptr; }
    operator ostd::Uint32 *() const { return p_code; }

private:
    ostd::Uint32 *p_code;
};

struct Ident;

struct IdentValue {
    union {
        int i;      /* ID_VAR, VAL_INT */
        float f;    /* ID_FVAR, VAL_FLOAT */
        char *s;    /* ID_SVAR, VAL_STR */
        ostd::Uint32 const *code; /* VAL_CODE */
        Ident *id;  /* VAL_IDENT */
        char const *cstr; /* VAL_CSTR */
    };
};

struct OSTD_EXPORT TaggedValue: IdentValue {
    friend struct Ident;

    int get_type() const {
        return p_type & 0xF;
    }

    ostd::Size get_str_len() const {
        return ostd::Size(p_type >> 4);
    }

    void set_int(int val) {
        p_type = VAL_INT;
        i = val;
    }
    void set_float(float val) {
        p_type = VAL_FLOAT;
        f = val;
    }
    void set_str(ostd::String val) {
        ostd::CharRange cr = val.iter();
        val.disown();
        set_mstr(cr);
    }
    void set_null() {
        p_type = VAL_NULL;
        i = 0;
    }
    void set_code(ostd::Uint32 const *val) {
        p_type = VAL_CODE;
        code = val;
    }
    void set_macro(ostd::Uint32 const *val) {
        p_type = VAL_MACRO | (strlen(reinterpret_cast<char const *>(val)) << 4);
        code = val;
    }
    void set_cstr(ostd::ConstCharRange val) {
        p_type = VAL_CSTR | (val.size() << 4);
        cstr = val.data();
    }
    void set_mstr(ostd::CharRange val) {
        p_type = VAL_STR | (val.size() << 4);
        s = val.data();
    }
    void set_ident(Ident *val) {
        p_type = VAL_IDENT;
        id = val;
    }

    void set(TaggedValue &tv) {
        *this = tv;
        tv.p_type = VAL_NULL;
    }

    ostd::String get_str() const;
    ostd::ConstCharRange get_strr() const;
    int get_int() const;
    float get_float() const;
    ostd::Uint32 *get_code() const;
    void get_val(TaggedValue &r) const;

    void force_null();
    float force_float();
    int force_int();
    ostd::ConstCharRange force_str();
    void force(int type);

    void cleanup();

private:
    int p_type;
};

using TvalRange = ostd::PointerRange<TaggedValue>;

struct IdentStack {
    IdentValue val;
    int valtype;
    IdentStack *next;
};

union IdentValuePtr {
    int *ip;   /* ID_VAR */
    float *fp; /* ID_FVAR */
    char **sp; /* ID_SVAR */
};

struct CsState;

using VarCb = ostd::Function<void(CsState &, Ident &)>;
using CmdFunc = ostd::Function<void(CsState &, TvalRange)>;

struct OSTD_EXPORT Ident {
    ostd::byte type; /* ID_something */
    union {
        int valtype; /* ID_ALIAS */
        int numargs; /* ID_COMMAND */
    };
    ostd::ushort flags;
    int index;
    ostd::String name;
    union {
        struct { /* ID_VAR, ID_FVAR, ID_SVAR */
            union {
                struct { /* ID_VAR */
                    int minval, maxval;
                };
                struct { /* ID_FVAR */
                    float minvalf, maxvalf;
                };
            };
            IdentValuePtr storage;
            IdentValue overrideval;
        };
        struct { /* ID_ALIAS */
            ostd::Uint32 *code;
            IdentValue val;
            IdentStack *stack;
        };
        struct { /* ID_COMMAND */
            char *args;
            ostd::Uint32 argmask;
        };
    };
    VarCb cb_var;
    CmdFunc cb_cftv;

    Ident(): type(ID_UNKNOWN) {}

    /* ID_VAR */
    Ident(int t, ostd::ConstCharRange n, int m, int x, int *s,
          VarCb f = VarCb(), int flags = 0);

    /* ID_FVAR */
    Ident(int t, ostd::ConstCharRange n, float m, float x, float *s,
          VarCb f = VarCb(), int flags = 0);

    /* ID_SVAR */
    Ident(int t, ostd::ConstCharRange n, char **s, VarCb f = VarCb(),
          int flags = 0);

    /* ID_ALIAS */
    Ident(int t, ostd::ConstCharRange n, char *a, int flags);
    Ident(int t, ostd::ConstCharRange n, int a, int flags);
    Ident(int t, ostd::ConstCharRange n, float a, int flags);
    Ident(int t, ostd::ConstCharRange n, int flags);
    Ident(int t, ostd::ConstCharRange n, TaggedValue const &v, int flags);

    /* ID_COMMAND */
    Ident(int t, ostd::ConstCharRange n, ostd::ConstCharRange args,
          ostd::Uint32 argmask, int numargs, CmdFunc f = CmdFunc(),
          int flags = 0);

    void changed(CsState &cs) {
        if (cb_var) cb_var(cs, *this);
    }

    void set_value(TaggedValue const &v) {
        valtype = v.get_type() | int(v.get_str_len() << 4);
        val = v;
    }

    void set_value(IdentStack const &v) {
        valtype = v.valtype;
        val = v.val;
    }

    void force_null() {
        if (valtype == VAL_STR)
            delete[] val.s;
        valtype = VAL_NULL;
    }

    float get_float() const;
    int get_int() const;
    ostd::String get_str() const;
    ostd::ConstCharRange get_strr() const;
    void get_val(TaggedValue &r) const;
    void get_cstr(TaggedValue &v) const;
    void get_cval(TaggedValue &v) const;

    ostd::ConstCharRange get_key() const {
        return name.iter();
    }

    void clean_code();

    void push_arg(TaggedValue const &v, IdentStack &st, bool um = true);
    void pop_arg();
    void undo_arg(IdentStack &st);
    void redo_arg(IdentStack const &st);

    void push_alias(IdentStack &st);
    void pop_alias();

    void set_arg(CsState &cs, TaggedValue &v);
    void set_alias(CsState &cs, TaggedValue &v);

    int get_valtype() const {
        return valtype & 0xF;
    }
};

struct IdentLink {
    Ident *id;
    IdentLink *next;
    int usedargs;
    IdentStack *argstack;
};

struct OSTD_EXPORT CsState {
    ostd::Keyset<Ident> idents;
    ostd::Vector<Ident *> identmap;

    Ident *dummy = nullptr;
    TaggedValue *result = nullptr;

    IdentLink noalias;
    IdentLink *stack = &noalias;

    ostd::ConstCharRange src_file;
    ostd::ConstCharRange src_str;

    int identflags = 0;
    int nodebug = 0;
    int numargs = 0;
    int dbgalias = 4;

    CsState();
    ~CsState();

    void clear_override(Ident &id);
    void clear_overrides();

    template<typename ...A>
    Ident *add_ident(A &&...args) {
        Ident &def = idents.emplace(ostd::forward<A>(args)...).first.front();
        def.index = identmap.size();
        return identmap.push(&def);
    }

    Ident *new_ident(ostd::ConstCharRange name, int flags = IDF_UNKNOWN);
    Ident *force_ident(TaggedValue &v);

    Ident *get_ident(ostd::ConstCharRange name) {
        return idents.at(name);
    }

    bool have_ident(ostd::ConstCharRange name) {
        return idents.at(name) != nullptr;
    }

    bool reset_var(ostd::ConstCharRange name);
    void touch_var(ostd::ConstCharRange name);

    bool add_command(ostd::ConstCharRange name, ostd::ConstCharRange args,
                     CmdFunc func, int type = ID_COMMAND, int flags = 0);

    ostd::String run_str(ostd::Uint32 const *code);
    ostd::String run_str(ostd::ConstCharRange code);
    ostd::String run_str(Ident *id, TvalRange args);

    int run_int(ostd::Uint32 const *code);
    int run_int(ostd::ConstCharRange code);
    int run_int(Ident *id, TvalRange args);

    float run_float(ostd::Uint32 const *code);
    float run_float(ostd::ConstCharRange code);
    float run_float(Ident *id, TvalRange args);

    bool run_bool(ostd::Uint32 const *code);
    bool run_bool(ostd::ConstCharRange code);
    bool run_bool(Ident *id, TvalRange args);

    void run_ret(ostd::Uint32 const *code, TaggedValue &ret);
    void run_ret(ostd::ConstCharRange code, TaggedValue &ret);
    void run_ret(Ident *id, TvalRange args, TaggedValue &ret);

    void run_ret(ostd::Uint32 const *code) {
        run_ret(code, *result);
    }

    void run_ret(ostd::ConstCharRange code) {
        run_ret(code, *result);
    }

    void run_ret(Ident *id, TvalRange args) {
        run_ret(id, args, *result);
    }

    bool run_file(ostd::ConstCharRange fname);

    void set_alias(ostd::ConstCharRange name, TaggedValue &v);

    void set_var_int(ostd::ConstCharRange name, int v,
                     bool dofunc = true, bool doclamp = true);
    void set_var_float(ostd::ConstCharRange name, float v,
                       bool dofunc  = true, bool doclamp = true);
    void set_var_str(ostd::ConstCharRange name, ostd::ConstCharRange v,
                     bool dofunc = true);

    void set_var_int_checked(Ident *id, int v);
    void set_var_int_checked(Ident *id, TvalRange args);
    void set_var_float_checked(Ident *id, float v);
    void set_var_str_checked(Ident *id, ostd::ConstCharRange v);

    ostd::Maybe<int> get_var_int(ostd::ConstCharRange name);
    ostd::Maybe<float> get_var_float(ostd::ConstCharRange name);
    ostd::Maybe<ostd::String> get_var_str(ostd::ConstCharRange name);

    ostd::Maybe<int> get_var_min_int(ostd::ConstCharRange name);
    ostd::Maybe<int> get_var_max_int(ostd::ConstCharRange name);

    ostd::Maybe<float> get_var_min_float(ostd::ConstCharRange name);
    ostd::Maybe<float> get_var_max_float(ostd::ConstCharRange name);

    ostd::Maybe<ostd::String> get_alias(ostd::ConstCharRange name);

    void print_var(Ident *id);
    void print_var_int(Ident *id, int i);
    void print_var_float(Ident *id, float f);
    void print_var_str(Ident *id, ostd::ConstCharRange s);

    ostd::Uint32 *compile(ostd::ConstCharRange code);
};

enum {
    CS_LIB_BASE   = 1 << 0,
    CS_LIB_IO     = 1 << 1,
    CS_LIB_MATH   = 1 << 2,
    CS_LIB_STRING = 1 << 3,
    CS_LIB_LIST   = 1 << 4,
    CS_LIB_ALL    = 0b11111
};

OSTD_EXPORT void init_libs(CsState &cs, int libs = CS_LIB_ALL);

inline bool check_alias(Ident *id) {
    return id && (id->type == ID_ALIAS);
}

struct OSTD_EXPORT StackedValue: TaggedValue {
    Ident *id;

    StackedValue(Ident *idv = nullptr):
        TaggedValue(), id(idv), p_stack(), p_pushed(false) {}

    ~StackedValue() {
        pop();
    }

    bool alias(CsState &cs, ostd::ConstCharRange name) {
        id = cs.new_ident(name);
        return check_alias(id);
    }

    bool push() {
        if (p_pushed || !id) return false;
        id->push_arg(*this, p_stack);
        p_pushed = true;
        return true;
    }

    bool pop() {
        if (!p_pushed || !id) return false;
        id->pop_arg();
        p_pushed = false;
        return true;
    }

private:
    IdentStack p_stack;
    bool p_pushed;
};

namespace util {
    template<typename R>
    inline ostd::Size escape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 2;
        writer.put('"');
        for (; !str.empty(); str.pop_front()) switch (str.front()) {
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
        writer.put('"');
        return ret;
    }

    template<typename R>
    inline ostd::Size unescape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 0;
        for (; !str.empty(); str.pop_front()) {
            if (str.front() == '^') {
                str.pop_front();
                if (str.empty())
                    break;
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

    ostd::Size list_length(ostd::ConstCharRange s);
    ostd::Maybe<ostd::String> list_index(ostd::ConstCharRange s,
                                         ostd::Size idx);
    ostd::Vector<ostd::String> list_explode(ostd::ConstCharRange s,
                                            ostd::Size limit = -1);
} /* namespace util */

} /* namespace cscript */

#endif /* CUBESCRIPT_HH */
