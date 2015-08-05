#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include <ostd/algorithm.hh>
#include <ostd/vector.hh>
#include <ostd/string.hh>
#include <ostd/keyset.hh>
#include <ostd/format.hh>
#include <ostd/functional.hh>
#include <ostd/map.hh>
#include <ostd/io.hh>

inline char *dup_ostr(ostd::ConstCharRange s) {
    char *r = new char[s.size() + 1];
    memcpy(r, s.data(), s.size());
    r[s.size()] = 0;
    return r;
}

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
    ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS, ID_LOCAL,
    ID_DO, ID_DOARGS, ID_IF, ID_RESULT, ID_NOT, ID_AND, ID_OR
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
        const ostd::uint *code; /* VAL_CODE */
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
    void set_code(const ostd::uint *val) {
        type = VAL_CODE;
        code = val;
    }
    void set_macro(const ostd::uint *val) {
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

    const char *get_str() const;
    int get_int() const;
    float get_float() const;
    void get_val(TaggedValue &r) const;

    void force_null();
    float force_float();
    int force_int();
    const char *force_str();
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
            ostd::uint *code;
            IdentValue val;
            IdentStack *stack;
        };
        struct { /* ID_COMMAND */
            char *args;
            ostd::uint argmask;
        };
    };
    IdentFunc fun; /* ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND */

    Ident() {}
    /* ID_VAR */
    Ident(int t, ostd::ConstCharRange n, int m, int x, int *s, IdentFunc f = nullptr, int flags = 0)
        : type(t), flags(flags | (m > x ? IDF_READONLY : 0)), name(n), minval(m), maxval(x), fun(f) {
        storage.i = s;
    }
    /* ID_FVAR */
    Ident(int t, ostd::ConstCharRange n, float m, float x, float *s, IdentFunc f = nullptr, int flags = 0)
        : type(t), flags(flags | (m > x ? IDF_READONLY : 0)), name(n), minvalf(m), maxvalf(x), fun(f) {
        storage.f = s;
    }
    /* ID_SVAR */
    Ident(int t, ostd::ConstCharRange n, char **s, IdentFunc f = nullptr, int flags = 0)
        : type(t), flags(flags), name(n), fun(f) {
        storage.s = s;
    }
    /* ID_ALIAS */
    Ident(int t, ostd::ConstCharRange n, char *a, int flags)
        : type(t), valtype(VAL_STR), flags(flags), name(n), code(nullptr), stack(nullptr) {
        val.s = a;
    }
    Ident(int t, ostd::ConstCharRange n, int a, int flags)
        : type(t), valtype(VAL_INT), flags(flags), name(n), code(nullptr), stack(nullptr) {
        val.i = a;
    }
    Ident(int t, ostd::ConstCharRange n, float a, int flags)
        : type(t), valtype(VAL_FLOAT), flags(flags), name(n), code(nullptr), stack(nullptr) {
        val.f = a;
    }
    Ident(int t, ostd::ConstCharRange n, int flags)
        : type(t), valtype(VAL_NULL), flags(flags), name(n), code(nullptr), stack(nullptr) {
    }
    Ident(int t, ostd::ConstCharRange n, const TaggedValue &v, int flags)
        : type(t), valtype(v.type), flags(flags), name(n), code(nullptr), stack(nullptr) {
        val = v;
    }
    /* ID_COMMAND */
    Ident(int t, ostd::ConstCharRange n, const char *args, ostd::uint argmask, int numargs, IdentFunc f = nullptr, int flags = 0)
        : type(t), numargs(numargs), flags(flags), name(n), args(args ? dup_ostr(ostd::ConstCharRange(args)) : nullptr), argmask(argmask), fun(f) {
    }

    void changed(CsState &cs) {
        if (fun) fun(cs, this);
    }

    void setval(const TaggedValue &v) {
        valtype = v.type;
        val = v;
    }

    void setval(const IdentStack &v) {
        valtype = v.valtype;
        val = v.val;
    }

    void forcenull() {
        if (valtype == VAL_STR) delete[] val.s;
        valtype = VAL_NULL;
    }

    float get_float() const;
    int get_int() const;
    const char *get_str() const;
    void get_val(TaggedValue &r) const;
    void getcstr(TaggedValue &v) const;
    void getcval(TaggedValue &v) const;

    ostd::ConstCharRange get_key() const {
        return name.iter();
    }

    void clean_code();
};

struct CsState {
    ostd::Keyset<Ident> idents;
    ostd::Vector<Ident *> identmap;

    Ident *dummy = nullptr;
    TaggedValue *result = nullptr;

    int identflags = 0;

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
};

extern CsState cstate;

extern const char *intstr(int v);
extern const char *floatstr(float v);
extern void stringret(char *s);
extern void result(TaggedValue &v);
extern void result(const char *s);

static inline int parseint(const char *s) {
    return int(strtoul(s, nullptr, 0));
}

#define PARSEFLOAT(name, type) \
    static inline type parse##name(const char *s) \
    { \
        /* not all platforms (windows) can parse hexadecimal integers via strtod */ \
        char *end; \
        double val = strtod(s, &end); \
        return val || end==s || (*end!='x' && *end!='X') ? type(val) : type(parseint(s)); \
    }
PARSEFLOAT(float, float)
PARSEFLOAT(number, double)

static inline void intformat(char *buf, int v, int len = 20) {
    snprintf(buf, len, "%d", v);
}
static inline void floatformat(char *buf, float v, int len = 20) {
    snprintf(buf, len, v == int(v) ? "%.1f" : "%.7g", v);
}

static inline const char *get_str(const IdentValue &v, int type) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return v.s;
    case VAL_INT:
        return intstr(v.i);
    case VAL_FLOAT:
        return floatstr(v.f);
    default:
        return "";
    }
}
inline const char *TaggedValue::get_str() const {
    return ::get_str(*this, type);
}
inline const char *Ident::get_str() const {
    return ::get_str(val, valtype);
}

#define GETNUMBER(name, ret) \
    static inline ret get_##name(const IdentValue &v, int type) \
    { \
        switch(type) \
        { \
            case VAL_FLOAT: return ret(v.f); \
            case VAL_INT: return ret(v.i); \
            case VAL_STR: case VAL_MACRO: case VAL_CSTR: return parse##name(v.s); \
            default: return ret(0); \
        } \
    } \
    inline ret TaggedValue::get_##name() const { return ::get_##name(*this, type); } \
    inline ret Ident::get_##name() const { return ::get_##name(val, valtype); }
GETNUMBER(int, int)
GETNUMBER(float, float)

static inline void get_val(const IdentValue &v, int type, TaggedValue &r) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        r.set_str(dup_ostr(v.s));
        break;
    case VAL_INT:
        r.set_int(v.i);
        break;
    case VAL_FLOAT:
        r.set_float(v.f);
        break;
    default:
        r.set_null();
        break;
    }
}

inline void TaggedValue::get_val(TaggedValue &r) const {
    ::get_val(*this, type, r);
}
inline void Ident::get_val(TaggedValue &r) const {
    ::get_val(val, valtype, r);
}

inline void Ident::getcstr(TaggedValue &v) const {
    switch (valtype) {
    case VAL_MACRO:
        v.set_macro(val.code);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(val.s);
        break;
    case VAL_INT:
        v.set_str(dup_ostr(intstr(val.i)));
        break;
    case VAL_FLOAT:
        v.set_str(dup_ostr(floatstr(val.f)));
        break;
    default:
        v.set_cstr("");
        break;
    }
}

inline void Ident::getcval(TaggedValue &v) const {
    switch (valtype) {
    case VAL_MACRO:
        v.set_macro(val.code);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(val.s);
        break;
    case VAL_INT:
        v.set_int(val.i);
        break;
    case VAL_FLOAT:
        v.set_float(val.f);
        break;
    default:
        v.set_null();
        break;
    }
}

extern int variable(const char *name, int min, int cur, int max, int *storage, IdentFunc fun, int flags);
extern float fvariable(const char *name, float min, float cur, float max, float *storage, IdentFunc fun, int flags);
extern char *svariable(const char *name, const char *cur, char **storage, IdentFunc fun, int flags);
extern void setvar(const char *name, int i, bool dofunc = true, bool doclamp = true);
extern void setfvar(const char *name, float f, bool dofunc = true, bool doclamp = true);
extern void setsvar(const char *name, const char *str, bool dofunc = true);
extern void setvarchecked(Ident *id, int val);
extern void setfvarchecked(Ident *id, float val);
extern void setsvarchecked(Ident *id, const char *val);
extern int getvar(const char *name);
extern int getvarmin(const char *name);
extern int getvarmax(const char *name);
extern bool addcommand(const char *name, IdentFunc fun, const char *narg, int type = ID_COMMAND);
extern ostd::uint *compilecode(const char *p);
extern void keepcode(ostd::uint *p);
extern void freecode(ostd::uint *p);
extern void executeret(const ostd::uint *code, TaggedValue &result = *cstate.result);
extern void executeret(const char *p, TaggedValue &result = *cstate.result);
extern void executeret(Ident *id, TaggedValue *args, int numargs, bool lookup = false, TaggedValue &result = *cstate.result);
extern char *executestr(const ostd::uint *code);
extern char *executestr(const char *p);
extern char *executestr(Ident *id, TaggedValue *args, int numargs, bool lookup = false);
extern char *execidentstr(const char *name, bool lookup = false);
extern int execute(const ostd::uint *code);
extern int execute(const char *p);
extern int execute(Ident *id, TaggedValue *args, int numargs, bool lookup = false);
extern int execident(const char *name, int noid = 0, bool lookup = false);
extern float executefloat(const ostd::uint *code);
extern float executefloat(const char *p);
extern float executefloat(Ident *id, TaggedValue *args, int numargs, bool lookup = false);
extern float execidentfloat(const char *name, float noid = 0, bool lookup = false);
extern bool executebool(const ostd::uint *code);
extern bool executebool(const char *p);
extern bool executebool(Ident *id, TaggedValue *args, int numargs, bool lookup = false);
extern bool execidentbool(const char *name, bool noid = false, bool lookup = false);
extern bool execfile(const char *cfgfile, bool msg = true);
extern void alias(const char *name, const char *action);
extern void alias(const char *name, TaggedValue &v);
extern const char *getalias(const char *name);
extern const char *escapestring(const char *s);
extern const char *escapeid(const char *s);
static inline const char *escapeid(Ident &id) {
    return escapeid(id.name.data());
}
extern bool validateblock(const char *s);
void explodelist(const char *s, ostd::Vector<ostd::String> &elems, int limit = -1);
extern char *indexlist(const char *s, int pos);
extern int listlen(CsState &cs, const char *s);
extern void printvar(Ident *id);
extern void printvar(Ident *id, int i);
extern void printfvar(Ident *id, float f);
extern void printsvar(Ident *id, const char *s);
extern int clampvar(Ident *id, int i, int minval, int maxval);
extern float clampfvar(Ident *id, float f, float minval, float maxval);
extern void loopiter(Ident *id, IdentStack &stack, const TaggedValue &v);
extern void loopend(Ident *id, IdentStack &stack);

#define loopstart(id, stack) if((id)->type != ID_ALIAS) return; IdentStack stack;
static inline void loopiter(Ident *id, IdentStack &stack, int i) {
    TaggedValue v;
    v.set_int(i);
    loopiter(id, stack, v);
}
static inline void loopiter(Ident *id, IdentStack &stack, float f) {
    TaggedValue v;
    v.set_float(f);
    loopiter(id, stack, v);
}
static inline void loopiter(Ident *id, IdentStack &stack, const char *s) {
    TaggedValue v;
    v.set_str(dup_ostr(s));
    loopiter(id, stack, v);
}

void pusharg(Ident &id, const TaggedValue &v, IdentStack &stack);
void poparg(Ident &id);

#define KEYWORD(name, type) static bool __dummy_##type = addcommand(#name, (IdentFunc)nullptr, nullptr, type)
#define COMMANDKN(name, type, fun, nargs) static bool __dummy_##fun = addcommand(#name, (IdentFunc)fun, nargs, type)
#define COMMANDK(name, type, nargs) COMMANDKN(name, type, name, nargs)
#define COMMANDN(name, fun, nargs) COMMANDKN(name, ID_COMMAND, fun, nargs)
#define COMMAND(name, nargs) COMMANDN(name, name, nargs)

#define ICOMMANDNAME(name) _icmd_##name
#define ICOMMANDSNAME _icmds_
#define ICOMMANDKNS(name, type, cmdname, nargs, proto, b) template<int N> struct cmdname; template<> struct cmdname<__LINE__> { static bool init; static void run proto; }; bool cmdname<__LINE__>::init = addcommand(name, (IdentFunc)cmdname<__LINE__>::run, nargs, type); void cmdname<__LINE__>::run proto \
    { b; }
#define ICOMMANDKN(name, type, cmdname, nargs, proto, b) ICOMMANDKNS(#name, type, cmdname, nargs, proto, b)
#define ICOMMANDK(name, type, nargs, proto, b) ICOMMANDKN(name, type, ICOMMANDNAME(name), nargs, proto, b)
#define ICOMMANDKS(name, type, nargs, proto, b) ICOMMANDKNS(name, type, ICOMMANDSNAME, nargs, proto, b)
#define ICOMMANDNS(name, cmdname, nargs, proto, b) ICOMMANDKNS(name, ID_COMMAND, cmdname, nargs, proto, b)
#define ICOMMANDN(name, cmdname, nargs, proto, b) ICOMMANDNS(#name, cmdname, nargs, proto, b)
#define ICOMMAND(name, nargs, proto, b) ICOMMANDN(name, ICOMMANDNAME(name), nargs, proto, b)
#define ICOMMANDS(name, nargs, proto, b) ICOMMANDNS(name, ICOMMANDSNAME, nargs, proto, b)

void init_lib_math(CsState &cs);
void init_lib_shell(CsState &cs);