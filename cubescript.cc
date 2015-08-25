#include "cubescript.hh"

#include <limits.h>
#include <ctype.h>
#include <math.h>

#include <ostd/algorithm.hh>
#include <ostd/format.hh>
#include <ostd/array.hh>

namespace cscript {

static inline int parseint(const char *s) {
    return int(strtoul(s, nullptr, 0));
}

static inline int cs_parse_int(ostd::ConstCharRange s) {
    if (s.empty()) return 0;
    return parseint(s.data());
}

static inline float parsefloat(const char *s)
{
    /* not all platforms (windows) can parse hexadecimal integers via strtod */
    char *end;
    double val = strtod(s, &end);
    return val || end==s || (*end!='x' && *end!='X') ? float(val) : float(parseint(s));
}

static inline float cs_parse_float(ostd::ConstCharRange s) {
    if (s.empty()) return 0.0f;
    return parsefloat(s.data());
}

static inline void intformat(char *buf, int v, int len = 20) {
    snprintf(buf, len, "%d", v);
}
static inline void floatformat(char *buf, float v, int len = 20) {
    snprintf(buf, len, v == int(v) ? "%.1f" : "%.7g", v);
}

static char retbuf[4][256];
static int retidx = 0;

const char *intstr(int v) {
    retidx = (retidx + 1) % 4;
    intformat(retbuf[retidx], v);
    return retbuf[retidx];
}

const char *floatstr(float v) {
    retidx = (retidx + 1) % 4;
    floatformat(retbuf[retidx], v);
    return retbuf[retidx];
}

inline char *cs_dup_ostr(ostd::ConstCharRange s) {
    char *r = new char[s.size() + 1];
    memcpy(r, s.data(), s.size());
    r[s.size()] = 0;
    return r;
}

static inline bool cs_check_num(ostd::ConstCharRange s) {
    if (isdigit(s[0]))
        return true;
    switch (s[0]) {
    case '+':
    case '-':
        return isdigit(s[1]) || (s[1] == '.' && isdigit(s[2]));
    case '.':
        return isdigit(s[1]) != 0;
    default:
        return false;
    }
}

/* ID_VAR */
Ident::Ident(int t, ostd::ConstCharRange n, int m, int x, int *s,
             IdentFunc f, int flags)
    : type(t), flags(flags | (m > x ? IDF_READONLY : 0)), name(n),
      minval(m), maxval(x), fun(f) {
    storage.ip = s;
}

/* ID_FVAR */
Ident::Ident(int t, ostd::ConstCharRange n, float m, float x, float *s,
             IdentFunc f, int flags)
    : type(t), flags(flags | (m > x ? IDF_READONLY : 0)), name(n),
      minvalf(m), maxvalf(x), fun(f) {
    storage.fp = s;
}

/* ID_SVAR */
Ident::Ident(int t, ostd::ConstCharRange n, char **s, IdentFunc f, int flags)
    : type(t), flags(flags), name(n), fun(f) {
    storage.sp = s;
}

/* ID_ALIAS */
Ident::Ident(int t, ostd::ConstCharRange n, char *a, int flags)
    : type(t), valtype(VAL_STR | (n.size() << 4)), flags(flags), name(n), code(nullptr),
      stack(nullptr) {
    val.s = a;
}
Ident::Ident(int t, ostd::ConstCharRange n, int a, int flags)
    : type(t), valtype(VAL_INT), flags(flags), name(n), code(nullptr),
      stack(nullptr) {
    val.i = a;
}
Ident::Ident(int t, ostd::ConstCharRange n, float a, int flags)
    : type(t), valtype(VAL_FLOAT), flags(flags), name(n), code(nullptr),
      stack(nullptr) {
    val.f = a;
}
Ident::Ident(int t, ostd::ConstCharRange n, int flags)
    : type(t), valtype(VAL_NULL), flags(flags), name(n), code(nullptr),
      stack(nullptr) {
}
Ident::Ident(int t, ostd::ConstCharRange n, const TaggedValue &v, int flags)
    : type(t), valtype(v.p_type), flags(flags), name(n), code(nullptr),
      stack(nullptr) {
    val = v;
}

/* ID_COMMAND */
Ident::Ident(int t, ostd::ConstCharRange n, ostd::ConstCharRange args,
             ostd::Uint32 argmask, int numargs, IdentFunc f, int flags)
    : type(t), numargs(numargs), flags(flags), name(n),
      args(!args.empty() ? cs_dup_ostr(args) : nullptr),
      argmask(argmask), fun(f) {
}

const struct NullValue: TaggedValue {
    NullValue() { set_null(); }
} null_value;

static TaggedValue no_ret = null_value;

CsState::CsState(): result(&no_ret) {
    for (int i = 0; i < MAX_ARGUMENTS; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident((const char *)buf, IDF_ARG);
    }
    dummy = new_ident("//dummy");
    add_ident(ID_VAR, "numargs", MAX_ARGUMENTS, 0, &numargs);
    add_ident(ID_VAR, "dbgalias", 0, 1000, &dbgalias); 
}

CsState::~CsState() {
    for (Ident &i: idents.iter()) {
        if (i.type == ID_ALIAS) {
            i.force_null();
            delete[] i.code;
            i.code = nullptr;
        } else if (i.type == ID_COMMAND || i.type >= ID_LOCAL) {
            delete[] i.args;
        }
    }
}

ostd::ConstCharRange cs_debug_line(CsState &cs,
                                   ostd::ConstCharRange p,
                                   ostd::ConstCharRange fmt,
                                   ostd::CharRange buf) {
    if (cs.src_str.empty()) return fmt;
    ostd::Size num = 1;
    ostd::ConstCharRange line(cs.src_str);
    for (;;) {
        ostd::ConstCharRange end = ostd::find(line, '\n');
        if (!end.empty())
            line = ostd::slice_until(line, end);
        if (&p[0] >= &line[0] && &p[0] <= &line[line.size()]) {
            ostd::CharRange r(buf);
            if (!cs.src_file.empty())
                ostd::format(r, "%s:%d: %s", cs.src_file, num, fmt);
            else
                ostd::format(r, "%d: %s", num, fmt);
            r.put('\0');
            return buf;
        }
        if (end.empty()) break;
        line = end;
        line.pop_front();
        ++num;
    }
    return fmt;
}

void cs_debug_alias(CsState &cs) {
    if (!cs.dbgalias) return;
    int total = 0, depth = 0;
    for (IdentLink *l = cs.stack; l != &cs.noalias; l = l->next) total++;
    for (IdentLink *l = cs.stack; l != &cs.noalias; l = l->next) {
        Ident *id = l->id;
        ++depth;
        if (depth < cs.dbgalias)
            ostd::err.writefln("  %d) %s", total - depth + 1, id->name);
        else if (l->next == &cs.noalias)
            ostd::err.writefln(depth == cs.dbgalias ? "  %d) %s"
                                                    : "  ..%d) %s",
                               total - depth + 1, id->name);
    }
}

template<typename ...A>
void cs_debug_code(CsState &cs, ostd::ConstCharRange fmt, A &&...args) {
    if (cs.nodebug) return;
    ostd::err.writefln(fmt, ostd::forward<A>(args)...);
    cs_debug_alias(cs);
}

template<typename ...A>
void cs_debug_code_line(CsState &cs, ostd::ConstCharRange p,
                        ostd::ConstCharRange fmt, A &&...args) {
    if (cs.nodebug) return;
    ostd::Array<char, 256> buf;
    ostd::err.writefln(cs_debug_line(cs, p, fmt, ostd::CharRange(buf.data(),
                                                                 buf.size())),
                       ostd::forward<A>(args)...);
    cs_debug_alias(cs);
}

void CsState::clear_override(Ident &id) {
    if (!(id.flags & IDF_OVERRIDDEN)) return;
    switch (id.type) {
    case ID_ALIAS:
        if (id.get_valtype() == VAL_STR) {
            if (!id.val.s[0]) break;
            delete[] id.val.s;
        }
        id.clean_code();
        id.valtype = VAL_STR;
        id.val.s = cs_dup_ostr("");
        break;
    case ID_VAR:
        *id.storage.ip = id.overrideval.i;
        id.changed(*this);
        break;
    case ID_FVAR:
        *id.storage.fp = id.overrideval.f;
        id.changed(*this);
        break;
    case ID_SVAR:
        delete[] *id.storage.sp;
        *id.storage.sp = id.overrideval.s;
        id.changed(*this);
        break;
    }
    id.flags &= ~IDF_OVERRIDDEN;
}

void CsState::clear_overrides() {
    for (Ident &id: idents.iter())
        clear_override(id);
}

Ident *CsState::new_ident(ostd::ConstCharRange name, int flags) {
    Ident *id = idents.at(name);
    if (!id) {
        if (cs_check_num(name)) {
            cs_debug_code(*this, "number %s is not a valid identifier name",
                          name);
            return dummy;
        }
        id = add_ident(ID_ALIAS, name, flags);
    }
    return id;
}

Ident *CsState::force_ident(TaggedValue &v) {
    switch (v.get_type()) {
    case VAL_IDENT:
        return v.id;
    case VAL_MACRO:
    case VAL_CSTR: {
        Ident *id = new_ident(v.s);
        v.set_ident(id);
        return id;
    }
    case VAL_STR: {
        Ident *id = new_ident(v.s);
        delete[] v.s;
        v.set_ident(id);
        return id;
    }
    }
    v.cleanup();
    v.set_ident(dummy);
    return dummy;
}

bool CsState::reset_var(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id) return false;
    if (id->flags & IDF_READONLY) {
        cs_debug_code(*this, "variable %s is read only", id->name);
        return false;
    }
    clear_override(*id);
    return true;
}

void CsState::touch_var(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (id) switch (id->type) {
    case ID_VAR:
    case ID_FVAR:
    case ID_SVAR:
        id->changed(*this);
        break;
    }
}

void CsState::set_alias(ostd::ConstCharRange name, TaggedValue &v) {
    Ident *id = idents.at(name);
    if (id) {
        switch (id->type) {
        case ID_ALIAS:
            if (id->index < MAX_ARGUMENTS)
                id->set_arg(*this, v);
            else
                id->set_alias(*this, v);
            return;
        case ID_VAR:
            set_var_int_checked(id, v.get_int());
            break;
        case ID_FVAR:
            set_var_float_checked(id, v.get_float());
            break;
        case ID_SVAR:
            set_var_str_checked(id, v.get_str());
            break;
        default:
            cs_debug_code(*this, "cannot redefine builtin %s with an alias",
                          id->name);
            break;
        }
        v.cleanup();
    } else if (cs_check_num(name)) {
        cs_debug_code(*this, "cannot alias number %s", name);
        v.cleanup();
    } else {
        add_ident(ID_ALIAS, name, v, identflags);
    }
}

void CsState::print_var_int(Ident *id, int i) {
    if (i < 0) {
        writefln("%s = %d", id->name, i);
        return;
    }
    if (id->flags & IDF_HEX) {
        if (id->maxval == 0xFFFFFF)
            writefln("%s = 0x%.6X (%d, %d, %d)", id->name,
                     i, (i >> 16) & 0xFF, (i >> 8) & 0xFF, i & 0xFF);
        else
            writefln("%s = 0x%X", id->name, i);
        return;
    }
    writefln("%s = %d", id->name, i);
}

void CsState::print_var_float(Ident *id, float f) {
    writefln("%s = %s", id->name, floatstr(f));
}

void CsState::print_var_str(Ident *id, ostd::ConstCharRange s) {
    if (ostd::find(s, '"').empty())
        writefln("%s = \"%s\"", id->name, s);
    else
        writefln("%s = [%s]", id->name, s);
}

void CsState::print_var(Ident *id) {
    switch (id->type) {
    case ID_VAR:
        print_var_int(id, *id->storage.ip);
        break;
    case ID_FVAR:
        print_var_float(id, *id->storage.fp);
        break;
    case ID_SVAR:
        print_var_str(id, *id->storage.sp);
        break;
    }
}

inline void TaggedValue::cleanup() {
    switch (get_type()) {
    case VAL_STR:
        delete[] s;
        break;
    case VAL_CODE:
        if (code[-1] == CODE_START) delete[] (ostd::byte *)&code[-1];
        break;
    }
}

inline void TaggedValue::force_null() {
    if (get_type() == VAL_NULL) return;
    cleanup();
    set_null();
}

inline float TaggedValue::force_float() {
    float rf = 0.0f;
    switch (get_type()) {
    case VAL_INT:
        rf = i;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        rf = parsefloat(s);
        break;
    case VAL_FLOAT:
        return f;
    }
    cleanup();
    set_float(rf);
    return rf;
}

inline int TaggedValue::force_int() {
    int ri = 0;
    switch (get_type()) {
    case VAL_FLOAT:
        ri = f;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        ri = parseint(s);
        break;
    case VAL_INT:
        return i;
    }
    cleanup();
    set_int(ri);
    return ri;
}

inline ostd::ConstCharRange TaggedValue::force_str() {
    const char *rs = "";
    switch (get_type()) {
    case VAL_FLOAT:
        rs = floatstr(f);
        break;
    case VAL_INT:
        rs = intstr(i);
        break;
    case VAL_MACRO:
    case VAL_CSTR:
        rs = s;
        break;
    case VAL_STR:
        return s;
    }
    cleanup();
    set_str_dup(rs);
    return s;
}

inline void TaggedValue::force(int type) {
    switch (get_type()) {
    case RET_STR:
        if (type != VAL_STR) force_str();
        break;
    case RET_INT:
        if (type != VAL_INT) force_int();
        break;
    case RET_FLOAT:
        if (type != VAL_FLOAT) force_float();
        break;
    }
}

static inline int cs_get_int(const IdentValue &v, int type) {
    switch (type) {
    case VAL_FLOAT:
        return int(v.f);
    case VAL_INT:
        return v.i;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return parseint(v.s);
    }
    return 0;
}

inline int TaggedValue::get_int() const {
    return cs_get_int(*this, get_type());
}

inline int Ident::get_int() const {
    return cs_get_int(val, get_valtype());
}

static inline float cs_get_float(const IdentValue &v, int type) {
    switch (type) {
    case VAL_FLOAT:
        return v.f;
    case VAL_INT:
        return float(v.i);
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return parsefloat(v.s);
    }
    return 0.0f;
}

inline float TaggedValue::get_float() const {
    return cs_get_float(*this, get_type());
}

inline float Ident::get_float() const {
    return cs_get_float(val, get_valtype());
}

static inline ostd::ConstCharRange cs_get_str(const IdentValue &v, int type, int len) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return ostd::ConstCharRange(v.s, len);
    case VAL_INT:
        return intstr(v.i);
    case VAL_FLOAT:
        return floatstr(v.f);
    }
    return "";
}

inline ostd::ConstCharRange TaggedValue::get_str() const {
    return cs_get_str(*this, get_type(), p_type >> 4);
}

inline ostd::ConstCharRange Ident::get_str() const {
    return cs_get_str(val, get_valtype(), valtype >> 4);
}

static inline void cs_get_val(const IdentValue &v, int type, int len, TaggedValue &r) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR: {
        r.set_str_dup(ostd::ConstCharRange(v.s, len));
        break;
    }
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
    cs_get_val(*this, get_type(), p_type >> 4, r);
}

inline void Ident::get_val(TaggedValue &r) const {
    cs_get_val(val, get_valtype(), valtype >> 4, r);
}

inline void Ident::get_cstr(TaggedValue &v) const {
    switch (get_valtype()) {
    case VAL_MACRO:
        v.set_macro(val.code);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(ostd::ConstCharRange(val.s, valtype >> 4));
        break;
    case VAL_INT:
        v.set_str_dup(intstr(val.i));
        break;
    case VAL_FLOAT:
        v.set_str_dup(floatstr(val.f));
        break;
    default:
        v.set_cstr("");
        break;
    }
}

inline void Ident::get_cval(TaggedValue &v) const {
    switch (get_valtype()) {
    case VAL_MACRO:
        v.set_macro(val.code);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(ostd::ConstCharRange(val.s, valtype >> 4));
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

static inline void free_args(TaggedValue *args, int &oldnum, int newnum) {
    for (int i = newnum; i < oldnum; i++) args[i].cleanup();
    oldnum = newnum;
}

void Ident::clean_code() {
    if (code) {
        code[0] -= 0x100;
        if (int(code[0]) < 0x100) delete[] code;
        code = nullptr;
    }
}

void Ident::push_arg(const TaggedValue &v, IdentStack &st, bool um) {
    st.val = val;
    st.valtype = valtype;
    st.next = stack;
    stack = &st;
    set_value(v);
    clean_code();
    if (um)
        flags &= ~IDF_UNKNOWN;
}

void Ident::pop_arg() {
    if (!stack) return;
    IdentStack *st = stack;
    if (get_valtype() == VAL_STR) delete[] val.s;
    set_value(*stack);
    clean_code();
    stack = st->next;
}

void Ident::undo_arg(IdentStack &st) {
    IdentStack *prev = stack;
    st.val = val;
    st.valtype = valtype;
    st.next = prev;
    stack = prev->next;
    set_value(*prev);
    clean_code();
}

void Ident::redo_arg(const IdentStack &st) {
    IdentStack *prev = st.next;
    prev->val = val;
    prev->valtype = valtype;
    stack = prev;
    set_value(st);
    clean_code();
}

void Ident::push_alias(IdentStack &stack) {
    if (type == ID_ALIAS && index >= MAX_ARGUMENTS)
        push_arg(null_value, stack);
}

void Ident::pop_alias() {
    if (type == ID_ALIAS && index >= MAX_ARGUMENTS) pop_arg();
}

void Ident::set_arg(CsState &cs, TaggedValue &v) {
    if (cs.stack->usedargs & (1 << index)) {
        if (get_valtype() == VAL_STR) delete[] val.s;
        set_value(v);
        clean_code();
    } else {
        push_arg(v, cs.stack->argstack[index], false);
        cs.stack->usedargs |= 1 << index;
    }
}

void Ident::set_alias(CsState &cs, TaggedValue &v) {
    if (get_valtype() == VAL_STR) delete[] val.s;
    set_value(v);
    clean_code();
    flags = (flags & cs.identflags) | cs.identflags;
}

template<typename F>
static void cs_do_args(CsState &cs, F body) {
    IdentStack argstack[MAX_ARGUMENTS];
    int argmask1 = cs.stack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) if(argmask1 & 1)
        cs.identmap[i]->undo_arg(argstack[i]);
    IdentLink *prevstack = cs.stack->next;
    IdentLink aliaslink = {
        cs.stack->id, cs.stack, prevstack->usedargs, prevstack->argstack
    };
    cs.stack = &aliaslink;
    body();
    prevstack->usedargs = aliaslink.usedargs;
    cs.stack = aliaslink.next;
    int argmask2 = cs.stack->usedargs;
    for(int i = 0; argmask2; argmask2 >>= 1, ++i) if(argmask2 & 1)
        cs.identmap[i]->redo_arg(argstack[i]);
}

template<typename SF, typename RF, typename CF>
bool cs_override_var(CsState &cs, Ident *id, SF sf, RF rf, CF cf) {
    if ((cs.identflags & IDF_OVERRIDDEN) || (id->flags & IDF_OVERRIDE)) {
        if (id->flags & IDF_PERSIST) {
            cs_debug_code(cs, "cannot override persistent variable '%s'",
                          id->name);
            return false;
        }
        if (!(id->flags & IDF_OVERRIDDEN)) {
            sf();
            id->flags |= IDF_OVERRIDDEN;
        } else cf();
    } else {
        if (id->flags & IDF_OVERRIDDEN) {
            rf();
            id->flags &= ~IDF_OVERRIDDEN;
        }
        cf();
    }
    return true;
}

void CsState::set_var_int(ostd::ConstCharRange name, int v,
                          bool dofunc, bool doclamp) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_VAR)
        return;
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.i = *id->storage.ip; },
        []() {}, []() {});
    if (!success)
        return;
    if (doclamp)
        *id->storage.ip = ostd::clamp(v, id->minval, id->maxval);
    else
        *id->storage.ip = v;
    if (dofunc)
        id->changed(*this);
}

void CsState::set_var_float(ostd::ConstCharRange name, float v,
                            bool dofunc, bool doclamp) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_FVAR)
        return;
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.f = *id->storage.fp; },
        []() {}, []() {});
    if (!success)
        return;
    if (doclamp)
        *id->storage.fp = ostd::clamp(v, id->minvalf, id->maxvalf);
    else
        *id->storage.fp = v;
    if (dofunc)
        id->changed(*this);
}

void CsState::set_var_str(ostd::ConstCharRange name, ostd::ConstCharRange v,
                          bool dofunc) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_SVAR)
        return;
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.s = *id->storage.sp; },
        [&id]() { delete[] id->overrideval.s; },
        [&id]() { delete[] *id->storage.sp; });
    if (!success)
        return;
    *id->storage.sp = cs_dup_ostr(v);
    if (dofunc)
        id->changed(*this);
}

ostd::Maybe<int> CsState::get_var_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_VAR)
        return ostd::nothing;
    return *id->storage.ip;
}

ostd::Maybe<float> CsState::get_var_float(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_FVAR)
        return ostd::nothing;
    return *id->storage.fp;
}

ostd::Maybe<ostd::String> CsState::get_var_str(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_SVAR)
        return ostd::nothing;
    return ostd::String(*id->storage.sp);
}

ostd::Maybe<int> CsState::get_var_min_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_VAR)
        return ostd::nothing;
    return id->minval;
}

ostd::Maybe<int> CsState::get_var_max_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_VAR)
        return ostd::nothing;
    return id->maxval;
}

ostd::Maybe<float> CsState::get_var_min_float(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_FVAR)
        return ostd::nothing;
    return id->minvalf;
}

ostd::Maybe<float> CsState::get_var_max_float(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_FVAR)
        return ostd::nothing;
    return id->maxvalf;
}

ostd::Maybe<ostd::ConstCharRange>
CsState::get_alias(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_ALIAS)
        return ostd::nothing;
    if ((id->index < MAX_ARGUMENTS) && !(stack->usedargs & (1 << id->index)))
        return ostd::nothing;
    return id->get_str();
}

int cs_clamp_var(CsState &cs, Ident *id, int v) {
    if (v < id->minval)
        v = id->minval;
    else if (v > id->maxval)
        v = id->maxval;
    else
        return v;
    cs_debug_code(cs, (id->flags & IDF_HEX)
                       ? ((id->minval <= 255)
                          ? "valid range for '%s' is %d..0x%X"
                          : "valid range for '%s' is 0x%X..0x%X")
                       : "valid range for '%s' is %d..%d",
                  id->name, id->minval, id->maxval);
    return v;
}

void CsState::set_var_int_checked(Ident *id, int v) {
    if (id->flags & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", id->name);
        return;
    }
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.i = *id->storage.ip; },
        []() {}, []() {});
    if (!success)
        return;
    if (v < id->minval || v > id->maxval)
        v = cs_clamp_var(*this, id, v);
    *id->storage.ip = v;
    id->changed(*this);
}

void CsState::set_var_int_checked(Ident *id,
                                  ostd::PointerRange<TaggedValue> args) {
    int v = args[0].force_int();
    if ((id->flags & IDF_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2)
            v |= args[2].force_int();
    }
    set_var_int_checked(id, v);
}

float cs_clamp_fvar(CsState &cs, Ident *id, float v) {
    if (v < id->minvalf)
        v = id->minvalf;
    else if (v > id->maxvalf)
        v = id->maxvalf;
    else
        return v;
    cs_debug_code(cs, "valid range for '%s' is %s..%s", floatstr(id->minvalf),
                  floatstr(id->maxvalf));
    return v;
}

void CsState::set_var_float_checked(Ident *id, float v) {
    if (id->flags & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", id->name);
        return;
    }
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.f = *id->storage.fp; },
        []() {}, []() {});
    if (!success)
        return;
    if (v < id->minvalf || v > id->maxvalf)
        v = cs_clamp_fvar(*this, id, v);
    *id->storage.fp = v;
    id->changed(*this);
}

void CsState::set_var_str_checked(Ident *id, ostd::ConstCharRange v) {
    if (id->flags & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", id->name);
        return;
    }
    bool success = cs_override_var(*this, id,
        [&id]() { id->overrideval.s = *id->storage.sp; },
        [&id]() { delete[] id->overrideval.s; },
        [&id]() { delete[] *id->storage.sp; });
    if (!success) return;
    *id->storage.sp = cs_dup_ostr(v);
    id->changed(*this);
}

bool CsState::add_command(ostd::ConstCharRange name, ostd::ConstCharRange args,
                          IdentFunc func, int type) {
    ostd::Uint32 argmask = 0;
    int nargs = 0;
    bool limit = true;
    ostd::ConstCharRange fmt(args);
    for (; !fmt.empty(); fmt.pop_front()) {
        switch (fmt.front()) {
        case 'i':
        case 'b':
        case 'f':
        case 'F':
        case 't':
        case 'T':
        case 'E':
        case 'N':
        case 'D':
            if (nargs < MAX_ARGUMENTS) nargs++;
            break;
        case 'S':
        case 's':
        case 'e':
        case 'r':
        case '$':
            if (nargs < MAX_ARGUMENTS) {
                argmask |= 1 << nargs;
                nargs++;
            }
            break;
        case '1':
        case '2':
        case '3':
        case '4':
            if (nargs < MAX_ARGUMENTS)
                fmt.push_front_n(fmt.front() - '0' + 1);
            break;
        case 'C':
        case 'V':
            limit = false;
            break;
        default:
            ostd::err.writefln("builtin %s declared with illegal type: %c",
                               name, fmt.front());
            return false;
        }
    }
    if (limit && nargs > MAX_COMARGS) {
        ostd::err.writefln("builtin %s declared with too many arguments: %d",
                           name, nargs);
        return false;
    }
    add_ident(type, name, args, argmask, nargs, func);
    return true;
}

static void cs_init_lib_base_var(CsState &cs) {
    cs.add_command("nodebug", "e", [](CsState &cs, ostd::Uint32 *body) {
        ++cs.nodebug;
        cs.run_ret(body);
        --cs.nodebug;
    });

    cs.add_command("push", "rTe", [](CsState &cs, Ident *id,
                                     TaggedValue *v, ostd::Uint32 *code) {
        if (id->type != ID_ALIAS || id->index < MAX_ARGUMENTS) return;
        IdentStack stack;
        id->push_arg(*v, stack);
        v->set_null();
        cs.run_ret(code);
        id->pop_arg();
    });

    cs.add_command("local", nullptr, nullptr, ID_LOCAL);

    cs.add_command("resetvar", "s", [](CsState &cs, char *name) {
        cs.result->set_int(cs.reset_var(name));
    });

    cs.add_command("alias", "sT", [](CsState &cs, const char *name,
                                     TaggedValue *v) {
        cs.set_alias(name, *v);
        v->set_null();
    });

    cs.add_command("getvarmin", "s", [](CsState &cs, const char *name) {
        cs.result->set_int(cs.get_var_min_int(name).value_or(0));
    });
    cs.add_command("getvarmax", "s", [](CsState &cs, const char *name) {
        cs.result->set_int(cs.get_var_max_int(name).value_or(0));
    });
    cs.add_command("getfvarmin", "s", [](CsState &cs, const char *name) {
        cs.result->set_float(cs.get_var_min_float(name).value_or(0.0f));
    });
    cs.add_command("getfvarmax", "s", [](CsState &cs, const char *name) {
        cs.result->set_float(cs.get_var_max_float(name).value_or(0.0f));
    });

    cs.add_command("identexists", "s", [](CsState &cs, const char *name) {
        cs.result->set_int(cs.have_ident(name));
    });

    cs.add_command("getalias", "s", [](CsState &cs, const char *name) {
        cs.result->set_str_dup(cs.get_alias(name).value_or(""));
    });
}

const char *parsestring(const char *p) {
    for (; *p; p++) switch (*p) {
        case '\r':
        case '\n':
        case '\"':
            return p;
        case '^':
            if (*++p) break;
            return p;
        }
    return p;
}

ostd::ConstCharRange cs_parse_str(ostd::ConstCharRange str) {
    for (; !str.empty(); str.pop_front())
        switch (str.front()) {
        case '\r':
        case '\n':
        case '\"':
            return str;
        case '^':
            str.pop_front();
            if (!str.empty()) break;
            return str;
        }
    return str;
}

static char *conc(ostd::Vector<char> &buf, ostd::PointerRange<TaggedValue> v, bool space, const char *prefix = nullptr, int prefixlen = 0) {
    if (prefix) {
        buf.push_n(prefix, prefixlen);
        if (space && !v.empty()) buf.push(' ');
    }
    for (ostd::Size i = 0; i < v.size(); ++i) {
        const char *s = "";
        int len = 0;
        switch (v[i].get_type()) {
        case VAL_INT:
            s = intstr(v[i].i);
            break;
        case VAL_FLOAT:
            s = floatstr(v[i].f);
            break;
        case VAL_STR:
        case VAL_CSTR:
            s = v[i].s;
            break;
        case VAL_MACRO:
            s = v[i].s;
            len = v[i].code[-1] >> 8;
            goto haslen;
        }
        len = int(strlen(s));
haslen:
        buf.push_n(s, len);
        if (i == v.size() - 1) break;
        if (space) buf.push(' ');
    }
    buf.push('\0');
    return buf.data();
}

static char *conc(ostd::PointerRange<TaggedValue> v, bool space, const char *prefix, int prefixlen) {
    static int vlen[MAX_ARGUMENTS];
    static char numbuf[3 * 256];
    int len = prefixlen, numlen = 0, i = 0;
    for (; i < int(v.size()); i++) switch (v[i].get_type()) {
        case VAL_MACRO:
            len += (vlen[i] = v[i].code[-1] >> 8);
            break;
        case VAL_STR:
        case VAL_CSTR:
            len += (vlen[i] = int(strlen(v[i].s)));
            break;
        case VAL_INT:
            if (numlen + 256 > int(sizeof(numbuf))) goto overflow;
            intformat(&numbuf[numlen], v[i].i);
            numlen += (vlen[i] = strlen(&numbuf[numlen]));
            break;
        case VAL_FLOAT:
            if (numlen + 256 > int(sizeof(numbuf))) goto overflow;
            floatformat(&numbuf[numlen], v[i].f);
            numlen += (vlen[i] = strlen(&numbuf[numlen]));
            break;
        default:
            vlen[i] = 0;
            break;
        }
overflow:
    if (space) len += ostd::max(prefix ? i : i - 1, 0);
    char *buf = new char[len + numlen + 1];
    int offset = 0, numoffset = 0;
    if (prefix) {
        memcpy(buf, prefix, prefixlen);
        offset += prefixlen;
        if (space && i) buf[offset++] = ' ';
    }
    for (ostd::Size j = 0; j < ostd::Size(i); ++j) {
        if (v[j].get_type() == VAL_INT || v[j].get_type() == VAL_FLOAT) {
            memcpy(&buf[offset], &numbuf[numoffset], vlen[j]);
            numoffset += vlen[j];
        } else if (vlen[j]) memcpy(&buf[offset], v[j].s, vlen[j]);
        offset += vlen[j];
        if (j == ostd::Size(i) - 1) break;
        if (space) buf[offset++] = ' ';
    }
    buf[offset] = '\0';
    if (i < int(v.size())) {
        char *morebuf = conc(ostd::iter(&v[i], v.size() - i), space, buf, offset);
        delete[] buf;
        return morebuf;
    }
    return buf;
}

static inline char *conc(ostd::PointerRange<TaggedValue> v, bool space) {
    return conc(v, space, nullptr, 0);
}

static inline void skipcomments(const char *&p) {
    for (;;) {
        p += strspn(p, " \t\r");
        if (p[0] != '/' || p[1] != '/') break;
        p += strcspn(p, "\n\0");
    }
}

static ostd::Vector<char> strbuf[4];
static int stridx = 0;

static inline void cutstring(const char *&p, ostd::ConstCharRange &s) {
    p++;
    const char *end = parsestring(p);
    int maxlen = int(end - p) + 1;

    stridx = (stridx + 1) % 4;
    ostd::Vector<char> &buf = strbuf[stridx];
    buf.reserve(maxlen);

    auto writer = buf.iter_cap();
    s = ostd::ConstCharRange(buf.data(),
        util::unescape_string(writer, ostd::ConstCharRange(p, end)));
    writer.put('\0');
    p = end;
    if (*p == '\"') p++;
}

static inline char *cutstring(const char *&p) {
    p++;
    const char *end = parsestring(p);
    char *buf = new char[end - p + 1];
    auto writer = ostd::CharRange(buf, end - p + 1);
    util::unescape_string(writer, ostd::ConstCharRange(p, end));
    writer.put('\0');
    p = end;
    if (*p == '\"') p++;
    return buf;
}

static inline const char *parseword(const char *p) {
    const int maxbrak = 100;
    static char brakstack[maxbrak];
    int brakdepth = 0;
    for (;; p++) {
        p += strcspn(p, "\"/;()[] \t\r\n\0");
        switch (p[0]) {
        case '"':
        case ';':
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\0':
            return p;
        case '/':
            if (p[1] == '/') return p;
            break;
        case '[':
        case '(':
            if (brakdepth >= maxbrak) return p;
            brakstack[brakdepth++] = p[0];
            break;
        case ']':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '[') return p;
            break;
        case ')':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '(') return p;
            break;
        }
    }
    return p;
}

static inline void cutword(const char *&p, ostd::ConstCharRange &s) {
    const char *op = p;
    p = parseword(p);
    s = ostd::ConstCharRange(op, p - op);
}

static inline char *cutword(const char *&p) {
    const char *word = p;
    p = parseword(p);
    return p != word ? cs_dup_ostr(ostd::ConstCharRange(word, p - word)) : nullptr;
}

static inline int cs_ret_code(int type, int def = 0) {
    return (type >= VAL_ANY) ? ((type == VAL_CSTR) ? RET_STR : def)
                             : (type << CODE_RET);
}

struct GenState;

static void compilestatements(GenState &gs, int rettype, int brak = '\0', int prevargs = 0);
static inline const char *compileblock(GenState &gs, const char *p, int rettype = RET_NULL, int brak = '\0');

struct GenState {
    CsState &cs;
    ostd::Vector<ostd::Uint32> code;
    const char *source;

    GenState() = delete;
    GenState(CsState &cs): cs(cs), code(), source(nullptr) {}

    void gen_str(ostd::ConstCharRange word, bool macro = false) {
        if (word.size() <= 3 && !macro) {
            ostd::Uint32 op = CODE_VALI | RET_STR;
            for (ostd::Size i = 0; i < word.size(); ++i)
                op |= ostd::Uint32(ostd::byte(word[i])) << ((i + 1) * 8);
            code.push(op);
            return;
        }
        code.push((macro ? CODE_MACRO : (CODE_VAL | RET_STR)) |
                  (word.size() << 8));
        code.push_n((const ostd::Uint32 *)word.data(),
                    word.size() / sizeof(ostd::Uint32));
        ostd::Size esz = word.size() % sizeof(ostd::Uint32);
        union {
            char c[sizeof(ostd::Uint32)];
            ostd::Uint32 u;
        } end;
        end.u = 0;
        memcpy(end.c, word.data() + word.size() - esz, esz);
        code.push(end.u);
    }

    void gen_str() {
        code.push(CODE_VALI | RET_STR);
    }

    void gen_null() {
        code.push(CODE_VALI | RET_NULL);
    }

    void gen_int(int i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF)
            code.push(CODE_VALI | RET_INT | (i << 8));
        else {
            code.push(CODE_VAL | RET_INT);
            code.push(i);
        }
    }

    void gen_int(ostd::ConstCharRange word) {
        gen_int(cs_parse_int(word));
    }

    void gen_float(float f = 0.0f) {
        if (int(f) == f && f >= -0x800000 && f <= 0x7FFFFF)
            code.push(CODE_VALI | RET_FLOAT | (int(f) << 8));
        else {
            union {
                float f;
                ostd::Uint32 u;
            } c;
            c.f = f;
            code.push(CODE_VAL | RET_FLOAT);
            code.push(c.u);
        }
    }

    void gen_float(ostd::ConstCharRange word) {
        gen_float(cs_parse_float(word));
    }

    void gen_ident(Ident *id) {
        code.push(((id->index < MAX_ARGUMENTS) ? CODE_IDENTARG
                                               : CODE_IDENT) |
                  (id->index << 8));
    }

    void gen_ident() {
        gen_ident(cs.dummy);
    }

    void gen_ident(ostd::ConstCharRange word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(int wordtype, ostd::ConstCharRange word
                                     = ostd::ConstCharRange()) {
        switch (wordtype) {
        case VAL_CANY:
            if (!word.empty())
                gen_str(word, true);
            else
                gen_null();
            break;
        case VAL_CSTR:
            gen_str(word, true);
            break;
        case VAL_ANY:
            if (!word.empty())
                gen_str(word);
            else
                gen_null();
            break;
        case VAL_STR:
            gen_str(word);
            break;
        case VAL_FLOAT:
            gen_float(word);
            break;
        case VAL_INT:
            gen_int(word);
            break;
        case VAL_COND:
            if (!word.empty())
                compileblock(*this, word.data());
            else
                gen_null();
            break;
        case VAL_CODE:
            compileblock(*this, word.data());
            break;
        case VAL_IDENT:
            gen_ident(word);
            break;
        default:
            break;
        }
    }

    void gen_main(ostd::ConstCharRange s, int ret_type = VAL_ANY);

    char next_char() {
        return *source++;
    }

    char current() {
        return *source;
    }
};

static inline void compileblock(GenState &gs) {
    gs.code.push(CODE_EMPTY);
}

static inline const char *compileblock(GenState &gs, const char *p, int rettype, int brak) {
    ostd::Size start = gs.code.size();
    gs.code.push(CODE_BLOCK);
    gs.code.push(CODE_OFFSET | ((start + 2) << 8));
    if (p) {
        const char *op = gs.source;
        gs.source = p;
        compilestatements(gs, VAL_ANY, brak);
        p = gs.source;
        gs.source = op;
    }
    if (gs.code.size() > start + 2) {
        gs.code.push(CODE_EXIT | rettype);
        gs.code[start] |= ostd::Uint32(gs.code.size() - (start + 1)) << 8;
    } else {
        gs.code.resize(start);
        gs.code.push(CODE_EMPTY | rettype);
    }
    return p;
}

static inline void compileunescapestr(GenState &gs, bool macro = false) {
    gs.next_char();
    const char *end = parsestring(gs.source);
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + (end - gs.source) / sizeof(ostd::Uint32) + 1);
    char *buf = (char *)&gs.code[gs.code.size()];
    auto writer = ostd::CharRange(buf, (gs.code.capacity() - gs.code.size()) * sizeof(ostd::Uint32));
    ostd::Size len = util::unescape_string(writer, ostd::ConstCharRange(gs.source, end));
    writer.put('\0');
    memset(&buf[len], 0, sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    gs.code.back() |= len << 8;
    gs.code.advance(len / sizeof(ostd::Uint32) + 1);
    gs.source = end;
    if (*gs.source == '\"') gs.next_char();
}

static ostd::Uint32 emptyblock[VAL_ANY][2] = {
    { CODE_START + 0x100, CODE_EXIT | RET_NULL },
    { CODE_START + 0x100, CODE_EXIT | RET_INT },
    { CODE_START + 0x100, CODE_EXIT | RET_FLOAT },
    { CODE_START + 0x100, CODE_EXIT | RET_STR }
};

static inline bool cs_get_bool(ostd::ConstCharRange s) {
    if (s.empty())
        return false;
    switch (s.front()) {
    case '+':
    case '-':
        switch (s[1]) {
        case '0':
            break;
        case '.':
            return !isdigit(s[2]) || (cs_parse_float(s) != 0);
        default:
            return true;
        }
    /* fallthrough */
    case '0': {
        char *end;
        int val = int(strtoul(s.data(), &end, 0));
        if (val) return true;
        switch (*end) {
        case 'e':
        case '.':
            return (cs_parse_float(s) != 0);
        default:
            return false;
        }
    }
    case '.':
        return !isdigit(s[1]) || (cs_parse_float(s) != 0);
    case '\0':
        return false;
    }
    return true;
}

static inline bool cs_get_bool(const TaggedValue &v) {
    switch (v.get_type()) {
    case VAL_FLOAT:
        return v.f != 0;
    case VAL_INT:
        return v.i != 0;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return cs_get_bool(v.s);
    default:
        return false;
    }
}

static ostd::ConstCharRange unusedword(nullptr, nullptr);
static bool compilearg(GenState &gs, int wordtype, int prevargs = MAX_RESULTS, ostd::ConstCharRange &word = unusedword);

static void compilelookup(GenState &gs, int ltype, int prevargs = MAX_RESULTS) {
    ostd::ConstCharRange lookup;
    gs.next_char();
    switch (gs.current()) {
    case '(':
    case '[':
        if (!compilearg(gs, VAL_CSTR, prevargs)) goto invalid;
        break;
    case '$':
        compilelookup(gs, VAL_CSTR, prevargs);
        break;
    case '\"':
        cutstring(gs.source, lookup);
        goto lookupid;
    default: {
        cutword(gs.source, lookup);
        if (!lookup.size()) goto invalid;
lookupid:
        Ident *id = gs.cs.new_ident(lookup);
        if (id) switch (id->type) {
            case ID_VAR:
                gs.code.push(CODE_IVAR | cs_ret_code(ltype, RET_INT) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    gs.code.pop();
                    break;
                case VAL_CODE:
                    gs.code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    gs.code.push(CODE_IDENTU);
                    break;
                }
                return;
            case ID_FVAR:
                gs.code.push(CODE_FVAR | cs_ret_code(ltype, RET_FLOAT) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    gs.code.pop();
                    break;
                case VAL_CODE:
                    gs.code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    gs.code.push(CODE_IDENTU);
                    break;
                }
                return;
            case ID_SVAR:
                switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_CANY:
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                case VAL_COND:
                    gs.code.push(CODE_SVARM | (id->index << 8));
                    break;
                default:
                    gs.code.push(CODE_SVAR | cs_ret_code(ltype, RET_STR) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_ALIAS:
                switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_CANY:
                case VAL_COND:
                    gs.code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                    break;
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                    gs.code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | RET_STR | (id->index << 8));
                    break;
                default:
                    gs.code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPARG : CODE_LOOKUP) | cs_ret_code(ltype, RET_STR) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_COMMAND: {
                int comtype = CODE_COM, numargs = 0;
                if (prevargs >= MAX_RESULTS) gs.code.push(CODE_ENTER);
                for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
                    case 'S':
                        gs.gen_str();
                        numargs++;
                        break;
                    case 's':
                        gs.gen_str(ostd::ConstCharRange(), true);
                        numargs++;
                        break;
                    case 'i':
                        gs.gen_int();
                        numargs++;
                        break;
                    case 'b':
                        gs.gen_int(INT_MIN);
                        numargs++;
                        break;
                    case 'f':
                        gs.gen_float();
                        numargs++;
                        break;
                    case 'F':
                        gs.code.push(CODE_DUP | RET_FLOAT);
                        numargs++;
                        break;
                    case 'E':
                    case 'T':
                    case 't':
                        gs.gen_null();
                        numargs++;
                        break;
                    case 'e':
                        compileblock(gs);
                        numargs++;
                        break;
                    case 'r':
                        gs.gen_ident();
                        numargs++;
                        break;
                    case '$':
                        gs.gen_ident(id);
                        numargs++;
                        break;
                    case 'N':
                        gs.gen_int(-1);
                        numargs++;
                        break;
                    case 'C':
                        comtype = CODE_COMC;
                        goto compilecomv;
                    case 'V':
                        comtype = CODE_COMV;
                        goto compilecomv;
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        break;
                    }
                gs.code.push(comtype | cs_ret_code(ltype) | (id->index << 8));
                gs.code.push((prevargs >= MAX_RESULTS ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
compilecomv:
                gs.code.push(comtype | cs_ret_code(ltype) | (numargs << 8) | (id->index << 13));
                gs.code.push((prevargs >= MAX_RESULTS ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
            }
            default:
                goto invalid;
            }
        gs.gen_str(lookup, true);
        break;
    }
    }
    switch (ltype) {
    case VAL_CANY:
    case VAL_COND:
        gs.code.push(CODE_LOOKUPMU);
        break;
    case VAL_CSTR:
    case VAL_CODE:
    case VAL_IDENT:
        gs.code.push(CODE_LOOKUPMU | RET_STR);
        break;
    default:
        gs.code.push(CODE_LOOKUPU | cs_ret_code(ltype));
        break;
    }
done:
    switch (ltype) {
    case VAL_POP:
        gs.code.push(CODE_POP);
        break;
    case VAL_CODE:
        gs.code.push(CODE_COMPILE);
        break;
    case VAL_COND:
        gs.code.push(CODE_COND);
        break;
    case VAL_IDENT:
        gs.code.push(CODE_IDENTU);
        break;
    }
    return;
invalid:
    switch (ltype) {
    case VAL_POP:
        break;
    case VAL_NULL:
    case VAL_ANY:
    case VAL_CANY:
    case VAL_WORD:
    case VAL_COND:
        gs.gen_null();
        break;
    default:
        gs.gen_value(ltype);
        break;
    }
}

static bool compileblockstr(GenState &gs, ostd::ConstCharRange str, bool macro) {
    int start = gs.code.size();
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1);
    char *buf = (char *)&gs.code[gs.code.size()];
    int len = 0;
    while (!str.empty()) {
        const char *p = str.data();
        str = ostd::find_one_of(str, ostd::ConstCharRange("\r/\"@]"));
        memcpy(&buf[len], p, str.data() - p);
        len += str.data() - p;
        if (str.empty())
            goto done;
        switch (str.front()) {
        case '\r':
            str.pop_front();
            break;
        case '\"': {
            ostd::ConstCharRange start = str;
            start.pop_front();
            ostd::ConstCharRange end = cs_parse_str(start);
            if (!end.empty() && (end.front() == '\"'))
                end.pop_front();
            ostd::Size slen = str.distance_front(end);
            memcpy(&buf[len], str.data(), slen);
            len += slen;
            str = end;
            break;
        }
        case '/':
            if (str[1] == '/')
                str = ostd::find(str, '\n');
            else {
                buf[len++] = str.front();
                str.pop_front();
            }
            break;
        case '@':
        case ']':
            buf[len++] = str.front();
            str.pop_front();
            break;
        }
    }
done:
    memset(&buf[len], '\0', sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    gs.code.advance(len / sizeof(ostd::Uint32) + 1);
    gs.code[start] |= len << 8;
    return true;
}

static bool compileblocksub(GenState &gs, int prevargs) {
    ostd::ConstCharRange lookup;
    const char *op;
    switch (gs.current()) {
    case '(':
        if (!compilearg(gs, VAL_CANY, prevargs)) return false;
        break;
    case '[':
        if (!compilearg(gs, VAL_CSTR, prevargs)) return false;
        gs.code.push(CODE_LOOKUPMU);
        break;
    case '\"':
        cutstring(gs.source, lookup);
        goto lookupid;
    default: {
        op = gs.source;
        while (isalnum(gs.current()) || gs.current() == '_') gs.next_char();
        lookup = ostd::ConstCharRange(op, gs.source - op);
        if (lookup.empty()) return false;
lookupid:
        Ident *id = gs.cs.new_ident(lookup);
        if (id) switch (id->type) {
            case ID_VAR:
                gs.code.push(CODE_IVAR | (id->index << 8));
                goto done;
            case ID_FVAR:
                gs.code.push(CODE_FVAR | (id->index << 8));
                goto done;
            case ID_SVAR:
                gs.code.push(CODE_SVARM | (id->index << 8));
                goto done;
            case ID_ALIAS:
                gs.code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                goto done;
            }
        gs.gen_str(lookup, true);
        gs.code.push(CODE_LOOKUPMU);
done:
        break;
    }
    }
    return true;
}

static void compileblockmain(GenState &gs, int wordtype, int prevargs) {
    const char *line = gs.source, *start = gs.source;
    int concs = 0;
    for (int brak = 1; brak;) {
        gs.source += strcspn(gs.source, "@\"/[]\0");
        char c = gs.next_char();
        switch (c) {
        case '\0':
            cs_debug_code_line(gs.cs, line, "missing \"]\"");
            gs.source--;
            goto done;
        case '\"':
            gs.source = parsestring(gs.source);
            if (gs.current() == '\"') gs.next_char();
            break;
        case '/':
            if (gs.current() == '/') gs.source += strcspn(gs.source, "\n\0");
            break;
        case '[':
            brak++;
            break;
        case ']':
            brak--;
            break;
        case '@': {
            const char *esc = gs.source;
            while (gs.current() == '@') gs.next_char();
            int level = gs.source - (esc - 1);
            if (brak > level) continue;
            else if (brak < level) cs_debug_code_line(gs.cs, line, "too many @s");
            if (!concs && prevargs >= MAX_RESULTS) gs.code.push(CODE_ENTER);
            if (concs + 2 > MAX_ARGUMENTS) {
                gs.code.push(CODE_CONCW | RET_STR | (concs << 8));
                concs = 1;
            }
            if (compileblockstr(gs, ostd::ConstCharRange(start, esc - 1), true)) concs++;
            if (compileblocksub(gs, prevargs + concs)) concs++;
            if (concs) start = gs.source;
            else if (prevargs >= MAX_RESULTS) gs.code.pop();
            break;
        }
        }
    }
done:
    if (gs.source - 1 > start) {
        if (!concs) switch (wordtype) {
            case VAL_POP:
                return;
            case VAL_CODE:
            case VAL_COND:
                gs.source = compileblock(gs, start, RET_NULL, ']');
                return;
            case VAL_IDENT:
                gs.gen_ident(ostd::ConstCharRange(start, gs.source - 1));
                return;
            }
        switch (wordtype) {
        case VAL_CSTR:
        case VAL_CODE:
        case VAL_IDENT:
        case VAL_CANY:
        case VAL_COND:
            compileblockstr(gs, ostd::ConstCharRange(start, gs.source - 1), true);
            break;
        default:
            compileblockstr(gs, ostd::ConstCharRange(start, gs.source - 1), concs > 0);
            break;
        }
        if (concs > 1) concs++;
    }
    if (concs) {
        if (prevargs >= MAX_RESULTS) {
            gs.code.push(CODE_CONCM | cs_ret_code(wordtype) | (concs << 8));
            gs.code.push(CODE_EXIT | cs_ret_code(wordtype));
        } else gs.code.push(CODE_CONCW | cs_ret_code(wordtype) | (concs << 8));
    }
    switch (wordtype) {
    case VAL_POP:
        if (concs || gs.source - 1 > start) gs.code.push(CODE_POP);
        break;
    case VAL_COND:
        if (!concs && gs.source - 1 <= start) gs.gen_null();
        else gs.code.push(CODE_COND);
        break;
    case VAL_CODE:
        if (!concs && gs.source - 1 <= start) compileblock(gs);
        else gs.code.push(CODE_COMPILE);
        break;
    case VAL_IDENT:
        if (!concs && gs.source - 1 <= start) gs.gen_ident();
        else gs.code.push(CODE_IDENTU);
        break;
    case VAL_CSTR:
    case VAL_CANY:
        if (!concs && gs.source - 1 <= start)
            gs.gen_str(ostd::ConstCharRange(), true);
        break;
    case VAL_STR:
    case VAL_NULL:
    case VAL_ANY:
    case VAL_WORD:
        if (!concs && gs.source - 1 <= start) gs.gen_str();
        break;
    default:
        if (!concs) {
            if (gs.source - 1 <= start) gs.gen_value(wordtype);
            else gs.code.push(CODE_FORCE | (wordtype << CODE_RET));
        }
        break;
    }
}

static bool compilearg(GenState &gs, int wordtype, int prevargs, ostd::ConstCharRange &word) {
    skipcomments(gs.source);
    switch (gs.current()) {
    case '\"':
        switch (wordtype) {
        case VAL_POP:
            gs.source = parsestring(gs.source + 1);
            if (gs.current() == '\"') gs.next_char();
            break;
        case VAL_COND: {
            char *s = cutstring(gs.source);
            if (s[0]) compileblock(gs, s);
            else gs.gen_null();
            delete[] s;
            break;
        }
        case VAL_CODE: {
            char *s = cutstring(gs.source);
            compileblock(gs, s);
            delete[] s;
            break;
        }
        case VAL_WORD:
            cutstring(gs.source, word);
            break;
        case VAL_ANY:
        case VAL_STR:
            compileunescapestr(gs);
            break;
        case VAL_CANY:
        case VAL_CSTR:
            compileunescapestr(gs, true);
            break;
        default: {
            ostd::ConstCharRange s;
            cutstring(gs.source, s);
            gs.gen_value(wordtype, s);
            break;
        }
        }
        return true;
    case '$':
        compilelookup(gs, wordtype, prevargs);
        return true;
    case '(':
        gs.next_char();
        if (prevargs >= MAX_RESULTS) {
            gs.code.push(CODE_ENTER);
            compilestatements(gs, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')');
            gs.code.push(CODE_EXIT | cs_ret_code(wordtype));
        } else {
            ostd::Size start = gs.code.size();
            compilestatements(gs, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')', prevargs);
            if (gs.code.size() > start) gs.code.push(CODE_RESULT_ARG | cs_ret_code(wordtype));
            else {
                gs.gen_value(wordtype);
                return true;
            }
        }
        switch (wordtype) {
        case VAL_POP:
            gs.code.push(CODE_POP);
            break;
        case VAL_COND:
            gs.code.push(CODE_COND);
            break;
        case VAL_CODE:
            gs.code.push(CODE_COMPILE);
            break;
        case VAL_IDENT:
            gs.code.push(CODE_IDENTU);
            break;
        }
        return true;
    case '[':
        gs.next_char();
        compileblockmain(gs, wordtype, prevargs);
        return true;
    default:
        switch (wordtype) {
        case VAL_POP: {
            const char *s = gs.source;
            gs.source = parseword(gs.source);
            return gs.source != s;
        }
        case VAL_COND: {
            char *s = cutword(gs.source);
            if (!s) return false;
            compileblock(gs, s);
            delete[] s;
            return true;
        }
        case VAL_CODE: {
            char *s = cutword(gs.source);
            if (!s) return false;
            compileblock(gs, s);
            delete[] s;
            return true;
        }
        case VAL_WORD:
            cutword(gs.source, word);
            return !word.empty();
        default: {
            ostd::ConstCharRange s;
            cutword(gs.source, s);
            if (s.empty()) return false;
            gs.gen_value(wordtype, s);
            return true;
        }
        }
    }
}

static void compilestatements(GenState &gs, int rettype, int brak, int prevargs) {
    const char *line = gs.source;
    ostd::ConstCharRange idname;
    int numargs;
    for (;;) {
        skipcomments(gs.source);
        idname = ostd::ConstCharRange(nullptr, nullptr);
        bool more = compilearg(gs, VAL_WORD, prevargs, idname);
        if (!more) goto endstatement;
        skipcomments(gs.source);
        if (gs.current() == '=') switch (gs.source[1]) {
            case '/':
                if (gs.source[2] != '/') break;
            case ';':
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\0':
                gs.next_char();
                if (idname.data()) {
                    Ident *id = gs.cs.new_ident(idname);
                    if (id) switch (id->type) {
                        case ID_ALIAS:
                            if (!(more = compilearg(gs, VAL_ANY, prevargs))) gs.gen_str();
                            gs.code.push((id->index < MAX_ARGUMENTS ? CODE_ALIASARG : CODE_ALIAS) | (id->index << 8));
                            goto endstatement;
                        case ID_VAR:
                            if (!(more = compilearg(gs, VAL_INT, prevargs))) gs.gen_int();
                            gs.code.push(CODE_IVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_FVAR:
                            if (!(more = compilearg(gs, VAL_FLOAT, prevargs))) gs.gen_float();
                            gs.code.push(CODE_FVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_SVAR:
                            if (!(more = compilearg(gs, VAL_CSTR, prevargs))) gs.gen_str();
                            gs.code.push(CODE_SVAR1 | (id->index << 8));
                            goto endstatement;
                        }
                    gs.gen_str(idname, true);
                }
                if (!(more = compilearg(gs, VAL_ANY))) gs.gen_str();
                gs.code.push(CODE_ALIASU);
                goto endstatement;
            }
        numargs = 0;
        if (!idname.data()) {
noid:
            while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
            gs.code.push(CODE_CALLU | (numargs << 8));
        } else {
            Ident *id = gs.cs.idents.at(idname);
            if (!id) {
                if (!cs_check_num(idname)) {
                    gs.gen_str(idname, true);
                    goto noid;
                }
                switch (rettype) {
                case VAL_ANY:
                case VAL_CANY: {
                    char *end = (char *)idname.data();
                    int val = int(strtoul(idname.data(), &end, 0));
                    if (end < &idname[idname.size()]) gs.gen_str(idname, rettype == VAL_CANY);
                    else gs.gen_int(val);
                    break;
                }
                default:
                    gs.gen_value(rettype, idname);
                    break;
                }
                gs.code.push(CODE_RESULT);
            } else switch (id->type) {
                case ID_ALIAS:
                    while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_ANY, prevargs + numargs))) numargs++;
                    gs.code.push((id->index < MAX_ARGUMENTS ? CODE_CALLARG : CODE_CALL) | (numargs << 8) | (id->index << 13));
                    break;
                case ID_COMMAND: {
                    int comtype = CODE_COM, fakeargs = 0;
                    bool rep = false;
                    for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
                        case 'S':
                        case 's':
                            if (more) more = compilearg(gs, *fmt == 's' ? VAL_CSTR : VAL_STR, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_str(ostd::ConstCharRange(), *fmt == 's');
                                fakeargs++;
                            } else if (!fmt[1]) {
                                int numconc = 1;
                                while (numargs + numconc < MAX_ARGUMENTS && (more = compilearg(gs, VAL_CSTR, prevargs + numargs + numconc))) numconc++;
                                if (numconc > 1) gs.code.push(CODE_CONC | RET_STR | (numconc << 8));
                            }
                            numargs++;
                            break;
                        case 'i':
                            if (more) more = compilearg(gs, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_int();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'b':
                            if (more) more = compilearg(gs, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_int(INT_MIN);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'f':
                            if (more) more = compilearg(gs, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_float();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'F':
                            if (more) more = compilearg(gs, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.code.push(CODE_DUP | RET_FLOAT);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'T':
                        case 't':
                            if (more) more = compilearg(gs, *fmt == 't' ? VAL_CANY : VAL_ANY, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_null();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'E':
                            if (more) more = compilearg(gs, VAL_COND, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_null();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'e':
                            if (more) more = compilearg(gs, VAL_CODE, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileblock(gs);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'r':
                            if (more) more = compilearg(gs, VAL_IDENT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_ident();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case '$':
                            gs.gen_ident(id);
                            numargs++;
                            break;
                        case 'N':
                            gs.gen_int(numargs - fakeargs);
                            numargs++;
                            break;
                        case 'C':
                            comtype = CODE_COMC;
                            if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case 'V':
                            comtype = CODE_COMV;
                            if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                            if (more && numargs < MAX_ARGUMENTS) {
                                int numrep = *fmt - '0' + 1;
                                fmt -= numrep;
                                rep = true;
                            } else for (; numargs > MAX_ARGUMENTS; numargs--) gs.code.push(CODE_POP);
                            break;
                        }
                     gs.code.push(comtype | cs_ret_code(rettype) | (id->index << 8));
                    break;
compilecomv:
                    gs.code.push(comtype | cs_ret_code(rettype) | (numargs << 8) | (id->index << 13));
                    break;
                }
                case ID_LOCAL:
                    if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_IDENT, prevargs + numargs))) numargs++;
                    if (more) while ((more = compilearg(gs, VAL_POP)));
                    gs.code.push(CODE_LOCAL | (numargs << 8));
                    break;
                case ID_DO:
                    if (more) more = compilearg(gs, VAL_CODE, prevargs);
                    gs.code.push((more ? CODE_DO : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_DOARGS:
                    if (more) more = compilearg(gs, VAL_CODE, prevargs);
                    gs.code.push((more ? CODE_DOARGS : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_IF:
                    if (more) more = compilearg(gs, VAL_CANY, prevargs);
                    if (!more) gs.code.push(CODE_NULL | cs_ret_code(rettype));
                    else {
                        int start1 = gs.code.size();
                        more = compilearg(gs, VAL_CODE, prevargs + 1);
                        if (!more) {
                            gs.code.push(CODE_POP);
                            gs.code.push(CODE_NULL | cs_ret_code(rettype));
                        } else {
                            int start2 = gs.code.size();
                            more = compilearg(gs, VAL_CODE, prevargs + 2);
                            ostd::Uint32 inst1 = gs.code[start1], op1 = inst1 & ~CODE_RET_MASK, len1 = start2 - (start1 + 1);
                            if (!more) {
                                if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                    gs.code[start1] = (len1 << 8) | CODE_JUMP_FALSE;
                                    gs.code[start1 + 1] = CODE_ENTER_RESULT;
                                    gs.code[start1 + len1] = (gs.code[start1 + len1] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                    break;
                                }
                                compileblock(gs);
                            } else {
                                ostd::Uint32 inst2 = gs.code[start2], op2 = inst2 & ~CODE_RET_MASK, len2 = gs.code.size() - (start2 + 1);
                                if (op2 == (CODE_BLOCK | (len2 << 8))) {
                                    if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                        gs.code[start1] = ((start2 - start1) << 8) | CODE_JUMP_FALSE;
                                        gs.code[start1 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start1 + len1] = (gs.code[start1 + len1] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        gs.code[start2] = (len2 << 8) | CODE_JUMP;
                                        gs.code[start2 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start2 + len2] = (gs.code[start2 + len2] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        break;
                                    } else if (op1 == (CODE_EMPTY | (len1 << 8))) {
                                        gs.code[start1] = CODE_NULL | (inst2 & CODE_RET_MASK);
                                        gs.code[start2] = (len2 << 8) | CODE_JUMP_TRUE;
                                        gs.code[start2 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start2 + len2] = (gs.code[start2 + len2] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        break;
                                    }
                                }
                            }
                            gs.code.push(CODE_COM | cs_ret_code(rettype) | (id->index << 8));
                        }
                    }
                    break;
                case ID_RESULT:
                    if (more) more = compilearg(gs, VAL_ANY, prevargs);
                    gs.code.push((more ? CODE_RESULT : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_NOT:
                    if (more) more = compilearg(gs, VAL_CANY, prevargs);
                    gs.code.push((more ? CODE_NOT : CODE_TRUE) | cs_ret_code(rettype));
                    break;
                case ID_AND:
                case ID_OR:
                    if (more) more = compilearg(gs, VAL_COND, prevargs);
                    if (!more) {
                        gs.code.push((id->type == ID_AND ? CODE_TRUE : CODE_FALSE) | cs_ret_code(rettype));
                    } else {
                        numargs++;
                        int start = gs.code.size(), end = start;
                        while (numargs < MAX_ARGUMENTS) {
                            more = compilearg(gs, VAL_COND, prevargs + numargs);
                            if (!more) break;
                            numargs++;
                            if ((gs.code[end] & ~CODE_RET_MASK) != (CODE_BLOCK | (ostd::Uint32(gs.code.size() - (end + 1)) << 8))) break;
                            end = gs.code.size();
                        }
                        if (more) {
                            while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_COND, prevargs + numargs))) numargs++;
                            gs.code.push(CODE_COMV | cs_ret_code(rettype) | (numargs << 8) | (id->index << 13));
                        } else {
                            ostd::Uint32 op = id->type == ID_AND ? CODE_JUMP_RESULT_FALSE : CODE_JUMP_RESULT_TRUE;
                            gs.code.push(op);
                            end = gs.code.size();
                            while (start + 1 < end) {
                                ostd::Uint32 len = gs.code[start] >> 8;
                                gs.code[start] = ((end - (start + 1)) << 8) | op;
                                gs.code[start + 1] = CODE_ENTER;
                                gs.code[start + len] = (gs.code[start + len] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                start += len + 1;
                            }
                        }
                    }
                    break;
                case ID_VAR:
                    if (!(more = compilearg(gs, VAL_INT, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else if (!(id->flags & IDF_HEX) || !(more = compilearg(gs, VAL_INT, prevargs + 1))) gs.code.push(CODE_IVAR1 | (id->index << 8));
                    else if (!(more = compilearg(gs, VAL_INT, prevargs + 2))) gs.code.push(CODE_IVAR2 | (id->index << 8));
                    else gs.code.push(CODE_IVAR3 | (id->index << 8));
                    break;
                case ID_FVAR:
                    if (!(more = compilearg(gs, VAL_FLOAT, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else gs.code.push(CODE_FVAR1 | (id->index << 8));
                    break;
                case ID_SVAR:
                    if (!(more = compilearg(gs, VAL_CSTR, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else {
                        do ++numargs;
                        while (numargs < MAX_ARGUMENTS && (more = compilearg(gs, VAL_CANY, prevargs + numargs)));
                        if (numargs > 1) gs.code.push(CODE_CONC | RET_STR | (numargs << 8));
                        gs.code.push(CODE_SVAR1 | (id->index << 8));
                    }
                    break;
                }
        }
endstatement:
        if (more) while (compilearg(gs, VAL_POP));
        gs.source += strcspn(gs.source, ")];/\n\0");
        char c = gs.next_char();
        switch (c) {
        case '\0':
            if (c != brak) cs_debug_code_line(gs.cs, line, "missing \"%c\"", brak);
            gs.source--;
            return;

        case ')':
        case ']':
            if (c == brak) return;
            cs_debug_code_line(gs.cs, line, "unexpected \"%c\"", c);
            break;

        case '/':
            if (gs.current() == '/') gs.source += strcspn(gs.source, "\n\0");
            goto endstatement;
        }
    }
}

void GenState::gen_main(ostd::ConstCharRange s, int ret_type) {
    source = s.data();
    code.push(CODE_START);
    compilestatements(*this, VAL_ANY);
    code.push(CODE_EXIT | ((ret_type < VAL_ANY) ? (ret_type << CODE_RET) : 0));
}

ostd::Uint32 *CsState::compile(ostd::ConstCharRange str) {
    GenState gs(*this);
    gs.code.reserve(64);
    gs.gen_main(str);
    ostd::Uint32 *code = new ostd::Uint32[gs.code.size()];
    memcpy(code, gs.code.data(), gs.code.size() * sizeof(ostd::Uint32));
    code[0] += 0x100;
    return code;
}

static inline const ostd::Uint32 *forcecode(CsState &cs, TaggedValue &v) {
    if (v.get_type() != VAL_CODE) {
        GenState gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        v.cleanup();
        v.set_code(gs.code.disown() + 1);
    }
    return v.code;
}

static inline void forcecond(CsState &cs, TaggedValue &v) {
    switch (v.get_type()) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        if (v.s[0]) forcecode(cs, v);
        else v.set_int(0);
        break;
    }
}

void bcode_ref(ostd::Uint32 *code) {
    if (!code) return;
    switch (*code & CODE_OP_MASK) {
    case CODE_START:
        *code += 0x100;
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
    case CODE_START:
        code[-1] += 0x100;
        break;
    case CODE_OFFSET:
        code -= int(code[-1] >> 8);
        *code += 0x100;
        break;
    }
}

void bcode_unref(ostd::Uint32 *code) {
    if (!code) return;
    switch (*code & CODE_OP_MASK) {
    case CODE_START:
        *code -= 0x100;
        if (int(*code) < 0x100) delete[] code;
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
    case CODE_START:
        code[-1] -= 0x100;
        if (int(code[-1]) < 0x100) delete[] &code[-1];
        break;
    case CODE_OFFSET:
        code -= int(code[-1] >> 8);
        *code -= 0x100;
        if (int(*code) < 0x100) delete[] code;
        break;
    }
}

using CommandFunc = void (__cdecl *)(CsState &);
using CommandFunc1 = void (__cdecl *)(CsState &, void *);
using CommandFunc2 = void (__cdecl *)(CsState &, void *, void *);
using CommandFunc3 = void (__cdecl *)(CsState &, void *, void *, void *);
using CommandFunc4 = void (__cdecl *)(CsState &, void *, void *, void *, void *);
using CommandFunc5 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *);
using CommandFunc6 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *);
using CommandFunc7 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc8 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc9 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc10 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc11 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc12 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFuncTv = void (__cdecl *)(CsState &, ostd::PointerRange<TaggedValue>);

static const ostd::Uint32 *skipcode(const ostd::Uint32 *code, TaggedValue &result = no_ret) {
    int depth = 0;
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
        case CODE_MACRO:
        case CODE_VAL|RET_STR: {
            ostd::Uint32 len = op >> 8;
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }
        case CODE_BLOCK:
        case CODE_JUMP:
        case CODE_JUMP_TRUE:
        case CODE_JUMP_FALSE:
        case CODE_JUMP_RESULT_TRUE:
        case CODE_JUMP_RESULT_FALSE: {
            ostd::Uint32 len = op >> 8;
            code += len;
            continue;
        }
        case CODE_ENTER:
        case CODE_ENTER_RESULT:
            ++depth;
            continue;
        case CODE_EXIT|RET_NULL:
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            if (depth <= 0) {
                if (&result != &no_ret) result.force(op & CODE_RET_MASK);
                return code;
            }
            --depth;
            continue;
        }
    }
}

static inline void callcommand(CsState &cs, Ident *id, TaggedValue *args, int numargs, bool lookup = false) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
        case 'i':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(0);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'b':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(INT_MIN);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'f':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(0.0f);
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'F':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(args[i - 1].get_float());
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'S':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_str_dup("");
                fakeargs++;
            } else args[i].force_str();
            break;
        case 's':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_cstr("");
                fakeargs++;
            } else args[i].force_str();
            break;
        case 'T':
        case 't':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            }
            break;
        case 'E':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            } else forcecond(cs, args[i]);
            break;
        case 'e':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_code(emptyblock[VAL_NULL] + 1);
                fakeargs++;
            } else forcecode(cs, args[i]);
            break;
        case 'r':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_ident(cs.dummy);
                fakeargs++;
            } else cs.force_ident(args[i]);
            break;
        case '$':
            if (++i < numargs) args[i].cleanup();
            args[i].set_ident(id);
            break;
        case 'N':
            if (++i < numargs) args[i].cleanup();
            args[i].set_int(lookup ? -1 : i - fakeargs);
            break;
        case 'C': {
            i = ostd::max(i + 1, numargs);
            ostd::Vector<char> buf;
            ((CommandFunc1)id->fun)(cs, conc(buf, ostd::iter(args, i), true));
            goto cleanup;
        }
        case 'V':
            i = ostd::max(i + 1, numargs);
            ((CommandFuncTv)id->fun)(cs, ostd::iter(args, i));
            goto cleanup;
        case '1':
        case '2':
        case '3':
        case '4':
            if (i + 1 < numargs) {
                fmt -= *fmt - '0' + 1;
                rep = true;
            }
            break;
        }
    ++i;
#define OFFSETARG(n) n
#define ARG(n) (id->argmask&(1<<(n)) ? (void *)args[OFFSETARG(n)].s : (void *)&args[OFFSETARG(n)].i)
#define CALLCOM(n) \
        switch(n) \
        { \
            case 0: ((CommandFunc)id->fun)(cs); break; \
            case 1: ((CommandFunc1)id->fun)(cs, ARG(0)); break; \
            case 2: ((CommandFunc2)id->fun)(cs, ARG(0), ARG(1)); break; \
            case 3: ((CommandFunc3)id->fun)(cs, ARG(0), ARG(1), ARG(2)); break; \
            case 4: ((CommandFunc4)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3)); break; \
            case 5: ((CommandFunc5)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)); break; \
            case 6: ((CommandFunc6)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)); break; \
            case 7: ((CommandFunc7)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)); break; \
            case 8: ((CommandFunc8)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7)); break; \
            case 9: ((CommandFunc9)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)); break; \
            case 10: ((CommandFunc10)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9)); break; \
            case 11: ((CommandFunc11)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), ARG(10)); break; \
            case 12: ((CommandFunc12)id->fun)(cs, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), ARG(10), ARG(11)); break; \
        }
    CALLCOM(i)
#undef OFFSETARG
cleanup:
    for (ostd::Size k = 0; k < ostd::Size(i); ++k) args[k].cleanup();
    for (; i < numargs; i++) args[i].cleanup();
}

#define MAXRUNDEPTH 255
static int rundepth = 0;

static const ostd::Uint32 *runcode(CsState &cs, const ostd::Uint32 *code, TaggedValue &result) {
    result.set_null();
    if (rundepth >= MAXRUNDEPTH) {
        cs_debug_code(cs, "exceeded recursion limit");
        return skipcode(code, result);
    }
    ++rundepth;
    int numargs = 0;
    TaggedValue args[MAX_ARGUMENTS + MAX_RESULTS], *prevret = cs.result;
    cs.result = &result;
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
        case CODE_START:
        case CODE_OFFSET:
            continue;

#define RETOP(op, val) \
                case op: \
                    result.cleanup(); \
                    val; \
                    continue;

            RETOP(CODE_NULL | RET_NULL, result.set_null())
            RETOP(CODE_NULL | RET_STR, result.set_str_dup(""))
            RETOP(CODE_NULL | RET_INT, result.set_int(0))
            RETOP(CODE_NULL | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_FALSE | RET_STR, result.set_str_dup("0"))
        case CODE_FALSE|RET_NULL:
            RETOP(CODE_FALSE | RET_INT, result.set_int(0))
            RETOP(CODE_FALSE | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_TRUE | RET_STR, result.set_str_dup("1"))
        case CODE_TRUE|RET_NULL:
            RETOP(CODE_TRUE | RET_INT, result.set_int(1))
            RETOP(CODE_TRUE | RET_FLOAT, result.set_float(1.0f))

#define RETPOP(op, val) \
                RETOP(op, { --numargs; val; args[numargs].cleanup(); })

            RETPOP(CODE_NOT | RET_STR, result.set_str_dup(cs_get_bool(args[numargs]) ? "0" : "1"))
        case CODE_NOT|RET_NULL:
                RETPOP(CODE_NOT | RET_INT, result.set_int(cs_get_bool(args[numargs]) ? 0 : 1))
                RETPOP(CODE_NOT | RET_FLOAT, result.set_float(cs_get_bool(args[numargs]) ? 0.0f : 1.0f))

            case CODE_POP:
                    args[--numargs].cleanup();
            continue;
        case CODE_ENTER:
            code = runcode(cs, code, args[numargs++]);
            continue;
        case CODE_ENTER_RESULT:
            result.cleanup();
            code = runcode(cs, code, result);
            continue;
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            result.force(op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_EXIT|RET_NULL:
            goto exit;
        case CODE_RESULT_ARG|RET_STR:
        case CODE_RESULT_ARG|RET_INT:
        case CODE_RESULT_ARG|RET_FLOAT:
            result.force(op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_RESULT_ARG|RET_NULL:
            args[numargs++] = result;
            result.set_null();
            continue;
        case CODE_PRINT:
            cs.print_var(cs.identmap[op >> 8]);
            continue;

        case CODE_LOCAL: {
            result.cleanup();
            int numlocals = op >> 8, offset = numargs - numlocals;
            IdentStack locals[MAX_ARGUMENTS];
            for (int i = 0; i < numlocals; ++i) args[offset + i].id->push_alias(locals[i]);
            code = runcode(cs, code, result);
            for (int i = offset; i < numargs; i++) args[i].id->pop_alias();
            goto exit;
        }

        case CODE_DOARGS|RET_NULL:
        case CODE_DOARGS|RET_STR:
        case CODE_DOARGS|RET_INT:
        case CODE_DOARGS|RET_FLOAT:
            if (cs.stack != &cs.noalias) {
                cs_do_args(cs, [&]() {
                    result.cleanup();
                    runcode(cs, args[--numargs].code, result);
                    args[numargs].cleanup();
                    result.force(op & CODE_RET_MASK);
                });
                continue;
            }
        /* fallthrough */
        case CODE_DO|RET_NULL:
        case CODE_DO|RET_STR:
        case CODE_DO|RET_INT:
        case CODE_DO|RET_FLOAT:
            result.cleanup();
            runcode(cs, args[--numargs].code, result);
            args[numargs].cleanup();
            result.force(op & CODE_RET_MASK);
            continue;

        case CODE_JUMP: {
            ostd::Uint32 len = op >> 8;
            code += len;
            continue;
        }
        case CODE_JUMP_TRUE: {
            ostd::Uint32 len = op >> 8;
            if (cs_get_bool(args[--numargs])) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_FALSE: {
            ostd::Uint32 len = op >> 8;
            if (!cs_get_bool(args[--numargs])) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_RESULT_TRUE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                runcode(cs, args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (cs_get_bool(result)) code += len;
            continue;
        }
        case CODE_JUMP_RESULT_FALSE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                runcode(cs, args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (!cs_get_bool(result)) code += len;
            continue;
        }

        case CODE_MACRO: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_macro(code);
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }

        case CODE_VAL|RET_STR: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_str_dup(ostd::ConstCharRange((const char *)code, len));
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }
        case CODE_VALI|RET_STR: {
            char s[4] = { char((op >> 8) & 0xFF), char((op >> 16) & 0xFF), char((op >> 24) & 0xFF), '\0' };
            args[numargs++].set_str_dup(s);
            continue;
        }
        case CODE_VAL|RET_NULL:
        case CODE_VALI|RET_NULL:
            args[numargs++].set_null();
            continue;
        case CODE_VAL|RET_INT:
            args[numargs++].set_int(int(*code++));
            continue;
        case CODE_VALI|RET_INT:
            args[numargs++].set_int(int(op) >> 8);
            continue;
        case CODE_VAL|RET_FLOAT:
            args[numargs++].set_float(*(const float *)code++);
            continue;
        case CODE_VALI|RET_FLOAT:
            args[numargs++].set_float(float(int(op) >> 8));
            continue;

        case CODE_DUP|RET_NULL:
            args[numargs - 1].get_val(args[numargs]);
            numargs++;
            continue;
        case CODE_DUP|RET_INT:
            args[numargs].set_int(args[numargs - 1].get_int());
            numargs++;
            continue;
        case CODE_DUP|RET_FLOAT:
            args[numargs].set_float(args[numargs - 1].get_float());
            numargs++;
            continue;
        case CODE_DUP|RET_STR:
            args[numargs].set_str_dup(args[numargs - 1].get_str());
            numargs++;
            continue;

        case CODE_FORCE|RET_STR:
            args[numargs - 1].force_str();
            continue;
        case CODE_FORCE|RET_INT:
            args[numargs - 1].force_int();
            continue;
        case CODE_FORCE|RET_FLOAT:
            args[numargs - 1].force_float();
            continue;

        case CODE_RESULT|RET_NULL:
            result.cleanup();
            result = args[--numargs];
            continue;
        case CODE_RESULT|RET_STR:
        case CODE_RESULT|RET_INT:
        case CODE_RESULT|RET_FLOAT:
            result.cleanup();
            result = args[--numargs];
            result.force(op & CODE_RET_MASK);
            continue;

        case CODE_EMPTY|RET_NULL:
            args[numargs++].set_code(emptyblock[VAL_NULL] + 1);
            break;
        case CODE_EMPTY|RET_STR:
            args[numargs++].set_code(emptyblock[VAL_STR] + 1);
            break;
        case CODE_EMPTY|RET_INT:
            args[numargs++].set_code(emptyblock[VAL_INT] + 1);
            break;
        case CODE_EMPTY|RET_FLOAT:
            args[numargs++].set_code(emptyblock[VAL_FLOAT] + 1);
            break;
        case CODE_BLOCK: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_code(code + 1);
            code += len;
            continue;
        }
        case CODE_COMPILE: {
            TaggedValue &arg = args[numargs - 1];
            GenState gs(cs);
            switch (arg.get_type()) {
            case VAL_INT:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_int(arg.i);
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            case VAL_FLOAT:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_float(arg.f);
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                gs.code.reserve(64);
                gs.gen_main(arg.s);
                arg.cleanup();
                break;
            default:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_null();
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            }
            arg.set_code(gs.code.disown() + 1);
            continue;
        }
        case CODE_COND: {
            TaggedValue &arg = args[numargs - 1];
            switch (arg.get_type()) {
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                if (arg.s[0]) {
                    GenState gs(cs);
                    gs.code.reserve(64);
                    gs.gen_main(arg.s);
                    arg.cleanup();
                    arg.set_code(gs.code.disown() + 1);
                } else arg.force_null();
                break;
            }
            continue;
        }

        case CODE_IDENT:
            args[numargs++].set_ident(cs.identmap[op >> 8]);
            continue;
        case CODE_IDENTARG: {
            Ident *id = cs.identmap[op >> 8];
            if (!(cs.stack->usedargs & (1 << id->index))) {
                id->push_arg(null_value, cs.stack->argstack[id->index], false);
                cs.stack->usedargs |= 1 << id->index;
            }
            args[numargs++].set_ident(id);
            continue;
        }
        case CODE_IDENTU: {
            TaggedValue &arg = args[numargs - 1];
            Ident *id = arg.get_type() == VAL_STR || arg.get_type() == VAL_MACRO || arg.get_type() == VAL_CSTR ? cs.new_ident(arg.cstr) : cs.dummy;
            if (id->index < MAX_ARGUMENTS && !(cs.stack->usedargs & (1 << id->index))) {
                id->push_arg(null_value, cs.stack->argstack[id->index], false);
                cs.stack->usedargs |= 1 << id->index;
            }
            arg.cleanup();
            arg.set_ident(id);
            continue;
        }

        case CODE_LOOKUPU|RET_STR:
#define LOOKUPU(aval, sval, ival, fval, nval) { \
                    TaggedValue &arg = args[numargs-1]; \
                    if(arg.get_type() != VAL_STR && arg.get_type() != VAL_MACRO && arg.get_type() != VAL_CSTR) continue; \
                    Ident *id = cs.idents.at(arg.s); \
                    if(id) switch(id->type) \
                    { \
                        case ID_ALIAS: \
                            if(id->flags&IDF_UNKNOWN) break; \
                            arg.cleanup(); \
                            if(id->index < MAX_ARGUMENTS && !(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                            aval; \
                            continue; \
                        case ID_SVAR: arg.cleanup(); sval; continue; \
                        case ID_VAR: arg.cleanup(); ival; continue; \
                        case ID_FVAR: arg.cleanup(); fval; continue; \
                        case ID_COMMAND: \
                        { \
                            arg.cleanup(); \
                            arg.set_null(); \
                            cs.result = &arg; \
                            TaggedValue buf[MAX_ARGUMENTS]; \
                            callcommand(cs, id, buf, 0, true); \
                            arg.force(op&CODE_RET_MASK); \
                            cs.result = &result; \
                            continue; \
                        } \
                        default: arg.cleanup(); nval; continue; \
                    } \
                    cs_debug_code(cs, "unknown alias lookup: %s", arg.s); \
                    arg.cleanup(); \
                    nval; \
                    continue; \
                }
            LOOKUPU(arg.set_str_dup(id->get_str()),
                    arg.set_str_dup(*id->storage.sp),
                    arg.set_str_dup(intstr(*id->storage.ip)),
                    arg.set_str_dup(floatstr(*id->storage.fp)),
                    arg.set_str_dup(""));
        case CODE_LOOKUP|RET_STR:
#define LOOKUP(aval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(id->flags&IDF_UNKNOWN) cs_debug_code(cs, "unknown alias lookup: %s", id->name); \
                    aval; \
                    continue; \
                }
            LOOKUP(args[numargs++].set_str_dup(id->get_str()));
        case CODE_LOOKUPARG|RET_STR:
#define LOOKUPARG(aval, nval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(!(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                    aval; \
                    continue; \
                }
            LOOKUPARG(args[numargs++].set_str_dup(id->get_str()), args[numargs++].set_str_dup(""));
        case CODE_LOOKUPU|RET_INT:
            LOOKUPU(arg.set_int(id->get_int()),
                    arg.set_int(parseint(*id->storage.sp)),
                    arg.set_int(*id->storage.ip),
                    arg.set_int(int(*id->storage.fp)),
                    arg.set_int(0));
        case CODE_LOOKUP|RET_INT:
            LOOKUP(args[numargs++].set_int(id->get_int()));
        case CODE_LOOKUPARG|RET_INT:
            LOOKUPARG(args[numargs++].set_int(id->get_int()), args[numargs++].set_int(0));
        case CODE_LOOKUPU|RET_FLOAT:
            LOOKUPU(arg.set_float(id->get_float()),
                    arg.set_float(parsefloat(*id->storage.sp)),
                    arg.set_float(float(*id->storage.ip)),
                    arg.set_float(*id->storage.fp),
                    arg.set_float(0.0f));
        case CODE_LOOKUP|RET_FLOAT:
            LOOKUP(args[numargs++].set_float(id->get_float()));
        case CODE_LOOKUPARG|RET_FLOAT:
            LOOKUPARG(args[numargs++].set_float(id->get_float()), args[numargs++].set_float(0.0f));
        case CODE_LOOKUPU|RET_NULL:
            LOOKUPU(id->get_val(arg),
                    arg.set_str_dup(*id->storage.sp),
                    arg.set_int(*id->storage.ip),
                    arg.set_float(*id->storage.fp),
                    arg.set_null());
        case CODE_LOOKUP|RET_NULL:
            LOOKUP(id->get_val(args[numargs++]));
        case CODE_LOOKUPARG|RET_NULL:
            LOOKUPARG(id->get_val(args[numargs++]), args[numargs++].set_null());

        case CODE_LOOKUPMU|RET_STR:
            LOOKUPU(id->get_cstr(arg),
                    arg.set_cstr(*id->storage.sp),
                    arg.set_str_dup(intstr(*id->storage.ip)),
                    arg.set_str_dup(floatstr(*id->storage.fp)),
                    arg.set_cstr(""));
        case CODE_LOOKUPM|RET_STR:
            LOOKUP(id->get_cstr(args[numargs++]));
        case CODE_LOOKUPMARG|RET_STR:
            LOOKUPARG(id->get_cstr(args[numargs++]), args[numargs++].set_cstr(""));
        case CODE_LOOKUPMU|RET_NULL:
            LOOKUPU(id->get_cval(arg),
                    arg.set_cstr(*id->storage.sp),
                    arg.set_int(*id->storage.ip),
                    arg.set_float(*id->storage.fp),
                    arg.set_null());
        case CODE_LOOKUPM|RET_NULL:
            LOOKUP(id->get_cval(args[numargs++]));
        case CODE_LOOKUPMARG|RET_NULL:
            LOOKUPARG(id->get_cval(args[numargs++]), args[numargs++].set_null());

        case CODE_SVAR|RET_STR:
        case CODE_SVAR|RET_NULL:
            args[numargs++].set_str_dup(*cs.identmap[op >> 8]->storage.sp);
            continue;
        case CODE_SVAR|RET_INT:
            args[numargs++].set_int(parseint(*cs.identmap[op >> 8]->storage.sp));
            continue;
        case CODE_SVAR|RET_FLOAT:
            args[numargs++].set_float(parsefloat(*cs.identmap[op >> 8]->storage.sp));
            continue;
        case CODE_SVARM:
            args[numargs++].set_cstr(*cs.identmap[op >> 8]->storage.sp);
            continue;
        case CODE_SVAR1:
            cs.set_var_str_checked(cs.identmap[op >> 8], args[--numargs].s);
            args[numargs].cleanup();
            continue;

        case CODE_IVAR|RET_INT:
        case CODE_IVAR|RET_NULL:
            args[numargs++].set_int(*cs.identmap[op >> 8]->storage.ip);
            continue;
        case CODE_IVAR|RET_STR:
            args[numargs++].set_str_dup(intstr(*cs.identmap[op >> 8]->storage.ip));
            continue;
        case CODE_IVAR|RET_FLOAT:
            args[numargs++].set_float(float(*cs.identmap[op >> 8]->storage.ip));
            continue;
        case CODE_IVAR1:
            cs.set_var_int_checked(cs.identmap[op >> 8], args[--numargs].i);
            continue;
        case CODE_IVAR2:
            numargs -= 2;
            cs.set_var_int_checked(cs.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8));
            continue;
        case CODE_IVAR3:
            numargs -= 3;
            cs.set_var_int_checked(cs.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8) | args[numargs + 2].i);
            continue;

        case CODE_FVAR|RET_FLOAT:
        case CODE_FVAR|RET_NULL:
            args[numargs++].set_float(*cs.identmap[op >> 8]->storage.fp);
            continue;
        case CODE_FVAR|RET_STR:
            args[numargs++].set_str_dup(floatstr(*cs.identmap[op >> 8]->storage.fp));
            continue;
        case CODE_FVAR|RET_INT:
            args[numargs++].set_int(int(*cs.identmap[op >> 8]->storage.fp));
            continue;
        case CODE_FVAR1:
            cs.set_var_float_checked(cs.identmap[op >> 8], args[--numargs].f);
            continue;

#define OFFSETARG(n) offset+n
        case CODE_COM|RET_NULL:
        case CODE_COM|RET_STR:
        case CODE_COM|RET_FLOAT:
        case CODE_COM|RET_INT: {
            Ident *id = cs.identmap[op >> 8];
            int offset = numargs - id->numargs;
            result.force_null();
            CALLCOM(id->numargs)
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
            }
#undef OFFSETARG

        case CODE_COMV|RET_NULL:
        case CODE_COMV|RET_STR:
        case CODE_COMV|RET_FLOAT:
        case CODE_COMV|RET_INT: {
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            ((CommandFuncTv)id->fun)(cs, ostd::iter(&args[offset], callargs));
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }
        case CODE_COMC|RET_NULL:
        case CODE_COMC|RET_STR:
        case CODE_COMC|RET_FLOAT:
        case CODE_COMC|RET_INT: {
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            {
                ostd::Vector<char> buf;
                buf.reserve(256);
                ((CommandFunc1)id->fun)(cs, conc(buf, ostd::iter(&args[offset], callargs), true));
            }
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }

        case CODE_CONC|RET_NULL:
        case CODE_CONC|RET_STR:
        case CODE_CONC|RET_FLOAT:
        case CODE_CONC|RET_INT:
        case CODE_CONCW|RET_NULL:
        case CODE_CONCW|RET_STR:
        case CODE_CONCW|RET_FLOAT:
        case CODE_CONCW|RET_INT: {
            int numconc = op >> 8;
            char *s = conc(ostd::iter(&args[numargs - numconc], numconc), (op & CODE_OP_MASK) == CODE_CONC);
            free_args(args, numargs, numargs - numconc);
            args[numargs].set_str(s);
            args[numargs].force(op & CODE_RET_MASK);
            numargs++;
            continue;
        }

        case CODE_CONCM|RET_NULL:
        case CODE_CONCM|RET_STR:
        case CODE_CONCM|RET_FLOAT:
        case CODE_CONCM|RET_INT: {
            int numconc = op >> 8;
            char *s = conc(ostd::iter(&args[numargs - numconc], numconc), false);
            free_args(args, numargs, numargs - numconc);
            result.set_str(s);
            result.force(op & CODE_RET_MASK);
            continue;
        }

        case CODE_ALIAS:
            cs.identmap[op >> 8]->set_alias(cs, args[--numargs]);
            continue;
        case CODE_ALIASARG:
            cs.identmap[op >> 8]->set_arg(cs, args[--numargs]);
            continue;
        case CODE_ALIASU:
            numargs -= 2;
            cs.set_alias(args[numargs].get_str(), args[numargs + 1]);
            args[numargs].cleanup();
            continue;

#define SKIPARGS(offset) offset
        case CODE_CALL|RET_NULL:
        case CODE_CALL|RET_STR:
        case CODE_CALL|RET_FLOAT:
        case CODE_CALL|RET_INT: {
#define FORCERESULT { \
                free_args(args, numargs, SKIPARGS(offset)); \
                result.force(op&CODE_RET_MASK); \
                continue; \
            }
#define CALLALIAS(cs, result) { \
                IdentStack argstack[MAX_ARGUMENTS]; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->push_arg(args[offset + i], argstack[i], false); \
                int oldargs = (cs).numargs; \
                (cs).numargs = callargs; \
                int oldflags = (cs).identflags; \
                (cs).identflags |= id->flags&IDF_OVERRIDDEN; \
                IdentLink aliaslink = { id, (cs).stack, (1<<callargs)-1, argstack }; \
                (cs).stack = &aliaslink; \
                if(!id->code) id->code = (cs).compile(id->get_str()); \
                ostd::Uint32 *code = id->code; \
                code[0] += 0x100; \
                runcode((cs), code+1, (result)); \
                code[0] -= 0x100; \
                if(int(code[0]) < 0x100) delete[] code; \
                (cs).stack = aliaslink.next; \
                (cs).identflags = oldflags; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->pop_arg(); \
                for(int argmask = aliaslink.usedargs&(~0<<callargs), i = callargs; argmask; i++) \
                    if(argmask&(1<<i)) { (cs).identmap[i]->pop_arg(); argmask &= ~(1<<i); } \
                (result).force(op&CODE_RET_MASK); \
                (cs).numargs = oldargs; \
                numargs = SKIPARGS(offset); \
            }
            result.force_null();
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (id->flags & IDF_UNKNOWN) {
                cs_debug_code(cs, "unknown command: %s", id->name);
                FORCERESULT;
            }
            CALLALIAS(cs, result);
            continue;
        }
        case CODE_CALLARG|RET_NULL:
        case CODE_CALLARG|RET_STR:
        case CODE_CALLARG|RET_FLOAT:
        case CODE_CALLARG|RET_INT: {
            result.force_null();
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (!(cs.stack->usedargs & (1 << id->index))) FORCERESULT;
            CALLALIAS(cs, result);
            continue;
        }
#undef SKIPARGS

#define SKIPARGS(offset) offset-1
        case CODE_CALLU|RET_NULL:
        case CODE_CALLU|RET_STR:
        case CODE_CALLU|RET_FLOAT:
        case CODE_CALLU|RET_INT: {
            int callargs = op >> 8, offset = numargs - callargs;
            TaggedValue &idarg = args[offset - 1];
            if (idarg.get_type() != VAL_STR && idarg.get_type() != VAL_MACRO && idarg.get_type() != VAL_CSTR) {
litval:
                result.cleanup();
                result = idarg;
                result.force(op & CODE_RET_MASK);
                while (--numargs >= offset) args[numargs].cleanup();
                continue;
            }
            Ident *id = cs.idents.at(idarg.s);
            if (!id) {
noid:
                if (cs_check_num(idarg.s)) goto litval;
                cs_debug_code(cs, "unknown command: %s", idarg.s);
                result.force_null();
                FORCERESULT;
            }
            result.force_null();
            switch (id->type) {
            default:
                if (!id->fun) FORCERESULT;
            /* fallthrough */
            case ID_COMMAND:
                idarg.cleanup();
                callcommand(cs, id, &args[offset], callargs);
                result.force(op & CODE_RET_MASK);
                numargs = offset - 1;
                continue;
            case ID_LOCAL: {
                IdentStack locals[MAX_ARGUMENTS];
                idarg.cleanup();
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) cs.force_ident(args[offset + j])->push_alias(locals[j]);
                code = runcode(cs, code, result);
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) args[offset + j].id->pop_alias();
                goto exit;
            }
            case ID_VAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_int_checked(id, ostd::iter(&args[offset], callargs));
                FORCERESULT;
            case ID_FVAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_float_checked(id, args[offset].force_float());
                FORCERESULT;
            case ID_SVAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_str_checked(id, args[offset].force_str());
                FORCERESULT;
            case ID_ALIAS:
                if (id->index < MAX_ARGUMENTS && !(cs.stack->usedargs & (1 << id->index))) FORCERESULT;
                if (id->get_valtype() == VAL_NULL) goto noid;
                idarg.cleanup();
                CALLALIAS(cs, result);
                continue;
            }
        }
#undef SKIPARGS
        }
    }
exit:
    cs.result = prevret;
    --rundepth;
    return code;
}

void CsState::run_ret(const ostd::Uint32 *code, TaggedValue &result) {
    runcode(*this, code, result);
}

void CsState::run_ret(ostd::ConstCharRange code, TaggedValue &result) {
    GenState gs(*this);
    gs.code.reserve(64);
    /* FIXME range */
    gs.gen_main(code.data(), VAL_ANY);
    runcode(*this, gs.code.data() + 1, result);
    if (int(gs.code[0]) >= 0x100)
        gs.code.disown();
}

/* TODO */
void CsState::run_ret(Ident *id, ostd::PointerRange<TaggedValue> args,
                      TaggedValue &ret) {
    int numargs = int(args.size());
    ret.set_null();
    ++rundepth;
    TaggedValue *prevret = result;
    result = &ret;
    if (rundepth > MAXRUNDEPTH) cs_debug_code(*this, "exceeded recursion limit");
    else if (id) switch (id->type) {
        default:
            if (!id->fun) break;
        /* fallthrough */
        case ID_COMMAND:
            if (numargs < id->numargs) {
                TaggedValue buf[MAX_ARGUMENTS];
                memcpy(buf, args.data(), args.size() * sizeof(TaggedValue));
                callcommand(*this, id, buf, numargs, false);
            } else callcommand(*this, id, args.data(), numargs, false);
            numargs = 0;
            break;
        case ID_VAR:
            if (args.empty()) print_var(id);
            else set_var_int_checked(id, args);
            break;
        case ID_FVAR:
            if (args.empty()) print_var(id);
            else set_var_float_checked(id, args[0].force_float());
            break;
        case ID_SVAR:
            if (args.empty()) print_var(id);
            else set_var_str_checked(id, args[0].force_str());
            break;
        case ID_ALIAS:
            if (id->index < MAX_ARGUMENTS && !(stack->usedargs & (1 << id->index))) break;
            if (id->get_valtype() == VAL_NULL) break;
#define callargs numargs
#define offset 0
#define op RET_NULL
#define SKIPARGS(offset) offset
            CALLALIAS(*this, ret);
#undef callargs
#undef offset
#undef op
#undef SKIPARGS
            break;
        }
    free_args(args.data(), numargs, 0);
    result = prevret;
    --rundepth;
}

ostd::String CsState::run_str(const ostd::Uint32 *code) {
    TaggedValue result;
    runcode(*this, code, result);
    if (result.get_type() == VAL_NULL) return ostd::String();
    result.force_str();
    ostd::String ret(result.s);
    delete[] result.s;
    return ret;
}

ostd::String CsState::run_str(ostd::ConstCharRange code) {
    TaggedValue result;
    /* FIXME range */
    run_ret(code, result);
    if (result.get_type() == VAL_NULL) return ostd::String();
    result.force_str();
    ostd::String ret(result.s);
    delete[] result.s;
    return ret;
}

ostd::String CsState::run_str(Ident *id, ostd::PointerRange<TaggedValue> args) {
    TaggedValue result;
    run_ret(id, args, result);
    if (result.get_type() == VAL_NULL) return nullptr;
    result.force_str();
    ostd::String ret(result.s);
    delete[] result.s;
    return ret;
}

int CsState::run_int(const ostd::Uint32 *code) {
    TaggedValue result;
    runcode(*this, code, result);
    int i = result.get_int();
    result.cleanup();
    return i;
}

int CsState::run_int(ostd::ConstCharRange p) {
    GenState gs(*this);
    gs.code.reserve(64);
    gs.gen_main(p.data(), VAL_INT);
    TaggedValue result;
    runcode(*this, gs.code.data() + 1, result);
    if (int(gs.code[0]) >= 0x100) gs.code.disown();
    int i = result.get_int();
    result.cleanup();
    return i;
}

int CsState::run_int(Ident *id, ostd::PointerRange<TaggedValue> args) {
    TaggedValue result;
    run_ret(id, args, result);
    int i = result.get_int();
    result.cleanup();
    return i;
}

float CsState::run_float(const ostd::Uint32 *code) {
    TaggedValue result;
    runcode(*this, code, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

float CsState::run_float(ostd::ConstCharRange code) {
    TaggedValue result;
    run_ret(code, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

float CsState::run_float(Ident *id, ostd::PointerRange<TaggedValue> args) {
    TaggedValue result;
    run_ret(id, args, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

bool CsState::run_bool(const ostd::Uint32 *code) {
    TaggedValue result;
    runcode(*this, code, result);
    bool b = cs_get_bool(result);
    result.cleanup();
    return b;
}

bool CsState::run_bool(ostd::ConstCharRange code) {
    TaggedValue result;
    run_ret(code, result);
    bool b = cs_get_bool(result);
    result.cleanup();
    return b;
}

bool CsState::run_bool(Ident *id, ostd::PointerRange<TaggedValue> args) {
    TaggedValue result;
    run_ret(id, args, result);
    bool b = cs_get_bool(result);
    result.cleanup();
    return b;
}

bool CsState::run_file(ostd::ConstCharRange fname, bool msg) {
    ostd::ConstCharRange oldsrcfile = src_file, oldsrcstr = src_str;
    char *buf = nullptr;
    ostd::Size len;

    ostd::FileStream f(fname, ostd::StreamMode::read);
    if (!f.is_open()) goto error;

    len = f.size();
    buf = new char[len + 1];
    if (f.get(buf, len) != len) {
        delete[] buf;
        goto error;
    }
    buf[len] = '\0';

    src_file = fname;
    src_str = ostd::ConstCharRange(buf, len);
    run_int(buf);
    src_file = oldsrcfile;
    src_str = oldsrcstr;
    delete[] buf;
    return true;

error:
    if (msg) ostd::err.writefln("could not read file \"%s\"", fname);
    return false;
}

void init_lib_io(CsState &cs) {
    cs.add_command("exec", "sb", [](CsState &cs, char *file, int *msg) {
        cs.result->set_int(cs.run_file(file, *msg != 0) ? 1 : 0);
    });

    cs.add_command("echo", "C", [](CsState &, char *s) {
        ostd::writeln(s);
    });
}

void cs_init_lib_base_loops(CsState &cs);

void init_lib_base(CsState &cs) {
    cs.add_command("do", "e", [](CsState &cs, ostd::Uint32 *body) {
        cs.run_ret(body);
    }, ID_DO);

    cs.add_command("doargs", "e", [](CsState &cs, ostd::Uint32 *body) {
        if (cs.stack != &cs.noalias)
            cs_do_args(cs, [&]() { cs.run_ret(body); });
        else
            cs.run_ret(body);
    }, ID_DOARGS);

    cs.add_command("if", "tee", [](CsState &cs, TaggedValue *cond,
                                   ostd::Uint32 *t, ostd::Uint32 *f) {
        cs.run_ret(cs_get_bool(*cond) ? t : f);
    }, ID_IF);

    cs.add_command("result", "T", [](CsState &cs, TaggedValue *v) {
        *cs.result = *v;
        v->set_null();
    }, ID_RESULT);

    cs.add_command("!", "t", [](CsState &cs, TaggedValue *a) {
        cs.result->set_int(!cs_get_bool(*a));
    }, ID_NOT);

    cs.add_command("&&", "E1V", [](CsState &cs,
                                   ostd::PointerRange<TaggedValue> args) {
        if (args.empty())
            cs.result->set_int(1);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) cs.result->cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code);
            else
                *cs.result = args[i];
            if (!cs_get_bool(*cs.result)) break;
        }
    }, ID_AND);

    cs.add_command("||", "E1V", [](CsState &cs,
                                   ostd::PointerRange<TaggedValue> args) {
        if (args.empty())
            cs.result->set_int(0);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) cs.result->cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code);
            else
                *cs.result = args[i];
            if (cs_get_bool(*cs.result)) break;
        }
    }, ID_OR);

    cs.add_command("?", "tTT", [](CsState &cs, TaggedValue *cond,
                                  TaggedValue *t, TaggedValue *f) {
        cs.result->set(*(cs_get_bool(*cond) ? t : f));
    });

    cs.add_command("cond", "ee2V", [](CsState &cs,
                                      ostd::PointerRange<TaggedValue> args) {
        for (ostd::Size i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run_bool(args[i].code)) {
                    cs.run_ret(args[i + 1].code);
                    break;
                }
            } else {
                cs.run_ret(args[i].code);
                break;
            }
        }
    });

#define CS_CMD_CASE(name, fmt, type, acc, compare) \
    cs.add_command(name, fmt "te2V", [](CsState &cs, \
                                        ostd::PointerRange<TaggedValue> args) { \
        type val = acc; \
        ostd::Size i; \
        for (i = 1; (i + 1) < args.size(); i += 2) { \
            if (compare) { \
                cs.run_ret(args[i + 1].code); \
                return; \
            } \
        } \
    });

    CS_CMD_CASE("case", "i", int, args[0].get_int(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_int() == val)));

    CS_CMD_CASE("casef", "f", float, args[0].get_float(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_float() == val)));

    CS_CMD_CASE("cases", "s", ostd::ConstCharRange, args[0].get_str(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_str() == val)));

#undef CS_CMD_CASE

    cs.add_command("pushif", "rTe", [](CsState &cs, Ident *id,
                                       TaggedValue *v, ostd::Uint32 *code) {
        if ((id->type != ID_ALIAS) || (id->index < MAX_ARGUMENTS))
            return;
        if (cs_get_bool(*v)) {
            IdentStack stack;
            id->push_arg(*v, stack);
            v->set_null();
            cs.run_ret(code);
            id->pop_arg();
        }
    });

    cs_init_lib_base_loops(cs);
    cs_init_lib_base_var(cs);
}

static inline void cs_set_iter(Ident &id, int i, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.get_valtype() != VAL_INT) {
            if (id.get_valtype() == VAL_STR) delete[] id.val.s;
            id.clean_code();
            id.valtype = VAL_INT;
        }
        id.val.i = i;
        return;
    }
    TaggedValue v;
    v.set_int(i);
    id.push_arg(v, stack);
}

static inline void cs_do_loop(CsState &cs, Ident &id, int offset, int n,
                              int step, ostd::Uint32 *cond, ostd::Uint32 *body) {
    if (n <= 0 || (id.type != ID_ALIAS))
        return;
    IdentStack stack;
    for (int i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        if (cond && !cs.run_bool(cond)) break;
        cs.run_int(body);
    }
    id.pop_arg();
}

static inline void cs_loop_conc(CsState &cs, Ident &id, int offset, int n,
                                int step, ostd::Uint32 *body, bool space) {
    if (n <= 0 || id.type != ID_ALIAS)
        return;
    IdentStack stack;
    ostd::Vector<char> s;
    for (int i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        TaggedValue v;
        cs.run_ret(body, v);
        ostd::ConstCharRange vstr = v.get_str();
        if (space && i) s.push(' ');
        s.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n > 0) id.pop_arg();
    s.push('\0');
    ostd::Size len = s.size() - 1;
    cs.result->set_str(ostd::CharRange(s.disown(), len));
}

void cs_init_lib_base_loops(CsState &cs) {
    cs.add_command("loop", "rie", [](CsState &cs, Ident *id, int *n,
                                     ostd::Uint32 *body) {
        cs_do_loop(cs, *id, 0, *n, 1, nullptr, body);
    });

    cs.add_command("loop+", "riie", [](CsState &cs, Ident *id, int *offset,
                                       int *n, ostd::Uint32 *body) {
        cs_do_loop(cs, *id, *offset, *n, 1, nullptr, body);
    });

    cs.add_command("loop*", "riie", [](CsState &cs, Ident *id, int *step,
                                       int *n, ostd::Uint32 *body) {
        cs_do_loop(cs, *id, 0, *n, *step, nullptr, body);
    });

    cs.add_command("loop+*", "riiie", [](CsState &cs, Ident *id, int *offset,
                                         int *step, int *n, ostd::Uint32 *body) {
        cs_do_loop(cs, *id, *offset, *n, *step, nullptr, body);
    });

    cs.add_command("loopwhile", "riee", [](CsState &cs, Ident *id, int *n,
                                           ostd::Uint32 *cond,
                                           ostd::Uint32 *body) {
        cs_do_loop(cs, *id, 0, *n, 1, cond, body);
    });

    cs.add_command("loopwhile+", "riiee", [](CsState &cs, Ident *id,
                                             int *offset, int *n,
                                             ostd::Uint32 *cond,
                                             ostd::Uint32 *body) {
        cs_do_loop(cs, *id, *offset, *n, 1, cond, body);
    });

    cs.add_command("loopwhile*", "riiee", [](CsState &cs, Ident *id,
                                             int *step, int *n,
                                             ostd::Uint32 *cond,
                                             ostd::Uint32 *body) {
        cs_do_loop(cs, *id, 0, *n, *step, cond, body);
    });

    cs.add_command("loopwhile+*", "riiiee", [](CsState &cs, Ident *id,
                                               int *offset, int *step,
                                               int *n, ostd::Uint32 *cond,
                                               ostd::Uint32 *body) {
        cs_do_loop(cs, *id, *offset, *n, *step, cond, body);
    });

    cs.add_command("while", "ee", [](CsState &cs, ostd::Uint32 *cond,
                                     ostd::Uint32 *body) {
        while (cs.run_bool(cond)) cs.run_int(body);
    });

    cs.add_command("loopconcat", "rie", [](CsState &cs, Ident *id, int *n,
                                           ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, 0, *n, 1, body, true);
    });

    cs.add_command("loopconcat+", "riie", [](CsState &cs, Ident *id,
                                             int *offset, int *n,
                                             ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, *offset, *n, 1, body, true);
    });

    cs.add_command("loopconcat*", "riie", [](CsState &cs, Ident *id,
                                             int *step, int *n,
                                             ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, 0, *n, *step, body, true);
    });

    cs.add_command("loopconcat+*", "riiie", [](CsState &cs, Ident *id,
                                               int *offset, int *step,
                                               int *n, ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, *offset, *n, *step, body, true);
    });

    cs.add_command("loopconcatword", "rie", [](CsState &cs, Ident *id,
                                               int *n, ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, 0, *n, 1, body, false);
    });

    cs.add_command("loopconcatword+", "riie", [](CsState &cs, Ident *id,
                                                 int *offset, int *n,
                                                 ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, *offset, *n, 1, body, false);
    });

    cs.add_command("loopconcatword*", "riie", [](CsState &cs, Ident *id,
                                                 int *step, int *n,
                                                 ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, 0, *n, *step, body, false);
    });

    cs.add_command("loopconcatword+*", "riiie", [](CsState &cs, Ident *id,
                                                   int *offset, int *step,
                                                   int *n, ostd::Uint32 *body) {
        cs_loop_conc(cs, *id, *offset, *n, *step, body, false);
    });
}

struct ListParser {
    ostd::ConstCharRange input;
    ostd::ConstCharRange quote = ostd::ConstCharRange();
    ostd::ConstCharRange item = ostd::ConstCharRange();

    ListParser() = delete;
    ListParser(ostd::ConstCharRange src): input(src) {}

    void skip() {
        for (;;) {
            while (!input.empty()) {
                char c = input.front();
                if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
                    input.pop_front();
                else
                    break;
            }
            if ((input.size() < 2) || (input[0] != '/') || (input[1] != '/'))
                break;
            input = ostd::find(input, '\n');
        }
    }

    bool parse() {
        skip();
        if (input.empty())
            return false;
        switch (input.front()) {
        case '"':
            quote = input;
            input.pop_front();
            item = input;
            input = cs_parse_str(input);
            item = ostd::slice_until(item, input);
            if (!input.empty() && (input.front() == '"'))
                input.pop_front();
            quote = ostd::slice_until(quote, input);
            break;
        case '(':
        case '[': {
            quote = input;
            input.pop_front();
            item = input;
            char btype = quote.front();
            int brak = 1;
            for (;;) {
                input = ostd::find_one_of(input,
                    ostd::ConstCharRange("\"/;()[]"));
                if (input.empty())
                    return true;
                char c = input.front();
                input.pop_front();
                switch (c) {
                case '"':
                    input = cs_parse_str(input);
                    if (!input.empty() && (input.front() == '"'))
                        input.pop_front();
                    break;
                case '/':
                    if (!input.empty() && (input.front() == '/'))
                        input = ostd::find(input, '\n');
                    break;
                case '(':
                case '[':
                    brak += (c == btype);
                    break;
                case ')':
                    if ((btype == '(') && (--brak <= 0))
                        goto endblock;
                    break;
                case ']':
                    if ((btype == '[') && (--brak <= 0))
                        goto endblock;
                    break;
                }
            }
endblock:
            item = ostd::slice_until(item, input);
            item.pop_back();
            quote = ostd::slice_until(quote, input);
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            const char *e = parseword(input.data());
            item = input;
            input.pop_front_n(e - input.data());
            item = ostd::slice_until(item, input);
            quote = item;
            break;
        }
        }
        skip();
        if (!input.empty() && (input.front() == ';'))
            input.pop_front();
        return true;
    }

    ostd::String element() {
        ostd::String s;
        s.reserve(item.size());
        if (!quote.empty() && (quote.front() == '"')) {
            auto writer = s.iter_cap();
            util::unescape_string(writer, item);
            writer.put('\0');
        } else {
            memcpy(s.data(), item.data(), item.size());
            s[item.size()] = '\0';
        }
        s.advance(item.size());
        return s;
    }
};

namespace util {
    ostd::Size list_length(ostd::ConstCharRange s) {
        ListParser p(s);
        ostd::Size ret = 0;
        while (p.parse()) ++ret;
        return ret;
    }

    ostd::Maybe<ostd::String> list_index(ostd::ConstCharRange s,
                                         ostd::Size idx) {
        ListParser p(s);
        for (ostd::Size i = 0; i < idx; ++i)
            if (!p.parse()) return ostd::nothing;
        if (!p.parse())
            return ostd::nothing;
        return ostd::move(p.element());
    }

    ostd::Vector<ostd::String> list_explode(ostd::ConstCharRange s,
                                            ostd::Size limit) {
        ostd::Vector<ostd::String> ret;
        ListParser p(s);
        while ((ret.size() < limit) && p.parse())
            ret.push(ostd::move(p.element()));
        return ret;
    }
}

static inline void cs_set_iter(Ident &id, char *val, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.get_valtype() == VAL_STR)
            delete[] id.val.s;
        else
            id.valtype = VAL_STR | (strlen(val) << 4);
        id.clean_code();
        id.val.s = val;
        return;
    }
    TaggedValue v;
    v.set_str(val);
    id.push_arg(v, stack);
}

static void cs_loop_list_conc(CsState &cs, Ident *id, const char *list,
                              const ostd::Uint32 *body, bool space) {
    if (id->type != ID_ALIAS)
        return;
    IdentStack stack;
    ostd::Vector<char> r;
    int n = 0;
    for (ListParser p(list); p.parse(); ++n) {
        char *val = p.element().disown();
        cs_set_iter(*id, val, stack);
        if (n && space)
            r.push(' ');
        TaggedValue v;
        cs.run_ret(body, v);
        ostd::ConstCharRange vstr = v.get_str();
        r.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n >= 0)
        id->pop_arg();
    r.push('\0');
    ostd::Size len = r.size();
    cs.result->set_str(ostd::CharRange(r.disown(), len - 1));
}

int cs_list_includes(const char *list, ostd::ConstCharRange needle) {
    int offset = 0;
    for (ListParser p(list); p.parse();) {
        if (p.item == needle)
            return offset;
        ++offset;
    }
    return -1;
}

static void cs_init_lib_list_sort(CsState &cs);

void init_lib_list(CsState &cs) {
    cs.add_command("listlen", "s", [](CsState &cs, char *s) {
        cs.result->set_int(int(util::list_length(s)));
    });

    cs.add_command("at", "si1V", [](CsState &cs,
                                    ostd::PointerRange<TaggedValue> args) {
        if (args.empty())
            return;
        ostd::ConstCharRange str = args[0].get_str();
        ListParser p(str);
        p.item = str;
        for (ostd::Size i = 1; i < args.size(); ++i) {
            p.input = str;
            int pos = args[i].get_int();
            for (; pos > 0; --pos)
                if (!p.parse()) break;
            if (pos > 0 || !p.parse())
                p.item = p.quote = ostd::ConstCharRange();
        }
        auto elem = p.element();
        auto er = p.element().iter();
        elem.disown();
        cs.result->set_str(er);
    });

    cs.add_command("sublist", "siiN", [](CsState &cs, const char *s,
                                         int *skip, int *count, int *numargs) {
        int offset = ostd::max(*skip, 0),
            len = (*numargs >= 3) ? ostd::max(*count, 0) : -1;
        ListParser p(s);
        for (int i = 0; i < offset; ++i)
            if (!p.parse()) break;
        if (len < 0) {
            if (offset > 0)
                p.skip();
            cs.result->set_str_dup(p.input);
            return;
        }

        const char *list = p.input.data();
        p.quote = ostd::ConstCharRange();
        if (len > 0 && p.parse())
            while (--len > 0 && p.parse());
        const char *qend = !p.quote.empty() ? &p.quote[p.quote.size()] : list;
        cs.result->set_str_dup(ostd::ConstCharRange(list, qend - list));
    });

    cs.add_command("listfind", "rse", [](CsState &cs, Ident *id,
                                         const char *list,
                                         const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS) {
            cs.result->set_int(-1);
            return;
        }
        IdentStack stack;
        int n = -1;
        for (ListParser p(list); p.parse();) {
            ++n;
            cs_set_iter(*id, cs_dup_ostr(p.item), stack);
            if (cs.run_bool(body)) {
                cs.result->set_int(n);
                goto found;
            }
        }
        cs.result->set_int(-1);
found:
        if (n >= 0)
            id->pop_arg();
    });

    cs.add_command("listassoc", "rse", [](CsState &cs, Ident *id,
                                         const char *list,
                                         const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS)
            return;
        IdentStack stack;
        int n = -1;
        for (ListParser p(list); p.parse();) {
            ++n;
            cs_set_iter(*id, cs_dup_ostr(p.item), stack);
            if (cs.run_bool(body)) {
                if (p.parse()) {
                    auto elem = p.element();
                    auto er = elem.iter();
                    elem.disown();
                    cs.result->set_str(er);
                }
                break;
            }
            if (!p.parse())
                break;
        }
        if (n >= 0)
            id->pop_arg();
    });

#define CS_CMD_LIST_FIND(name, fmt, type, init, cmp) \
    cs.add_command(name, "s" fmt "i", [](CsState &cs, char *list, \
                                         type *val, int *skip) { \
        int n = 0; \
        init; \
        for (ListParser p(list); p.parse(); ++n) { \
            if (cmp) { \
                cs.result->set_int(n); \
                return; \
            } \
            for (int i = 0; i < *skip; ++i) { \
                if (!p.parse()) \
                    goto notfound; \
                ++n; \
            } \
        } \
    notfound: \
        cs.result->set_int(-1); \
    });

    CS_CMD_LIST_FIND("listfind=", "i", int, {}, cs_parse_int(p.item) == *val);
    CS_CMD_LIST_FIND("listfind=f", "f", float, {}, cs_parse_float(p.item) == *val);
    CS_CMD_LIST_FIND("listfind=s", "s", char, ostd::Size len = strlen(val),
        p.item == ostd::ConstCharRange(val, len));

#undef CS_CMD_LIST_FIND

#define CS_CMD_LIST_ASSOC(name, fmt, type, init, cmp) \
    cs.add_command(name, "s" fmt, [](CsState &cs, char *list, type *val) { \
        init; \
        for (ListParser p(list); p.parse();) { \
            if (cmp) { \
                if (p.parse()) { \
                    auto elem = p.element(); \
                    auto er = elem.iter(); \
                    elem.disown(); \
                    cs.result->set_str(er); \
                } \
                return; \
            } \
            if (!p.parse()) \
                break; \
        } \
    });

    CS_CMD_LIST_ASSOC("listassoc=", "i", int, {}, cs_parse_int(p.item) == *val);
    CS_CMD_LIST_ASSOC("listassoc=f", "f", float, {}, cs_parse_float(p.item) == *val);
    CS_CMD_LIST_ASSOC("listassoc=s", "s", char, ostd::Size len = strlen(val),
        p.item == ostd::ConstCharRange(val, len));

#undef CS_CMD_LIST_ASSOC

    cs.add_command("looplist", "rse", [](CsState &cs, Ident *id,
                                         const char *list,
                                         const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS)
            return;
        IdentStack stack;
        int n = 0;
        for (ListParser p(list); p.parse(); ++n) {
            cs_set_iter(*id, p.element().disown(), stack);
            cs.run_int(body);
        }
        if (n >= 0)
            id->pop_arg();
    });

    cs.add_command("looplist2", "rrse", [](CsState &cs, Ident *id,
                                           Ident *id2, const char *list,
                                           const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS || id2->type != ID_ALIAS)
            return;
        IdentStack stack, stack2;
        int n = 0;
        for (ListParser p(list); p.parse(); n += 2) {
            cs_set_iter(*id, p.element().disown(), stack);
            cs_set_iter(*id2, p.parse() ? p.element().disown()
                                        : cs_dup_ostr(""), stack2);
            cs.run_int(body);
        }
        if (n >= 0) {
            id->pop_arg();
            id2->pop_arg();
        }
    });

    cs.add_command("looplist3", "rrrse", [](CsState &cs, Ident *id,
                                            Ident *id2, Ident *id3,
                                            const char *list,
                                            const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS)
            return;
        if (id2->type != ID_ALIAS || id3->type != ID_ALIAS)
            return;
        IdentStack stack, stack2, stack3;
        int n = 0;
        for (ListParser p(list); p.parse(); n += 3) {
            cs_set_iter(*id, p.element().disown(), stack);
            cs_set_iter(*id2, p.parse() ? p.element().disown()
                                        : cs_dup_ostr(""), stack2);
            cs_set_iter(*id3, p.parse() ? p.element().disown()
                                        : cs_dup_ostr(""), stack3);
            cs.run_int(body);
        }
        if (n >= 0) {
            id->pop_arg();
            id2->pop_arg();
            id3->pop_arg();
        }
    });

    cs.add_command("looplistconcat", "rse", [](CsState &cs, Ident *id,
                                               char *list,
                                               ostd::Uint32 *body) {
        cs_loop_list_conc(cs, id, list, body, true);
    });

    cs.add_command("looplistconcatword", "rse", [](CsState &cs, Ident *id,
                                                   char *list,
                                                   ostd::Uint32 *body) {
        cs_loop_list_conc(cs, id, list, body, false);
    });

    cs.add_command("listfilter", "rse", [](CsState &cs, Ident *id,
                                           const char *list,
                                           const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS)
            return;
        IdentStack stack;
        ostd::Vector<char> r;
        int n = 0;
        for (ListParser p(list); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            cs_set_iter(*id, val, stack);
            if (cs.run_bool(body)) {
                if (r.size()) r.push(' ');
                r.push_n(p.quote.data(), p.quote.size());
            }
        }
        if (n >= 0)
            id->pop_arg();
        r.push('\0');
        ostd::Size len = r.size() - 1;
        cs.result->set_str(ostd::CharRange(r.disown(), len));
    });

    cs.add_command("listcount", "rse", [](CsState &cs, Ident *id,
                                          const char *list,
                                          const ostd::Uint32 *body) {
        if (id->type != ID_ALIAS)
            return;
        IdentStack stack;
        int n = 0, r = 0;
        for (ListParser p(list); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            cs_set_iter(*id, val, stack);
            if (cs.run_bool(body))
                r++;
        }
        if (n >= 0)
            id->pop_arg();
        cs.result->set_int(r);
    });

    cs.add_command("prettylist", "ss", [](CsState &cs, const char *s,
                                          const char *conj) {
        ostd::Vector<char> buf;
        ostd::Size len = util::list_length(s);
        ostd::Size n = 0;
        for (ListParser p(s); p.parse(); ++n) {
            if (!p.quote.empty() && (p.quote.front() == '"')) {
                buf.reserve(buf.size() + p.item.size());
                auto writer = ostd::CharRange(&buf[buf.size()],
                    buf.capacity() - buf.size());
                ostd::Size adv = util::unescape_string(writer, p.item);
                writer.put('\0');
                buf.advance(adv);
            } else
                buf.push_n(p.item.data(), p.item.size());
            if ((n + 1) < len) {
                if ((len > 2) || !conj[0])
                    buf.push(',');
                if ((n + 2 == len) && conj[0]) {
                    buf.push(' ');
                    buf.push_n(conj, strlen(conj));
                }
                buf.push(' ');
            }
        }
        buf.push('\0');
        ostd::Size slen = buf.size() - 1;
        cs.result->set_str(ostd::CharRange(buf.disown(), slen));
    });

    cs.add_command("indexof", "ss", [](CsState &cs, char *list, char *elem) {
        cs.result->set_int(cs_list_includes(list, elem));
    });

#define CS_CMD_LIST_MERGE(name, init, iter, filter, dir) \
    cs.add_command(name, "ss", [](CsState &cs, const char *list, \
                                  const char *elems) { \
        ostd::Vector<char> buf; \
        init; \
        for (ListParser p(iter); p.parse();) { \
            if (cs_list_includes(filter, p.item) dir 0) { \
                if (!buf.empty()) \
                    buf.push(' '); \
                buf.push_n(p.quote.data(), p.quote.size()); \
            } \
        } \
        buf.push('\0'); \
        ostd::Size len = buf.size() - 1; \
        cs.result->set_str(ostd::CharRange(buf.disown(), len)); \
    });

    CS_CMD_LIST_MERGE("listdel", {}, list, elems, <);
    CS_CMD_LIST_MERGE("listintersect", {}, list, elems, >=);
    CS_CMD_LIST_MERGE("listunion", buf.push_n(list, strlen(list)), elems,
        list, <);

#undef CS_CMD_LIST_MERGE

    cs.add_command("listsplice", "ssii", [](CsState &cs, const char *s,
                                            const char *vals, int *skip,
                                            int *count) {
        int offset = ostd::max(*skip, 0);
        int len    = ostd::max(*count, 0);
        const char *list = s;
        ListParser p(s);
        for (int i = 0; i < offset; ++i)
            if (!p.parse())
                break;
        const char *qend = !p.quote.empty() ? &p.quote[p.quote.size()] : list;
        ostd::Vector<char> buf;
        if (qend > list)
            buf.push_n(list, qend - list);
        if (*vals) {
            if (!buf.empty())
                buf.push(' ');
            buf.push_n(vals, strlen(vals));
        }
        for (int i = 0; i < len; ++i)
            if (!p.parse())
                break;
        p.skip();
        if (!p.input.empty()) switch (p.input.front()) {
        case ')':
        case ']':
            break;
        default:
            if (!buf.empty())
                buf.push(' ');
            buf.push_n(p.input.data(), p.input.size());
            break;
        }
        buf.push('\0');
        ostd::Size slen = buf.size() - 1;
        cs.result->set_str(ostd::CharRange(buf.disown(), slen));
    });

    cs_init_lib_list_sort(cs);
}

struct ListSortItem {
    const char *str;
    ostd::ConstCharRange quote;
};

struct ListSortFun {
    CsState &cs;
    Ident *x, *y;
    ostd::Uint32 *body;

    bool operator()(const ListSortItem &xval, const ListSortItem &yval) {
        x->clean_code();
        if (x->get_valtype() != VAL_CSTR)
            x->valtype = VAL_CSTR | (strlen(xval.str) << 4);
        x->val.cstr = xval.str;
        y->clean_code();
        if (y->get_valtype() != VAL_CSTR)
            y->valtype = VAL_CSTR | (strlen(xval.str) << 4);
        y->val.cstr = yval.str;
        return cs.run_bool(body);
    }
};

void cs_list_sort(CsState &cs, char *list, Ident *x, Ident *y,
                  ostd::Uint32 *body, ostd::Uint32 *unique) {
    if (x == y || x->type != ID_ALIAS || y->type != ID_ALIAS)
        return;

    ostd::Vector<ListSortItem> items;
    ostd::Size clen = strlen(list);
    ostd::Size total = 0;

    char *cstr = cs_dup_ostr(ostd::ConstCharRange(list, clen));
    for (ListParser p(list); p.parse();) {
        cstr[&p.item[p.item.size()] - list] = '\0';
        ListSortItem item = { &cstr[p.item.data() - list], p.quote };
        items.push(item);
        total += item.quote.size();
    }

    if (items.empty()) {
        cs.result->set_str(cstr);
        return;
    }

    IdentStack xstack, ystack;
    x->push_arg(null_value, xstack);
    y->push_arg(null_value, ystack);

    ostd::Size totaluniq = total;
    ostd::Size nuniq = items.size();
    if (body) {
        ListSortFun f = { cs, x, y, body };
        ostd::sort(items.iter(), f);
        if ((*unique & CODE_OP_MASK) != CODE_EXIT) {
            f.body = unique;
            totaluniq = items[0].quote.size();
            nuniq = 1;
            for (ostd::Size i = 1; i < items.size(); i++) {
                ListSortItem &item = items[i];
                if (f(items[i - 1], item))
                    item.quote = nullptr;
                else {
                    totaluniq += item.quote.size();
                    ++nuniq;
                }
            }
        }
    } else {
        ListSortFun f = { cs, x, y, unique };
        totaluniq = items[0].quote.size();
        nuniq = 1;
        for (ostd::Size i = 1; i < items.size(); i++) {
            ListSortItem &item = items[i];
            for (ostd::Size j = 0; j < i; ++j) {
                ListSortItem &prev = items[j];
                if (!prev.quote.empty() && f(item, prev)) {
                    item.quote = nullptr;
                    break;
                }
            }
            if (!item.quote.empty()) {
                totaluniq += item.quote.size();
                ++nuniq;
            }
        }
    }

    x->pop_arg();
    y->pop_arg();

    char *sorted = cstr;
    ostd::Size sortedlen = totaluniq + ostd::max(nuniq - 1, ostd::Size(0));
    if (clen < sortedlen) {
        delete[] cstr;
        sorted = new char[sortedlen + 1];
    }

    ostd::Size offset = 0;
    for (ostd::Size i = 0; i < items.size(); ++i) {
        ListSortItem &item = items[i];
        if (item.quote.empty())
            continue;
        if (i)
            sorted[offset++] = ' ';
        memcpy(&sorted[offset], item.quote.data(), item.quote.size());
        offset += item.quote.size();
    }
    sorted[offset] = '\0';

    cs.result->set_str(sorted);
}

static void cs_init_lib_list_sort(CsState &cs) {
    cs.add_command("sortlist", "srree", cs_list_sort);
    cs.add_command("uniquelist", "srre", [](CsState &cs, char *list,
                                            Ident *x, Ident *y,
                                            ostd::Uint32 *body) {
        cs_list_sort(cs, list, x, y, nullptr, body);
    });
}

static constexpr float PI = 3.14159265358979f;
static constexpr float RAD = PI / 180.0f;

void init_lib_math(CsState &cs) {
    cs.add_command("sin", "f", [](CsState &cs, float *a) {
        cs.result->set_float(sin(*a * RAD));
    });
    cs.add_command("cos", "f", [](CsState &cs, float *a) {
        cs.result->set_float(cos(*a * RAD));
    });
    cs.add_command("tan", "f", [](CsState &cs, float *a) {
        cs.result->set_float(tan(*a * RAD));
    });

    cs.add_command("asin", "f", [](CsState &cs, float *a) {
        cs.result->set_float(asin(*a) / RAD);
    });
    cs.add_command("acos", "f", [](CsState &cs, float *a) {
        cs.result->set_float(acos(*a) / RAD);
    });
    cs.add_command("atan", "f", [](CsState &cs, float *a) {
        cs.result->set_float(atan(*a) / RAD);
    });
    cs.add_command("atan2", "ff", [](CsState &cs, float *y, float *x) {
        cs.result->set_float(atan2(*y, *x) / RAD);
    });

    cs.add_command("sqrt", "f", [](CsState &cs, float *a) {
        cs.result->set_float(sqrt(*a));
    });
    cs.add_command("loge", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log(*a));
    });
    cs.add_command("log2", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log(*a) / M_LN2);
    });
    cs.add_command("log10", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log10(*a));
    });

    cs.add_command("exp", "f", [](CsState &cs, float *a) {
        cs.result->set_float(exp(*a));
    });

#define CS_CMD_MIN_MAX(name, fmt, type, op) \
    cs.add_command(#name, #fmt "1V", [](CsState &cs, \
                                        ostd::PointerRange<TaggedValue> args) { \
        type v = !args.empty() ? args[0].fmt : 0; \
        for (ostd::Size i = 1; i < args.size(); ++i) v = op(v, args[i].fmt); \
        cs.result->set_##type(v); \
    })

    CS_CMD_MIN_MAX(min, i, int, ostd::min);
    CS_CMD_MIN_MAX(max, i, int, ostd::max);
    CS_CMD_MIN_MAX(minf, f, float, ostd::min);
    CS_CMD_MIN_MAX(maxf, f, float, ostd::max);

#undef CS_CMD_MIN_MAX

    cs.add_command("abs", "i", [](CsState &cs, int *v) {
        cs.result->set_int(abs(*v));
    });
    cs.add_command("absf", "f", [](CsState &cs, float *v) {
        cs.result->set_float(fabs(*v));
    });

    cs.add_command("floor", "f", [](CsState &cs, float *v) {
        cs.result->set_float(floor(*v));
    });
    cs.add_command("ceil", "f", [](CsState &cs, float *v) {
        cs.result->set_float(ceil(*v));
    });

    cs.add_command("round", "ff", [](CsState &cs, float *n, float *k) {
        double step = *k;
        double r = *n;
        if (step > 0) {
            r += step * ((r < 0) ? -0.5 : 0.5);
            r -= fmod(r, step);
        } else {
            r = (r < 0) ? ceil(r - 0.5) : floor(r + 0.5);
        }
        cs.result->set_float(float(r));
    });

#define CS_CMD_MATH(name, fmt, type, op, initval, unaryop) \
    cs.add_command(name, #fmt "1V", [](CsState &, \
                                       ostd::PointerRange<TaggedValue> args) { \
        type val; \
        if (args.size() >= 2) { \
            val = args[0].fmt; \
            type val2 = args[1].fmt; \
            op; \
            for (ostd::Size i = 2; i < args.size(); ++i) { \
                val2 = args[i].fmt; \
                op; \
            } \
        } else { \
            val = (args.size() > 0) ? args[0].fmt : initval; \
            unaryop; \
        } \
    });

#define CS_CMD_MATHIN(name, op, initval, unaryop) \
    CS_CMD_MATH(#name, i, int, val = val op val2, initval, unaryop)

#define CS_CMD_MATHI(name, initval, unaryop) \
    CS_CMD_MATHIN(name, name, initval, unaryop)

#define CS_CMD_MATHFN(name, op, initval, unaryop) \
    CS_CMD_MATH(#name "f", f, float, val = val op val2, initval, unaryop)

#define CS_CMD_MATHF(name, initval, unaryop) \
    CS_CMD_MATHFN(name, name, initval, unaryop)

    CS_CMD_MATHI(+, 0, {});
    CS_CMD_MATHI(*, 1, {});
    CS_CMD_MATHI(-, 0, val = -val);

    CS_CMD_MATHI(^, 0, val = ~val);
    CS_CMD_MATHIN(~, ^, 0, val = ~val);
    CS_CMD_MATHI(&, 0, {});
    CS_CMD_MATHI(|, 0, {});
    CS_CMD_MATHI(^~, 0, {});
    CS_CMD_MATHI(&~, 0, {});
    CS_CMD_MATHI(|~, 0, {});

    CS_CMD_MATH("<<", i, int, {
        val = (val2 < 32) ? (val << ostd::max(val2, 0)) : 0;
    }, 0, {});
    CS_CMD_MATH(">>", i, int, val >>= ostd::clamp(val2, 0, 31), 0, {});

    CS_CMD_MATHF(+, 0, {});
    CS_CMD_MATHF(*, 1, {});
    CS_CMD_MATHF(-, 0, val = -val);

#define CS_CMD_DIV(name, fmt, type, op) \
    CS_CMD_MATH(#name, fmt, type, { if (val2) op; else val = 0; }, 0, {})

    CS_CMD_DIV(div, i, int, val /= val2);
    CS_CMD_DIV(mod, i, int, val %= val2);
    CS_CMD_DIV(divf, f, float, val /= val2);
    CS_CMD_DIV(modf, f, float, val = fmod(val, val2));

#undef CS_CMD_DIV

    CS_CMD_MATH("pow", f, float, val = pow(val, val2), 0, {});

#undef CS_CMD_MATHF
#undef CS_CMD_MATHFN
#undef CS_CMD_MATHI
#undef CS_CMD_MATHIN
#undef CS_CMD_MATH

#define CS_CMD_CMP(name, fmt, type, op) \
    cs.add_command(name, #fmt "1V", [](CsState &cs, \
                                       ostd::PointerRange<TaggedValue> args) { \
        bool val; \
        if (args.size() >= 2) { \
            val = args[0].fmt op args[1].fmt; \
            for (ostd::Size i = 2; i < args.size() && val; ++i) \
                val = args[i-1].fmt op args[i].fmt; \
        } else \
            val = ((args.size() > 0) ? args[0].fmt : 0) op 0; \
        cs.result->set_int(int(val)); \
    })

#define CS_CMD_CMPIN(name, op) CS_CMD_CMP(#name, i, int, op)
#define CS_CMD_CMPI(name) CS_CMD_CMPIN(name, name)
#define CS_CMD_CMPFN(name, op) CS_CMD_CMP(#name "f", f, float, op)
#define CS_CMD_CMPF(name) CS_CMD_CMPFN(name, name)

    CS_CMD_CMPIN(=, ==);
    CS_CMD_CMPI(!=);
    CS_CMD_CMPI(<);
    CS_CMD_CMPI(>);
    CS_CMD_CMPI(<=);
    CS_CMD_CMPI(>=);

    CS_CMD_CMPFN(=, ==);
    CS_CMD_CMPF(!=);
    CS_CMD_CMPF(<);
    CS_CMD_CMPF(>);
    CS_CMD_CMPF(<=);
    CS_CMD_CMPF(>=);

#undef CS_CMD_CMPF
#undef CS_CMD_CMPFN
#undef CS_CMD_CMPI
#undef CS_CMD_CMPIN
#undef CS_CMD_CMP
}

void init_lib_string(CsState &cs) {
    cs.add_command("strstr", "ss", [](CsState &cs, char *a, char *b) {
        char *s = strstr(a, b);
        cs.result->set_int(s ? (s - a) : -1);
    });

    cs.add_command("strlen", "s", [](CsState &cs, char *s) {
        cs.result->set_int(strlen(s));
    });

    cs.add_command("strcode", "si", [](CsState &cs, char *s, int *i) {
        cs.result->set_int((*i > 0)
                           ? (memchr(s, '\0', *i) ? 0 : ostd::byte(s[*i]))
                           : ostd::byte(s[0]));
    });

    cs.add_command("codestr", "i", [](CsState &cs, int *i) {
        char *s = new char[2];
        s[0] = char(*i);
        s[1] = '\0';
        cs.result->set_str(s);
    });

    cs.add_command("strlower", "s", [](CsState &cs, char *s) {
        ostd::Size len = strlen(s);
        char *buf = new char[len + 1];
        for (ostd::Size i = 0; i < len; ++i)
            buf[i] = tolower(s[i]);
        buf[len] = '\0';
        cs.result->set_str(ostd::CharRange(buf, len));
    });

    cs.add_command("strupper", "s", [](CsState &cs, char *s) {
        ostd::Size len = strlen(s);
        char *buf = new char[len + 1];
        for (ostd::Size i = 0; i < len; ++i)
            buf[i] = toupper(s[i]);
        buf[len] = '\0';
        cs.result->set_str(ostd::CharRange(buf, len));
    });

    cs.add_command("escape", "s", [](CsState &cs, char *s) {
        auto x = ostd::appender<ostd::String>();
        util::escape_string(x, s);
        ostd::Size len = x.size();
        cs.result->set_str(ostd::CharRange(x.get().disown(), len));
    });

    cs.add_command("unescape", "s", [](CsState &cs, char *s) {
        ostd::Size len = strlen(s);
        char *buf = new char[len + 1];
        auto writer = ostd::CharRange(buf, len + 1);
        util::unescape_string(writer, ostd::ConstCharRange(s, len));
        writer.put('\0');
        cs.result->set_str(ostd::CharRange(buf, len));
    });

    cs.add_command("concat", "V", [](CsState &cs,
                                     ostd::PointerRange<TaggedValue> args) {
        cs.result->set_str(conc(args, true));
    });

    cs.add_command("concatworld", "V", [](CsState &cs,
                                          ostd::PointerRange<TaggedValue> args) {
        cs.result->set_str(conc(args, false));
    });

    cs.add_command("format", "V", [](CsState &cs,
                                     ostd::PointerRange<TaggedValue> args) {
        if (args.empty())
            return;
        ostd::Vector<char> s;
        ostd::ConstCharRange f = args[0].get_str();
        while (!f.empty()) {
            char c = f.front();
            f.pop_front();
            if ((c == '%') && !f.empty()) {
                char ic = f.front();
                f.pop_front();
                if (ic >= '1' && ic <= '9') {
                    int i = ic - '0';
                    ostd::ConstCharRange sub = (i < int(args.size()))
                        ? args[i].get_str() : "";
                    s.push_n(sub.data(), sub.size());
                } else s.push(ic);
            } else s.push(c);
        }
        s.push('\0');
        ostd::Size len = s.size() - 1;
        cs.result->set_str(ostd::CharRange(s.disown(), len));
    });

    cs.add_command("tohex", "ii", [](CsState &cs, int *n, int *p) {
        auto r = ostd::appender<ostd::Vector<char>>();
        ostd::format(r, "0x%.*X", ostd::max(*p, 1), *n);
        r.put('\0');
        ostd::Size len = r.size() - 1;
        cs.result->set_str(ostd::CharRange(r.get().disown(), len));
    });

    cs.add_command("substr", "siiN", [](CsState &cs, char *s, int *start,
                                        int *count, int *numargs) {
        int len = strlen(s), offset = ostd::clamp(*start, 0, len);
        cs.result->set_str_dup(ostd::ConstCharRange(
            &s[offset],
            (*numargs >= 3) ? ostd::clamp(*count, 0, len - offset)
                            : (len - offset)));
    });

#define CS_CMD_CMPS(name, op) \
    cs.add_command(#name, "s1V", [](CsState &cs, \
                                    ostd::PointerRange<TaggedValue> args) { \
        bool val; \
        if (args.size() >= 2) { \
            val = strcmp(args[0].s, args[1].s) op 0; \
            for (ostd::Size i = 2; i < args.size() && val; ++i) \
                val = strcmp(args[i-1].s, args[i].s) op 0; \
        } else \
            val = (!args.empty() ? args[0].s[0] : 0) op 0; \
        cs.result->set_int(int(val)); \
    })

    CS_CMD_CMPS(strcmp, ==);
    CS_CMD_CMPS(=s, ==);
    CS_CMD_CMPS(!=s, !=);
    CS_CMD_CMPS(<s, <);
    CS_CMD_CMPS(>s, >);
    CS_CMD_CMPS(<=s, <=);
    CS_CMD_CMPS(>=s, >=);

#undef CS_CMD_CMPS

    cs.add_command("strreplace", "ssss", [](CsState &cs, const char *s,
                                            const char *oldval,
                                            const char *newval,
                                            const char *newval2) {
        if (!newval2[0]) newval2 = newval;
        ostd::Vector<char> buf;
        int oldlen = strlen(oldval);
        if (!oldlen) {
            cs.result->set_str_dup(s);
            return;
        }
        for (int i = 0;; ++i) {
            const char *found = strstr(s, oldval);
            if (found) {
                while (s < found) buf.push(*s++);
                for (const char *n = (i & 1) ? newval2 : newval; *n; ++n)
                    buf.push(*n);
                s = found + oldlen;
            } else {
                while (*s)
                    buf.push(*s++);
                buf.push('\0');
                ostd::Size len = buf.size() - 1;
                cs.result->set_str(ostd::CharRange(buf.disown(), len));
                return;
            }
        }
    });

    cs.add_command("strsplice", "ssii", [](CsState &cs, const char *s,
                                           const char *vals, int *skip,
                                           int *count) {
        int slen = strlen(s),
            vlen = strlen(vals),
            offset = ostd::clamp(*skip, 0, slen),
            len    = ostd::clamp(*count, 0, slen - offset);
        char *p = new char[slen - len + vlen + 1];
        if (offset)
            memcpy(p, s, offset);
        if (vlen)
            memcpy(&p[offset], vals, vlen);
        if (offset + len < slen)
            memcpy(&p[offset + vlen], &s[offset + len], slen - (offset + len));
        p[slen - len + vlen] = '\0';
        cs.result->set_str(ostd::CharRange(p, slen - len + vlen));
    });
}

} /* namespace cscript */