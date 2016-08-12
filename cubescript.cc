#include "cubescript.hh"

#include <limits.h>
#include <ctype.h>
#include <math.h>

#include <ostd/algorithm.hh>
#include <ostd/format.hh>
#include <ostd/array.hh>
#include <ostd/memory.hh>

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

enum {
    ID_UNKNOWN = -1, ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_RESULT, ID_NOT, ID_AND, ID_OR
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
    CODE_COM, CODE_COMC, CODE_COMV,
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

static inline int parseint(char const *s) {
    return int(strtoul(s, nullptr, 0));
}

int cs_parse_int(ostd::ConstCharRange s) {
    if (s.empty()) return 0;
    return parseint(s.data());
}

static inline float parsefloat(char const *s) {
    /* not all platforms (windows) can parse hexadecimal integers via strtod */
    char *end;
    double val = strtod(s, &end);
    return val || end==s || (*end!='x' && *end!='X') ? float(val) : float(parseint(s));
}

float cs_parse_float(ostd::ConstCharRange s) {
    if (s.empty()) return 0.0f;
    return parsefloat(s.data());
}

ostd::String intstr(int v) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

ostd::String floatstr(float v) {
    char buf[256];
    snprintf(buf, sizeof(buf), v == int(v) ? "%.1f" : "%.7g", v);
    return buf;
}

char *cs_dup_ostr(ostd::ConstCharRange s) {
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

Ident::Ident(): type(ID_UNKNOWN) {}

/* ID_VAR */
Ident::Ident(ostd::ConstCharRange n, int m, int x, int *s,
             VarCb f, int flagsv)
    : type(ID_VAR), flags(flagsv | (m > x ? IDF_READONLY : 0)), name(n),
      minval(m), maxval(x), cb_var(ostd::move(f)) {
    storage.ip = s;
}

/* ID_FVAR */
Ident::Ident(ostd::ConstCharRange n, float m, float x, float *s,
             VarCb f, int flagsv)
    : type(ID_FVAR), flags(flagsv | (m > x ? IDF_READONLY : 0)), name(n),
      minvalf(m), maxvalf(x), cb_var(ostd::move(f)) {
    storage.fp = s;
}

/* ID_SVAR */
Ident::Ident(ostd::ConstCharRange n, char **s, VarCb f, int flagsv)
    : type(ID_SVAR), flags(flagsv), name(n), cb_var(ostd::move(f)) {
    storage.sp = s;
}

/* ID_ALIAS */
Ident::Ident(ostd::ConstCharRange n, char *a, int flagsv)
    : type(ID_ALIAS), valtype(VAL_STR), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
    val.s = a;
    val.len = strlen(a);
}
Ident::Ident(ostd::ConstCharRange n, int a, int flagsv)
    : type(ID_ALIAS), valtype(VAL_INT), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
    val.i = a;
}
Ident::Ident(ostd::ConstCharRange n, float a, int flagsv)
    : type(ID_ALIAS), valtype(VAL_FLOAT), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
    val.f = a;
}
Ident::Ident(ostd::ConstCharRange n, int flagsv)
    : type(ID_ALIAS), valtype(VAL_NULL), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
}
Ident::Ident(ostd::ConstCharRange n, TaggedValue const &v, int flagsv)
    : type(ID_ALIAS), valtype(v.p_type), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
    val = v;
}

/* ID_COMMAND */
Ident::Ident(int t, ostd::ConstCharRange n, ostd::ConstCharRange args,
             ostd::Uint32 argmask, int numargs, CmdFunc f)
    : type(t), numargs(numargs), flags(0), name(n),
      cargs(!args.empty() ? cs_dup_ostr(args) : nullptr),
      argmask(argmask), cb_cftv(ostd::move(f)) {
}

struct NullValue: TaggedValue {
    NullValue() { set_null(); }
} const null_value;

static TaggedValue no_ret = null_value;

void cs_init_lib_base(CsState &cs);

CsState::CsState() {
    noalias.id = nullptr;
    noalias.next = nullptr;
    noalias.usedargs = (1 << MaxArguments) - 1;
    noalias.argstack = nullptr;
    for (int i = 0; i < MaxArguments; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident(static_cast<char const *>(buf), IDF_ARG);
    }
    dummy = new_ident("//dummy");
    add_ident("numargs", MaxArguments, 0, &numargs);
    add_ident("dbgalias", 0, 1000, &dbgalias);
    cs_init_lib_base(*this);
}

CsState::~CsState() {
    for (Ident &i: idents.iter()) {
        if (i.type == ID_ALIAS) {
            i.force_null();
            delete[] reinterpret_cast<ostd::Uint32 *>(i.code);
            i.code = nullptr;
        } else if (i.type == ID_COMMAND || i.type >= ID_LOCAL) {
            delete[] i.cargs;
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
        id.val.len = 0;
        break;
    case ID_VAR:
        *id.storage.ip = id.overrideval.i;
        id.changed();
        break;
    case ID_FVAR:
        *id.storage.fp = id.overrideval.f;
        id.changed();
        break;
    case ID_SVAR:
        delete[] *id.storage.sp;
        *id.storage.sp = id.overrideval.s;
        id.changed();
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
        id = add_ident(name, flags);
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
        id->changed();
        break;
    }
}

void CsState::set_alias(ostd::ConstCharRange name, TaggedValue &v) {
    Ident *id = idents.at(name);
    if (id) {
        switch (id->type) {
        case ID_ALIAS:
            if (id->index < MaxArguments)
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
        add_ident(name, v, identflags);
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

void TaggedValue::cleanup() {
    switch (get_type()) {
    case VAL_STR:
        delete[] s;
        break;
    case VAL_CODE:
        ostd::Uint32 *bcode = const_cast<ostd::Uint32 *>(
            reinterpret_cast<ostd::Uint32 const *>(code)
        );
        if (bcode[-1] == CODE_START) {
            delete[] bcode;
        }
        break;
    }
}

static ostd::Uint32 const *skipcode(ostd::Uint32 const *code, TaggedValue *result = nullptr);

void TaggedValue::copy_arg(TaggedValue &r) const {
    r.cleanup();
    switch (get_type()) {
        case VAL_INT:
        case VAL_FLOAT:
        case VAL_IDENT:
            r = *this;
            break;
        case VAL_STR:
        case VAL_CSTR:
        case VAL_MACRO:
            r.set_str(ostd::ConstCharRange(s, len));
            break;
        case VAL_CODE: {
            ostd::Uint32 const *bcode = reinterpret_cast<ostd::Uint32 const *>(code);
            ostd::Uint32 const *end = skipcode(bcode);
            ostd::Uint32 *dst = new ostd::Uint32[end - bcode + 1];
            *dst++ = CODE_START;
            memcpy(dst, bcode, (end - bcode) * sizeof(ostd::Uint32));
            r.set_code(reinterpret_cast<Bytecode const *>(dst));
            break;
        }
        default:
            r.set_null();
            break;
    }
}

/* XXX: nasty */
struct InternalTval: IdentValue {
    int type;
};

static inline void cs_set_macro(
    TaggedValue &tv, Bytecode const *val, ostd::Size len
) {
    InternalTval &itv = reinterpret_cast<InternalTval &>(tv);
    itv.type = VAL_MACRO;
    itv.len = len;
    itv.code = val;
}

void TaggedValue::force_null() {
    if (get_type() == VAL_NULL) return;
    cleanup();
    set_null();
}

float TaggedValue::force_float() {
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

int TaggedValue::force_int() {
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

ostd::ConstCharRange TaggedValue::force_str() {
    ostd::String rs;
    switch (get_type()) {
    case VAL_FLOAT:
        rs = ostd::move(floatstr(f));
        break;
    case VAL_INT:
        rs = ostd::move(intstr(i));
        break;
    case VAL_MACRO:
    case VAL_CSTR:
        rs = ostd::ConstCharRange(s, len);
        break;
    case VAL_STR:
        return s;
    }
    cleanup();
    set_str(ostd::move(rs));
    return s;
}

static inline void force_arg(TaggedValue &v, int type) {
    switch (type) {
    case RET_STR:
        if (v.get_type() != VAL_STR) v.force_str();
        break;
    case RET_INT:
        if (v.get_type() != VAL_INT) v.force_int();
        break;
    case RET_FLOAT:
        if (v.get_type() != VAL_FLOAT) v.force_float();
        break;
    }
}

static inline int cs_get_int(IdentValue const &v, int type) {
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

int TaggedValue::get_int() const {
    return cs_get_int(*this, get_type());
}

int Ident::get_int() const {
    return cs_get_int(val, get_valtype());
}

static inline float cs_get_float(IdentValue const &v, int type) {
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

float TaggedValue::get_float() const {
    return cs_get_float(*this, get_type());
}

float Ident::get_float() const {
    return cs_get_float(val, get_valtype());
}

Bytecode *TaggedValue::get_code() const {
    if (get_type() != VAL_CODE)
        return nullptr;
    return const_cast<Bytecode *>(code);
}

Ident *TaggedValue::get_ident() const {
    if (get_type() != VAL_IDENT)
        return nullptr;
    return id;
}

static inline ostd::String cs_get_str(IdentValue const &v, int type) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return ostd::ConstCharRange(v.s, v.len);
    case VAL_INT:
        return intstr(v.i);
    case VAL_FLOAT:
        return floatstr(v.f);
    }
    return ostd::String("");
}

ostd::String TaggedValue::get_str() const {
    return cs_get_str(*this, get_type());
}

ostd::String Ident::get_str() const {
    return cs_get_str(val, get_valtype());
}

static inline ostd::ConstCharRange cs_get_strr(IdentValue const &v, int type) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return ostd::ConstCharRange(v.s, v.len);
    default:
        break;
    }
    return ostd::ConstCharRange();
}

ostd::ConstCharRange TaggedValue::get_strr() const {
    return cs_get_strr(*this, get_type());
}

ostd::ConstCharRange Ident::get_strr() const {
    return cs_get_strr(val, get_valtype());
}

static inline void cs_get_val(IdentValue const &v, int type, TaggedValue &r) {
    switch (type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR: {
        r.set_str(ostd::ConstCharRange(v.s, v.len));
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

void TaggedValue::get_val(TaggedValue &r) const {
    cs_get_val(*this, get_type(), r);
}

void Ident::get_val(TaggedValue &r) const {
    cs_get_val(val, get_valtype(), r);
}

void Ident::get_cstr(TaggedValue &v) const {
    switch (get_valtype()) {
    case VAL_MACRO:
        cs_set_macro(v, val.code, val.len);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(ostd::ConstCharRange(val.s, val.len));
        break;
    case VAL_INT:
        v.set_str(ostd::move(intstr(val.i)));
        break;
    case VAL_FLOAT:
        v.set_str(ostd::move(floatstr(val.f)));
        break;
    default:
        v.set_cstr("");
        break;
    }
}

void Ident::get_cval(TaggedValue &v) const {
    switch (get_valtype()) {
    case VAL_MACRO:
        cs_set_macro(v, val.code, val.len);
        break;
    case VAL_STR:
    case VAL_CSTR:
        v.set_cstr(ostd::ConstCharRange(val.s, val.len));
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
    ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(code);
    if (bcode) {
        bcode[0] -= 0x100;
        if (int(bcode[0]) < 0x100) delete[] bcode;
        code = nullptr;
    }
}

void Ident::push_arg(TaggedValue const &v, IdentStack &st, bool um) {
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

void Ident::redo_arg(IdentStack const &st) {
    IdentStack *prev = st.next;
    prev->val = val;
    prev->valtype = valtype;
    stack = prev;
    set_value(st);
    clean_code();
}

void Ident::push_alias(IdentStack &stack) {
    if (type == ID_ALIAS && index >= MaxArguments)
        push_arg(null_value, stack);
}

void Ident::pop_alias() {
    if (type == ID_ALIAS && index >= MaxArguments) pop_arg();
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

IdentType Ident::get_type() const {
    if (type > ID_ALIAS) {
        return IdentType::unknown;
    }
    return IdentType(type);
}

template<typename F>
static void cs_do_args(CsState &cs, F body) {
    IdentStack argstack[MaxArguments];
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
        id->changed();
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
        id->changed();
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
        id->changed();
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

ostd::Maybe<ostd::String>
CsState::get_alias(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_ALIAS)
        return ostd::nothing;
    if ((id->index < MaxArguments) && !(stack->usedargs & (1 << id->index)))
        return ostd::nothing;
    return ostd::move(id->get_str());
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
    id->changed();
}

void CsState::set_var_int_checked(Ident *id, TvalRange args) {
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
    id->changed();
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
    id->changed();
}

static bool cs_add_command(
    CsState &cs, ostd::ConstCharRange name, ostd::ConstCharRange args,
    CmdFunc func, int type = ID_COMMAND
) {
    ostd::Uint32 argmask = 0;
    int nargs = 0;
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
            if (nargs < MaxArguments) nargs++;
            break;
        case 'S':
        case 's':
        case 'e':
        case 'r':
        case '$':
            if (nargs < MaxArguments) {
                argmask |= 1 << nargs;
                nargs++;
            }
            break;
        case '1':
        case '2':
        case '3':
        case '4':
            if (nargs < MaxArguments)
                fmt.push_front_n(fmt.front() - '0' + 1);
            break;
        case 'C':
        case 'V':
            break;
        default:
            ostd::err.writefln("builtin %s declared with illegal type: %c",
                               name, fmt.front());
            return false;
        }
    }
    cs.add_ident(type, name, args, argmask, nargs, ostd::move(func));
    return true;
}

bool CsState::add_command(
    ostd::ConstCharRange name, ostd::ConstCharRange args, CmdFunc func
) {
    return cs_add_command(*this, name, args, ostd::move(func));
}

char const *parsestring(char const *p) {
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

static inline void skipcomments(char const *&p) {
    for (;;) {
        p += strspn(p, " \t\r");
        if (p[0] != '/' || p[1] != '/') break;
        p += strcspn(p, "\n\0");
    }
}

static inline char *cutstring(char const *&p) {
    p++;
    char const *end = parsestring(p);
    char *buf = new char[end - p + 1];
    auto writer = ostd::CharRange(buf, end - p + 1);
    util::unescape_string(writer, ostd::ConstCharRange(p, end));
    writer.put('\0');
    p = end;
    if (*p == '\"') p++;
    return buf;
}

char const *parseword(char const *p) {
    constexpr int maxbrak = 100;
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

static inline char *cutword(char const *&p) {
    char const *word = p;
    p = parseword(p);
    return p != word ? cs_dup_ostr(ostd::ConstCharRange(word, p - word)) : nullptr;
}

static inline int cs_ret_code(int type, int def = 0) {
    return (type >= VAL_ANY) ? ((type == VAL_CSTR) ? RET_STR : def)
                             : (type << CODE_RET);
}

struct GenState;

static void compilestatements(GenState &gs, int rettype, int brak = '\0', int prevargs = 0);
static inline char const *compileblock(GenState &gs, char const *p, int rettype = RET_NULL, int brak = '\0');

struct GenState {
    CsState &cs;
    ostd::Vector<ostd::Uint32> code;
    char const *source;

    GenState() = delete;
    GenState(CsState &csr): cs(csr), code(), source(nullptr) {}

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
        code.push_n(reinterpret_cast<ostd::Uint32 const *>(word.data()),
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
        code.push(((id->index < MaxArguments) ? CODE_IDENTARG
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

static inline char const *compileblock(GenState &gs, char const *p, int rettype, int brak) {
    ostd::Size start = gs.code.size();
    gs.code.push(CODE_BLOCK);
    gs.code.push(CODE_OFFSET | ((start + 2) << 8));
    if (p) {
        char const *op = gs.source;
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
    char const *end = parsestring(gs.source);
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + (end - gs.source) / sizeof(ostd::Uint32) + 1);
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
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

OSTD_EXPORT bool code_is_empty(Bytecode const *code) {
    if (!code) {
        return true;
    }
    return (*reinterpret_cast<
        ostd::Uint32 const *
    >(code) & CODE_OP_MASK) == CODE_EXIT;
}

bool TaggedValue::code_is_empty() const {
    if (get_type() != VAL_CODE) {
        return true;
    }
    return cscript::code_is_empty(code);
}

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

bool TaggedValue::get_bool() const {
    switch (get_type()) {
    case VAL_FLOAT:
        return f != 0;
    case VAL_INT:
        return i != 0;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return cs_get_bool(ostd::ConstCharRange(s, len));
    default:
        return false;
    }
}

static bool compilearg(GenState &gs, int wordtype, int prevargs = MaxResults, ostd::Box<char[]> *word = nullptr);

static void compilelookup(GenState &gs, int ltype, int prevargs = MaxResults) {
    ostd::Box<char[]> lookup;
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
        lookup = ostd::Box<char[]>(cutstring(gs.source));
        goto lookupid;
    default: {
        lookup = ostd::Box<char[]>(cutword(gs.source));
        if (!lookup) goto invalid;
lookupid:
        Ident *id = gs.cs.new_ident(lookup.get());
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
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                    break;
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | RET_STR | (id->index << 8));
                    break;
                default:
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPARG : CODE_LOOKUP) | cs_ret_code(ltype, RET_STR) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_COMMAND: {
                int comtype = CODE_COM, numargs = 0;
                if (prevargs >= MaxResults) gs.code.push(CODE_ENTER);
                for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
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
                gs.code.push((prevargs >= MaxResults ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
compilecomv:
                gs.code.push(comtype | cs_ret_code(ltype) | (numargs << 8) | (id->index << 13));
                gs.code.push((prevargs >= MaxResults ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
            }
            default:
                goto invalid;
            }
        gs.gen_str(lookup.get(), true);
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
    int startc = gs.code.size();
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1);
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
    int len = 0;
    while (!str.empty()) {
        char const *p = str.data();
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
    gs.code[startc] |= len << 8;
    return true;
}

static bool compileblocksub(GenState &gs, int prevargs) {
    ostd::Box<char[]> lookup;
    ostd::ConstCharRange lkup;
    char const *op;
    switch (gs.current()) {
    case '(':
        if (!compilearg(gs, VAL_CANY, prevargs)) return false;
        break;
    case '[':
        if (!compilearg(gs, VAL_CSTR, prevargs)) return false;
        gs.code.push(CODE_LOOKUPMU);
        break;
    case '\"':
        lookup = ostd::Box<char[]>(cutstring(gs.source));
        goto lookupid;
    default: {
        op = gs.source;
        while (isalnum(gs.current()) || gs.current() == '_') gs.next_char();
        lkup = ostd::ConstCharRange(op, gs.source - op);
        if (lkup.empty()) return false;
        lookup = ostd::Box<char[]>(cs_dup_ostr(lkup));
lookupid:
        Ident *id = gs.cs.new_ident(lookup.get());
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
                gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                goto done;
            }
        gs.gen_str(lookup.get(), true);
        gs.code.push(CODE_LOOKUPMU);
done:
        break;
    }
    }
    return true;
}

static void compileblockmain(GenState &gs, int wordtype, int prevargs) {
    char const *line = gs.source, *start = gs.source;
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
            char const *esc = gs.source;
            while (gs.current() == '@') gs.next_char();
            int level = gs.source - (esc - 1);
            if (brak > level) continue;
            else if (brak < level) cs_debug_code_line(gs.cs, line, "too many @s");
            if (!concs && prevargs >= MaxResults) gs.code.push(CODE_ENTER);
            if (concs + 2 > MaxArguments) {
                gs.code.push(CODE_CONCW | RET_STR | (concs << 8));
                concs = 1;
            }
            if (compileblockstr(gs, ostd::ConstCharRange(start, esc - 1), true)) concs++;
            if (compileblocksub(gs, prevargs + concs)) concs++;
            if (concs) start = gs.source;
            else if (prevargs >= MaxResults) gs.code.pop();
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
        if (prevargs >= MaxResults) {
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

static bool compilearg(GenState &gs, int wordtype, int prevargs, ostd::Box<char[]> *word) {
    ostd::Box<char[]> unused;
    if (!word) word = &unused;
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
            *word = ostd::Box<char[]>(cutstring(gs.source));
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
            char *s = cutstring(gs.source);
            gs.gen_value(wordtype, s);
            delete[] s;
            break;
        }
        }
        return true;
    case '$':
        compilelookup(gs, wordtype, prevargs);
        return true;
    case '(':
        gs.next_char();
        if (prevargs >= MaxResults) {
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
            char const *s = gs.source;
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
            *word = ostd::Box<char[]>(cutword(gs.source));
            return !!*word;
        default: {
            char *s = cutword(gs.source);
            if (!s) return false;
            gs.gen_value(wordtype, s);
            delete[] s;
            return true;
        }
        }
    }
}

static void compilestatements(GenState &gs, int rettype, int brak, int prevargs) {
    char const *line = gs.source;
    ostd::Box<char[]> idname;
    int numargs;
    for (;;) {
        skipcomments(gs.source);
        idname.reset();
        bool more = compilearg(gs, VAL_WORD, prevargs, &idname);
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
                if (idname) {
                    Ident *id = gs.cs.new_ident(idname.get());
                    if (id) switch (id->type) {
                        case ID_ALIAS:
                            if (!(more = compilearg(gs, VAL_ANY, prevargs))) gs.gen_str();
                            gs.code.push((id->index < MaxArguments ? CODE_ALIASARG : CODE_ALIAS) | (id->index << 8));
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
                    gs.gen_str(idname.get(), true);
                }
                if (!(more = compilearg(gs, VAL_ANY))) gs.gen_str();
                gs.code.push(CODE_ALIASU);
                goto endstatement;
            }
        numargs = 0;
        if (!idname) {
noid:
            while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
            gs.code.push(CODE_CALLU | (numargs << 8));
        } else {
            Ident *id = gs.cs.idents.at(idname.get());
            if (!id) {
                if (!cs_check_num(idname.get())) {
                    gs.gen_str(idname.get(), true);
                    goto noid;
                }
                switch (rettype) {
                case VAL_ANY:
                case VAL_CANY: {
                    char *end = idname.get();
                    ostd::Size idlen = strlen(idname.get());
                    int val = int(strtoul(idname.get(), &end, 0));
                    if (end < &idname[idlen]) gs.gen_str(idname.get(), rettype == VAL_CANY);
                    else gs.gen_int(val);
                    break;
                }
                default:
                    gs.gen_value(rettype, idname.get());
                    break;
                }
                gs.code.push(CODE_RESULT);
            } else switch (id->type) {
                case ID_ALIAS:
                    while (numargs < MaxArguments && (more = compilearg(gs, VAL_ANY, prevargs + numargs))) numargs++;
                    gs.code.push((id->index < MaxArguments ? CODE_CALLARG : CODE_CALL) | (numargs << 8) | (id->index << 13));
                    break;
                case ID_COMMAND: {
                    int comtype = CODE_COM, fakeargs = 0;
                    bool rep = false;
                    for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
                        case 'S':
                        case 's':
                            if (more) more = compilearg(gs, *fmt == 's' ? VAL_CSTR : VAL_STR, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_str(ostd::ConstCharRange(), *fmt == 's');
                                fakeargs++;
                            } else if (!fmt[1]) {
                                int numconc = 1;
                                while (numargs + numconc < MaxArguments && (more = compilearg(gs, VAL_CSTR, prevargs + numargs + numconc))) numconc++;
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
                            if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case 'V':
                            comtype = CODE_COMV;
                            if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                            if (more && numargs < MaxArguments) {
                                int numrep = *fmt - '0' + 1;
                                fmt -= numrep;
                                rep = true;
                            } else for (; numargs > MaxArguments; numargs--) gs.code.push(CODE_POP);
                            break;
                        }
                     gs.code.push(comtype | cs_ret_code(rettype) | (id->index << 8));
                    break;
compilecomv:
                    gs.code.push(comtype | cs_ret_code(rettype) | (numargs << 8) | (id->index << 13));
                    break;
                }
                case ID_LOCAL:
                    if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_IDENT, prevargs + numargs))) numargs++;
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
                        while (numargs < MaxArguments) {
                            more = compilearg(gs, VAL_COND, prevargs + numargs);
                            if (!more) break;
                            numargs++;
                            if ((gs.code[end] & ~CODE_RET_MASK) != (CODE_BLOCK | (ostd::Uint32(gs.code.size() - (end + 1)) << 8))) break;
                            end = gs.code.size();
                        }
                        if (more) {
                            while (numargs < MaxArguments && (more = compilearg(gs, VAL_COND, prevargs + numargs))) numargs++;
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
                        while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs)));
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

ostd::Uint32 *compilecode(CsState &cs, ostd::ConstCharRange str) {
    GenState gs(cs);
    gs.code.reserve(64);
    gs.gen_main(str);
    ostd::Uint32 *code = new ostd::Uint32[gs.code.size()];
    memcpy(code, gs.code.data(), gs.code.size() * sizeof(ostd::Uint32));
    code[0] += 0x100;
    return code;
}

static inline ostd::Uint32 const *forcecode(CsState &cs, TaggedValue &v) {
    if (v.get_type() != VAL_CODE) {
        GenState gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        v.cleanup();
        v.set_code(reinterpret_cast<Bytecode *>(gs.code.disown() + 1));
    }
    return reinterpret_cast<ostd::Uint32 const *>(v.code);
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

BytecodeRef::BytecodeRef(Bytecode *v): p_code(v) {
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
}
BytecodeRef::BytecodeRef(BytecodeRef const &v): p_code(v.p_code) {
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
}

BytecodeRef::~BytecodeRef() {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
}

BytecodeRef &BytecodeRef::operator=(BytecodeRef const &v) {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
    p_code = v.p_code;
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
    return *this;
}

BytecodeRef &BytecodeRef::operator=(BytecodeRef &&v) {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
    p_code = v.p_code;
    v.p_code = nullptr;
    return *this;
}

static ostd::Uint32 const *skipcode(ostd::Uint32 const *code, TaggedValue *result) {
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
                if (result) {
                    force_arg(*result, op & CODE_RET_MASK);
                }
                return code;
            }
            --depth;
            continue;
        }
    }
}

static inline void callcommand(CsState &cs, Ident *id, TaggedValue *args, TaggedValue &res, int numargs, bool lookup = false) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
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
                args[i].set_str("");
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
                args[i].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_NULL] + 1));
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
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(args, i), " ");
            TaggedValue tv;
            tv.set_mstr(buf.get().iter());
            id->cb_cftv(TvalRange(&tv, 1), res);
            goto cleanup;
        }
        case 'V':
            i = ostd::max(i + 1, numargs);
            id->cb_cftv(ostd::iter(args, i), res);
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
    id->cb_cftv(TvalRange(args, i), res);
cleanup:
    for (ostd::Size k = 0; k < ostd::Size(i); ++k) args[k].cleanup();
    for (; i < numargs; i++) args[i].cleanup();
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

static ostd::Uint32 const *runcode(CsState &cs, ostd::Uint32 const *code, TaggedValue &result) {
    result.set_null();
    if (rundepth >= MaxRunDepth) {
        cs_debug_code(cs, "exceeded recursion limit");
        return skipcode(code, (&result == &no_ret) ? nullptr : &result);
    }
    ++rundepth;
    int numargs = 0;
    TaggedValue args[MaxArguments + MaxResults];
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
            RETOP(CODE_NULL | RET_STR, result.set_str(""))
            RETOP(CODE_NULL | RET_INT, result.set_int(0))
            RETOP(CODE_NULL | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_FALSE | RET_STR, result.set_str("0"))
        case CODE_FALSE|RET_NULL:
            RETOP(CODE_FALSE | RET_INT, result.set_int(0))
            RETOP(CODE_FALSE | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_TRUE | RET_STR, result.set_str("1"))
        case CODE_TRUE|RET_NULL:
            RETOP(CODE_TRUE | RET_INT, result.set_int(1))
            RETOP(CODE_TRUE | RET_FLOAT, result.set_float(1.0f))

#define RETPOP(op, val) \
                RETOP(op, { --numargs; val; args[numargs].cleanup(); })

            RETPOP(CODE_NOT | RET_STR, result.set_str(args[numargs].get_bool() ? "0" : "1"))
        case CODE_NOT|RET_NULL:
                RETPOP(CODE_NOT | RET_INT, result.set_int(args[numargs].get_bool() ? 0 : 1))
                RETPOP(CODE_NOT | RET_FLOAT, result.set_float(args[numargs].get_bool() ? 0.0f : 1.0f))

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
            force_arg(result, op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_EXIT|RET_NULL:
            goto exit;
        case CODE_RESULT_ARG|RET_STR:
        case CODE_RESULT_ARG|RET_INT:
        case CODE_RESULT_ARG|RET_FLOAT:
            force_arg(result, op & CODE_RET_MASK);
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
            IdentStack locals[MaxArguments];
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
                    cs.run_ret(args[--numargs].code, result);
                    args[numargs].cleanup();
                    force_arg(result, op & CODE_RET_MASK);
                });
                continue;
            }
        /* fallthrough */
        case CODE_DO|RET_NULL:
        case CODE_DO|RET_STR:
        case CODE_DO|RET_INT:
        case CODE_DO|RET_FLOAT:
            result.cleanup();
            cs.run_ret(args[--numargs].code, result);
            args[numargs].cleanup();
            force_arg(result, op & CODE_RET_MASK);
            continue;

        case CODE_JUMP: {
            ostd::Uint32 len = op >> 8;
            code += len;
            continue;
        }
        case CODE_JUMP_TRUE: {
            ostd::Uint32 len = op >> 8;
            if (args[--numargs].get_bool()) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_FALSE: {
            ostd::Uint32 len = op >> 8;
            if (!args[--numargs].get_bool()) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_RESULT_TRUE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                cs.run_ret(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (result.get_bool()) code += len;
            continue;
        }
        case CODE_JUMP_RESULT_FALSE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                cs.run_ret(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (!result.get_bool()) code += len;
            continue;
        }

        case CODE_MACRO: {
            ostd::Uint32 len = op >> 8;
            cs_set_macro(args[numargs++], reinterpret_cast<Bytecode const *>(code), len);
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }

        case CODE_VAL|RET_STR: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_str(ostd::ConstCharRange(reinterpret_cast<char const *>(code), len));
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }
        case CODE_VALI|RET_STR: {
            char s[4] = { char((op >> 8) & 0xFF), char((op >> 16) & 0xFF), char((op >> 24) & 0xFF), '\0' };
            args[numargs++].set_str(s);
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
            args[numargs++].set_float(*reinterpret_cast<float const *>(code++));
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
            args[numargs].set_str(ostd::move(args[numargs - 1].get_str()));
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
            force_arg(result, op & CODE_RET_MASK);
            continue;

        case CODE_EMPTY|RET_NULL:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_NULL] + 1));
            break;
        case CODE_EMPTY|RET_STR:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_STR] + 1));
            break;
        case CODE_EMPTY|RET_INT:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_INT] + 1));
            break;
        case CODE_EMPTY|RET_FLOAT:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_FLOAT] + 1));
            break;
        case CODE_BLOCK: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_code(reinterpret_cast<Bytecode const *>(code + 1));
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
            arg.set_code(reinterpret_cast<Bytecode const *>(gs.code.disown() + 1));
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
                    arg.set_code(reinterpret_cast<Bytecode const *>(gs.code.disown() + 1));
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
            Ident *id = arg.get_type() == VAL_STR || arg.get_type() == VAL_MACRO || arg.get_type() == VAL_CSTR ? cs.new_ident(ostd::ConstCharRange(arg.cstr, arg.len)) : cs.dummy;
            if (id->index < MaxArguments && !(cs.stack->usedargs & (1 << id->index))) {
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
                            if(id->index < MaxArguments && !(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                            aval; \
                            continue; \
                        case ID_SVAR: arg.cleanup(); sval; continue; \
                        case ID_VAR: arg.cleanup(); ival; continue; \
                        case ID_FVAR: arg.cleanup(); fval; continue; \
                        case ID_COMMAND: \
                        { \
                            arg.cleanup(); \
                            arg.set_null(); \
                            TaggedValue buf[MaxArguments]; \
                            callcommand(cs, id, buf, arg, 0, true); \
                            force_arg(arg, op&CODE_RET_MASK); \
                            continue; \
                        } \
                        default: arg.cleanup(); nval; continue; \
                    } \
                    cs_debug_code(cs, "unknown alias lookup: %s", arg.s); \
                    arg.cleanup(); \
                    nval; \
                    continue; \
                }
            LOOKUPU(arg.set_str(ostd::move(id->get_str())),
                    arg.set_str(*id->storage.sp),
                    arg.set_str(ostd::move(intstr(*id->storage.ip))),
                    arg.set_str(ostd::move(floatstr(*id->storage.fp))),
                    arg.set_str(""));
        case CODE_LOOKUP|RET_STR:
#define LOOKUP(aval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(id->flags&IDF_UNKNOWN) cs_debug_code(cs, "unknown alias lookup: %s", id->name); \
                    aval; \
                    continue; \
                }
            LOOKUP(args[numargs++].set_str(ostd::move(id->get_str())));
        case CODE_LOOKUPARG|RET_STR:
#define LOOKUPARG(aval, nval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(!(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                    aval; \
                    continue; \
                }
            LOOKUPARG(args[numargs++].set_str(ostd::move(id->get_str())), args[numargs++].set_str(""));
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
                    arg.set_str(*id->storage.sp),
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
                    arg.set_str(ostd::move(intstr(*id->storage.ip))),
                    arg.set_str(ostd::move(floatstr(*id->storage.fp))),
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
            args[numargs++].set_str(*cs.identmap[op >> 8]->storage.sp);
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
            args[numargs++].set_str(ostd::move(intstr(*cs.identmap[op >> 8]->storage.ip)));
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
            args[numargs++].set_str(ostd::move(floatstr(*cs.identmap[op >> 8]->storage.fp)));
            continue;
        case CODE_FVAR|RET_INT:
            args[numargs++].set_int(int(*cs.identmap[op >> 8]->storage.fp));
            continue;
        case CODE_FVAR1:
            cs.set_var_float_checked(cs.identmap[op >> 8], args[--numargs].f);
            continue;

        case CODE_COM|RET_NULL:
        case CODE_COM|RET_STR:
        case CODE_COM|RET_FLOAT:
        case CODE_COM|RET_INT: {
            Ident *id = cs.identmap[op >> 8];
            int offset = numargs - id->numargs;
            result.force_null();
            id->cb_cftv(TvalRange(args + offset, id->numargs), result);
            force_arg(result, op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
            }

        case CODE_COMV|RET_NULL:
        case CODE_COMV|RET_STR:
        case CODE_COMV|RET_FLOAT:
        case CODE_COMV|RET_INT: {
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            id->cb_cftv(ostd::iter(&args[offset], callargs), result);
            force_arg(result, op & CODE_RET_MASK);
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
                auto buf = ostd::appender<ostd::String>();
                cscript::util::tvals_concat(buf, ostd::iter(&args[offset], callargs), " ");
                TaggedValue tv;
                tv.set_mstr(buf.get().iter());
                id->cb_cftv(TvalRange(&tv, 1), result);
            }
            force_arg(result, op & CODE_RET_MASK);
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
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(&args[numargs - numconc], numconc), ((op & CODE_OP_MASK) == CODE_CONC) ? " " : "");
            free_args(args, numargs, numargs - numconc);
            args[numargs].set_mstr(buf.get().iter());
            buf.get().disown();
            force_arg(args[numargs], op & CODE_RET_MASK);
            numargs++;
            continue;
        }

        case CODE_CONCM|RET_NULL:
        case CODE_CONCM|RET_STR:
        case CODE_CONCM|RET_FLOAT:
        case CODE_CONCM|RET_INT: {
            int numconc = op >> 8;
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(&args[numargs - numconc], numconc));
            free_args(args, numargs, numargs - numconc);
            result.set_mstr(buf.get().iter());
            buf.get().disown();
            force_arg(result, op & CODE_RET_MASK);
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
                force_arg(result, op&CODE_RET_MASK); \
                continue; \
            }
#define CALLALIAS(cs, result) { \
                IdentStack argstack[MaxArguments]; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->push_arg(args[offset + i], argstack[i], false); \
                int oldargs = (cs).numargs; \
                (cs).numargs = callargs; \
                int oldflags = (cs).identflags; \
                (cs).identflags |= id->flags&IDF_OVERRIDDEN; \
                IdentLink aliaslink = { id, (cs).stack, (1<<callargs)-1, argstack }; \
                (cs).stack = &aliaslink; \
                if(!id->code) id->code = reinterpret_cast<Bytecode *>(compilecode(cs, id->get_str())); \
                ostd::Uint32 *codep = reinterpret_cast<ostd::Uint32 *>(id->code); \
                codep[0] += 0x100; \
                runcode((cs), codep+1, (result)); \
                codep[0] -= 0x100; \
                if(int(codep[0]) < 0x100) delete[] codep; \
                (cs).stack = aliaslink.next; \
                (cs).identflags = oldflags; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->pop_arg(); \
                for(int argmask = aliaslink.usedargs&(~0<<callargs), i = callargs; argmask; i++) \
                    if(argmask&(1<<i)) { (cs).identmap[i]->pop_arg(); argmask &= ~(1<<i); } \
                force_arg(result, op&CODE_RET_MASK); \
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
                force_arg(result, op & CODE_RET_MASK);
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
                if (!id->cb_cftv) FORCERESULT;
            /* fallthrough */
            case ID_COMMAND:
                idarg.cleanup();
                callcommand(cs, id, &args[offset], result, callargs);
                force_arg(result, op & CODE_RET_MASK);
                numargs = offset - 1;
                continue;
            case ID_LOCAL: {
                IdentStack locals[MaxArguments];
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
                if (id->index < MaxArguments && !(cs.stack->usedargs & (1 << id->index))) FORCERESULT;
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
    --rundepth;
    return code;
}

void CsState::run_ret(Bytecode const *code, TaggedValue &ret) {
    runcode(*this, reinterpret_cast<ostd::Uint32 const *>(code), ret);
}

void CsState::run_ret(ostd::ConstCharRange code, TaggedValue &ret) {
    GenState gs(*this);
    gs.code.reserve(64);
    /* FIXME range */
    gs.gen_main(code.data(), VAL_ANY);
    runcode(*this, gs.code.data() + 1, ret);
    if (int(gs.code[0]) >= 0x100)
        gs.code.disown();
}

/* TODO */
void CsState::run_ret(Ident *id, TvalRange args, TaggedValue &ret) {
    int nargs = int(args.size());
    ret.set_null();
    ++rundepth;
    if (rundepth > MaxRunDepth) cs_debug_code(*this, "exceeded recursion limit");
    else if (id) switch (id->type) {
        default:
            if (!id->cb_cftv) break;
        /* fallthrough */
        case ID_COMMAND:
            if (nargs < id->numargs) {
                TaggedValue buf[MaxArguments];
                memcpy(buf, args.data(), args.size() * sizeof(TaggedValue));
                callcommand(*this, id, buf, ret, nargs, false);
            } else callcommand(*this, id, args.data(), ret, nargs, false);
            nargs = 0;
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
            if (id->index < MaxArguments && !(stack->usedargs & (1 << id->index))) break;
            if (id->get_valtype() == VAL_NULL) break;
#define callargs nargs
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
    free_args(args.data(), nargs, 0);
    --rundepth;
}

ostd::String CsState::run_str(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

ostd::String CsState::run_str(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

ostd::String CsState::run_str(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

int CsState::run_int(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

int CsState::run_int(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

int CsState::run_int(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

float CsState::run_float(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

float CsState::run_float(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

float CsState::run_float(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

bool CsState::run_bool(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_file(ostd::ConstCharRange fname) {
    ostd::ConstCharRange oldsrcfile = src_file, oldsrcstr = src_str;
    char *buf = nullptr;
    ostd::Size len;

    ostd::FileStream f(fname, ostd::StreamMode::read);
    if (!f.is_open())
        return false;

    len = f.size();
    buf = new char[len + 1];
    if (f.get(buf, len) != len) {
        delete[] buf;
        return false;
    }
    buf[len] = '\0';

    src_file = fname;
    src_str = ostd::ConstCharRange(buf, len);
    run_int(buf);
    src_file = oldsrcfile;
    src_str = oldsrcstr;
    delete[] buf;
    return true;
}

void cs_init_lib_io(CsState &cs) {
    cs_add_command(cs, "exec", "sb", [&cs](TvalRange args, TaggedValue &res) {
        auto file = args[0].get_strr();
        bool ret = cs.run_file(file);
        if (!ret) {
            if (args[1].get_int())
                ostd::err.writefln("could not run file \"%s\"", file);
            res.set_int(0);
        } else
            res.set_int(1);
    });

    cs_add_command(cs, "echo", "C", [](TvalRange args, TaggedValue &) {
        ostd::writeln(args[0].get_strr());
    });
}

static inline void cs_set_iter(Ident &id, int i, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.get_valtype() != VAL_INT) {
            if (id.get_valtype() == VAL_STR) {
                delete[] id.val.s;
                id.val.s = nullptr;
                id.val.len = 0;
            }
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
                              int step, Bytecode *cond, Bytecode *body) {
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

static inline void cs_loop_conc(
    CsState &cs, TaggedValue &res, Ident &id, int offset, int n,
    int step, Bytecode *body, bool space
) {
    if (n <= 0 || id.type != ID_ALIAS)
        return;
    IdentStack stack;
    ostd::Vector<char> s;
    for (int i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        TaggedValue v;
        cs.run_ret(body, v);
        ostd::String vstr = ostd::move(v.get_str());
        if (space && i) s.push(' ');
        s.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n > 0) id.pop_arg();
    s.push('\0');
    ostd::Size len = s.size() - 1;
    res.set_mstr(ostd::CharRange(s.disown(), len));
}

void cs_init_lib_base(CsState &cs) {
    cs_add_command(cs, "do", "e", [&cs](TvalRange args, TaggedValue &res) {
        cs.run_ret(args[0].get_code(), res);
    }, ID_DO);

    cs_add_command(cs, "doargs", "e", [&cs](TvalRange args, TaggedValue &res) {
        if (cs.stack != &cs.noalias)
            cs_do_args(cs, [&]() { cs.run_ret(args[0].get_code(), res); });
        else
            cs.run_ret(args[0].get_code(), res);
    }, ID_DOARGS);

    cs_add_command(cs, "if", "tee", [&cs](TvalRange args, TaggedValue &res) {
        cs.run_ret((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    }, ID_IF);

    cs_add_command(cs, "result", "T", [](TvalRange args, TaggedValue &res) {
        TaggedValue &v = args[0];
        res = v;
        v.set_null();
    }, ID_RESULT);

    cs_add_command(cs, "!", "t", [](TvalRange args, TaggedValue &res) {
        res.set_int(!args[0].get_bool());
    }, ID_NOT);

    cs_add_command(cs, "&&", "E1V", [&cs](TvalRange args, TaggedValue &res) {
        if (args.empty())
            res.set_int(1);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) res.cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code, res);
            else
                res = args[i];
            if (!res.get_bool()) break;
        }
    }, ID_AND);

    cs_add_command(cs, "||", "E1V", [&cs](TvalRange args, TaggedValue &res) {
        if (args.empty())
            res.set_int(0);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) res.cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code, res);
            else
                res = args[i];
            if (res.get_bool()) break;
        }
    }, ID_OR);

    cs_add_command(cs, "?", "tTT", [](TvalRange args, TaggedValue &res) {
        res.set(args[0].get_bool() ? args[1] : args[2]);
    });

    cs_add_command(cs, "cond", "ee2V", [&cs](TvalRange args, TaggedValue &res) {
        for (ostd::Size i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run_bool(args[i].code)) {
                    cs.run_ret(args[i + 1].code, res);
                    break;
                }
            } else {
                cs.run_ret(args[i].code, res);
                break;
            }
        }
    });

#define CS_CMD_CASE(name, fmt, type, acc, compare) \
    cs_add_command(cs, name, fmt "te2V", [&cs](TvalRange args, TaggedValue &res) { \
        type val = ostd::move(acc); \
        ostd::Size i; \
        for (i = 1; (i + 1) < args.size(); i += 2) { \
            if (compare) { \
                cs.run_ret(args[i + 1].code, res); \
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

    CS_CMD_CASE("cases", "s", ostd::String, args[0].get_str(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_str() == val)));

#undef CS_CMD_CASE

    cs_add_command(cs, "pushif", "rTe", [&cs](TvalRange args, TaggedValue &res) {
        Ident *id = args[0].get_ident();
        TaggedValue &v = args[1];
        Bytecode *code = args[2].get_code();
        if ((id->type != ID_ALIAS) || (id->index < MaxArguments))
            return;
        if (v.get_bool()) {
            IdentStack stack;
            id->push_arg(v, stack);
            v.set_null();
            cs.run_ret(code, res);
            id->pop_arg();
        }
    });

    cs_add_command(cs, "loop", "rie", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    cs_add_command(cs, "loop+", "riie", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    cs_add_command(cs, "loop*", "riie", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    cs_add_command(cs, "loop+*", "riiie", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile", "riee", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    cs_add_command(cs, "loopwhile+", "riiee", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile*", "riiee", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile+*", "riiiee", [&cs](TvalRange args, TaggedValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    cs_add_command(cs, "while", "ee", [&cs](TvalRange args, TaggedValue &) {
        Bytecode *cond = args[0].get_code(), *body = args[1].get_code();
        while (cs.run_bool(cond)) {
            cs.run_int(body);
        }
    });

    cs_add_command(cs, "loopconcat", "rie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat+", "riie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat*", "riie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat+*", "riiie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcatword", "rie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword+", "riie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword*", "riie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword+*", "riiie", [&cs](TvalRange args, TaggedValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });

    cs_add_command(cs, "nodebug", "e", [&cs](TvalRange args, TaggedValue &res) {
        ++cs.nodebug;
        cs.run_ret(args[0].get_code(), res);
        --cs.nodebug;
    });

    cs_add_command(cs, "push", "rTe", [&cs](TvalRange args, TaggedValue &res) {
        Ident *id = args[0].get_ident();
        if (id->type != ID_ALIAS || id->index < MaxArguments) return;
        IdentStack stack;
        TaggedValue &v = args[1];
        id->push_arg(v, stack);
        v.set_null();
        cs.run_ret(args[2].get_code(), res);
        id->pop_arg();
    });

    cs_add_command(cs, "local", nullptr, nullptr, ID_LOCAL);

    cs_add_command(cs, "resetvar", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_int(cs.reset_var(args[0].get_strr()));
    });

    cs_add_command(cs, "alias", "sT", [&cs](TvalRange args, TaggedValue &) {
        TaggedValue &v = args[1];
        cs.set_alias(args[0].get_strr(), v);
        v.set_null();
    });

    cs_add_command(cs, "getvarmin", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_int(cs.get_var_min_int(args[0].get_strr()).value_or(0));
    });
    cs_add_command(cs, "getvarmax", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_int(cs.get_var_max_int(args[0].get_strr()).value_or(0));
    });
    cs_add_command(cs, "getfvarmin", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(cs.get_var_min_float(args[0].get_strr()).value_or(0.0f));
    });
    cs_add_command(cs, "getfvarmax", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(cs.get_var_max_float(args[0].get_strr()).value_or(0.0f));
    });

    cs_add_command(cs, "identexists", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_int(cs.have_ident(args[0].get_strr()));
    });

    cs_add_command(cs, "getalias", "s", [&cs](TvalRange args, TaggedValue &res) {
        res.set_str(ostd::move(cs.get_alias(args[0].get_strr()).value_or("")));
    });
}

void cs_init_lib_math(CsState &cs);
void cs_init_lib_string(CsState &cs);
void cs_init_lib_list(CsState &cs);

OSTD_EXPORT void init_libs(CsState &cs, int libs) {
    if (libs & CS_LIB_IO    ) cs_init_lib_io(cs);
    if (libs & CS_LIB_MATH  ) cs_init_lib_math(cs);
    if (libs & CS_LIB_STRING) cs_init_lib_string(cs);
    if (libs & CS_LIB_LIST  ) cs_init_lib_list(cs);
}

} /* namespace cscript */
