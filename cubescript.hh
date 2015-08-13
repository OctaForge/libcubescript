#include <stdio.h>
#include <stdlib.h>

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

static constexpr int MAX_ARGUMENTS = 25;
static constexpr int MAX_RESULTS = 7;
static constexpr int MAX_COMARGS = 12;

enum {
    VAL_NULL = 0, VAL_INT, VAL_FLOAT, VAL_STR,
    VAL_ANY, VAL_CODE, VAL_MACRO, VAL_IDENT, VAL_CSTR,
    VAL_CANY, VAL_WORD, VAL_POP, VAL_COND
};

enum {
    CODE_START = 0,
    CODE_OFFSET,
    CODE_NULL, CODE_TRUE, CODE_FALSE, CODE_NOT,
    CODE_POP,
    CODE_ENTER, CODE_ENTER_RESULT,
    CODE_EXIT, CODE_RESULT_ARG,
    CODE_VAL, CODE_VALI,
    CODE_DUP,
    CODE_MACRO,
    CODE_BOOL,
    CODE_BLOCK, CODE_EMPTY,
    CODE_COMPILE, CODE_COND,
    CODE_FORCE,
    CODE_RESULT,
    CODE_IDENT, CODE_IDENTU, CODE_IDENTARG,
    CODE_COM, CODE_COMD, CODE_COMC, CODE_COMV,
    CODE_CONC, CODE_CONCW, CODE_CONCM, CODE_DOWN,
    CODE_SVAR, CODE_SVARM, CODE_SVAR1,
    CODE_IVAR, CODE_IVAR1, CODE_IVAR2, CODE_IVAR3,
    CODE_FVAR, CODE_FVAR1,
    CODE_LOOKUP, CODE_LOOKUPU, CODE_LOOKUPARG,
    CODE_LOOKUPM, CODE_LOOKUPMU, CODE_LOOKUPMARG,
    CODE_ALIAS, CODE_ALIASU, CODE_ALIASARG,
    CODE_CALL, CODE_CALLU, CODE_CALLARG,
    CODE_PRINT,
    CODE_LOCAL,
    CODE_DO, CODE_DOARGS,
    CODE_JUMP, CODE_JUMP_TRUE, CODE_JUMP_FALSE,
    CODE_JUMP_RESULT_TRUE, CODE_JUMP_RESULT_FALSE,

    CODE_OP_MASK = 0x3F,
    CODE_RET = 6,
    CODE_RET_MASK = 0xC0,

    /* return type flags */
    RET_NULL   = VAL_NULL << CODE_RET,
    RET_STR    = VAL_STR << CODE_RET,
    RET_INT    = VAL_INT << CODE_RET,
    RET_FLOAT  = VAL_FLOAT << CODE_RET,
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

struct Ident;

struct IdentValue {
    union {
        int i;      /* ID_VAR, VAL_INT */
        float f;    /* ID_FVAR, VAL_FLOAT */
        char *s;    /* ID_SVAR, VAL_STR */
        const ostd::Uint32 *code; /* VAL_CODE */
        Ident *id;  /* VAL_IDENT */
        const char *cstr; /* VAL_CSTR */
    };
};

struct TaggedValue: IdentValue {
    int type;

    void set_int(int val) {
        type = VAL_INT;
        i = val;
    }
    void set_float(float val) {
        type = VAL_FLOAT;
        f = val;
    }
    void set_str(char *val) {
        type = VAL_STR;
        s = val;
    }
    void set_null() {
        type = VAL_NULL;
        i = 0;
    }
    void set_code(const ostd::Uint32 *val) {
        type = VAL_CODE;
        code = val;
    }
    void set_macro(const ostd::Uint32 *val) {
        type = VAL_MACRO;
        code = val;
    }
    void set_cstr(const char *val) {
        type = VAL_CSTR;
        cstr = val;
    }
    void set_ident(Ident *val) {
        type = VAL_IDENT;
        id = val;
    }

    void set(TaggedValue &tv) {
        *this = tv;
        tv.type = VAL_NULL;
    }

    ostd::ConstCharRange get_str() const;
    int get_int() const;
    float get_float() const;
    void get_val(TaggedValue &r) const;

    void force_null();
    float force_float();
    int force_int();
    ostd::ConstCharRange force_str();
    void force(int type);

    void cleanup();
};

struct IdentStack {
    IdentValue val;
    int valtype;
    IdentStack *next;
};

union IdentValuePtr {
    void *p;
    int *i;   /* ID_VAR */
    float *f; /* ID_FVAR */
    char **s; /* ID_SVAR */
};

struct CsState;

using IdentFunc = void (__cdecl *)(CsState &cs, Ident *id);

struct Ident {
    ostd::byte type; /* ID_something */
    union {
        ostd::byte valtype; /* ID_ALIAS */
        ostd::byte numargs; /* ID_COMMAND */
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
    IdentFunc fun; /* ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND */

    Ident(): type(ID_UNKNOWN) {}

    /* ID_VAR */
    Ident(int t, ostd::ConstCharRange n, int m, int x, int *s,
          IdentFunc f = nullptr, int flags = 0);

    /* ID_FVAR */
    Ident(int t, ostd::ConstCharRange n, float m, float x, float *s,
          IdentFunc f = nullptr, int flags = 0);

    /* ID_SVAR */
    Ident(int t, ostd::ConstCharRange n, char **s, IdentFunc f = nullptr,
          int flags = 0);

    /* ID_ALIAS */
    Ident(int t, ostd::ConstCharRange n, char *a, int flags);
    Ident(int t, ostd::ConstCharRange n, int a, int flags);
    Ident(int t, ostd::ConstCharRange n, float a, int flags);
    Ident(int t, ostd::ConstCharRange n, int flags);
    Ident(int t, ostd::ConstCharRange n, const TaggedValue &v, int flags);

    /* ID_COMMAND */
    Ident(int t, ostd::ConstCharRange n, ostd::ConstCharRange args,
          ostd::Uint32 argmask, int numargs, IdentFunc f = nullptr,
          int flags = 0);

    void changed(CsState &cs) {
        if (fun) fun(cs, this);
    }

    void set_value(const TaggedValue &v) {
        valtype = v.type;
        val = v;
    }

    void set_value(const IdentStack &v) {
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
    ostd::ConstCharRange get_str() const;
    void get_val(TaggedValue &r) const;
    void get_cstr(TaggedValue &v) const;
    void get_cval(TaggedValue &v) const;

    ostd::ConstCharRange get_key() const {
        return name.iter();
    }

    void clean_code();

    void push_arg(const TaggedValue &v, IdentStack &st, bool um = true);
    void pop_arg();
    void undo_arg(IdentStack &st);
    void redo_arg(const IdentStack &st);

    void push_alias(IdentStack &st);
    void pop_alias();

    void set_arg(CsState &cs, TaggedValue &v);
    void set_alias(CsState &cs, TaggedValue &v);
};

struct IdentLink {
    Ident *id;
    IdentLink *next;
    int usedargs;
    IdentStack *argstack;
};

struct CsState {
    ostd::Keyset<Ident> idents;
    ostd::Vector<Ident *> identmap;

    Ident *dummy = nullptr;
    TaggedValue *result = nullptr;

    IdentLink noalias = {
        nullptr, nullptr, (1 << MAX_ARGUMENTS) - 1, nullptr
    };
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

    Ident *new_ident(ostd::ConstCharRange name, int flags = 0);
    Ident *force_ident(TaggedValue &v);

    Ident *get_ident(ostd::ConstCharRange name) {
        return idents.at(name);
    }

    bool have_ident(ostd::ConstCharRange name) {
        return idents.at(name) != nullptr;
    }

    bool reset_var(ostd::ConstCharRange name);
    void touch_var(ostd::ConstCharRange name);

    template<typename F>
    bool add_command(ostd::ConstCharRange name, ostd::ConstCharRange args,
                     F func, int type = ID_COMMAND) {
        return add_command(name, args,
            (IdentFunc)(ostd::FunctionMakeDefaultConstructible<F>)func, type);
    }

    bool add_command(ostd::ConstCharRange name, ostd::ConstCharRange args,
                     IdentFunc func, int type = ID_COMMAND);

    ostd::String run_str(const ostd::Uint32 *code);
    ostd::String run_str(ostd::ConstCharRange code);
    ostd::String run_str(Ident *id, ostd::PointerRange<TaggedValue> args);

    int run_int(const ostd::Uint32 *code);
    int run_int(ostd::ConstCharRange code);
    int run_int(Ident *id, ostd::PointerRange<TaggedValue> args);

    float run_float(const ostd::Uint32 *code);
    float run_float(ostd::ConstCharRange code);
    float run_float(Ident *id, ostd::PointerRange<TaggedValue> args);

    bool run_bool(const ostd::Uint32 *code);
    bool run_bool(ostd::ConstCharRange code);
    bool run_bool(Ident *id, ostd::PointerRange<TaggedValue> args);

    void run_ret(const ostd::Uint32 *code, TaggedValue &result);
    void run_ret(ostd::ConstCharRange code, TaggedValue &result);
    void run_ret(Ident *id, ostd::PointerRange<TaggedValue> args,
                 TaggedValue &result);

    void run_ret(const ostd::Uint32 *code) {
        run_ret(code, *result);
    }

    void run_ret(ostd::ConstCharRange code) {
        run_ret(code, *result);
    }

    void run_ret(Ident *id, ostd::PointerRange<TaggedValue> args) {
        run_ret(id, args, *result);
    }

    bool run_file(ostd::ConstCharRange fname, bool msg = true);

    void set_alias(ostd::ConstCharRange name, TaggedValue &v);

    void set_var_int(ostd::ConstCharRange name, int v,
                     bool dofunc = true, bool doclamp = true);
    void set_var_float(ostd::ConstCharRange name, float v,
                       bool dofunc  = true, bool doclamp = true);
    void set_var_str(ostd::ConstCharRange name, ostd::ConstCharRange v,
                     bool dofunc = true);

    void set_var_int_checked(Ident *id, int v);
    void set_var_int_checked(Ident *id, ostd::PointerRange<TaggedValue> args);
    void set_var_float_checked(Ident *id, float v);
    void set_var_str_checked(Ident *id, ostd::ConstCharRange v);

    ostd::Maybe<int> get_var_int(ostd::ConstCharRange name);
    ostd::Maybe<float> get_var_float(ostd::ConstCharRange name);
    ostd::Maybe<ostd::String> get_var_str(ostd::ConstCharRange name);

    ostd::Maybe<int> get_var_min_int(ostd::ConstCharRange name);
    ostd::Maybe<int> get_var_max_int(ostd::ConstCharRange name);

    ostd::Maybe<float> get_var_min_float(ostd::ConstCharRange name);
    ostd::Maybe<float> get_var_max_float(ostd::ConstCharRange name);

    ostd::Maybe<ostd::ConstCharRange> get_alias(ostd::ConstCharRange name);

    void print_var(Ident *id);
    void print_var_int(Ident *id, int i);
    void print_var_float(Ident *id, float f);
    void print_var_str(Ident *id, ostd::ConstCharRange s);

    ostd::Uint32 *compile(ostd::ConstCharRange code);
};

void bcode_ref(ostd::Uint32 *p);
void bcode_unref(ostd::Uint32 *p);

void init_lib_base(CsState &cs);
void init_lib_io(CsState &cs);
void init_lib_math(CsState &cs);
void init_lib_string(CsState &cs);
void init_lib_list(CsState &cs);

inline bool check_alias(Ident *id) {
    return id && (id->type == ID_ALIAS);
}

struct StackedValue: TaggedValue {
    IdentStack stack;
    Ident *id;
    bool pushed;

    StackedValue(Ident *id = nullptr):
        TaggedValue(), stack(), id(id), pushed(false) {}

    ~StackedValue() {
        pop();
    }

    bool push() {
        if (pushed || !id) return false;
        id->push_arg(*this, stack);
        pushed = true;
        return true;
    }

    bool pop() {
        printf("pop\n");
        if (!pushed || !id) return false;
        id->pop_arg();
        pushed = false;
        return true;
    }
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
}

} /* namespace cscript */