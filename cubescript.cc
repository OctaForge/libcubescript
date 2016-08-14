#include "cubescript.hh"
#include "cs_vm.hh"

#include <limits.h>
#include <ctype.h>
#include <math.h>

#include <ostd/algorithm.hh>
#include <ostd/format.hh>
#include <ostd/memory.hh>

namespace cscript {

int parseint(char const *s) {
    return int(strtoul(s, nullptr, 0));
}

int cs_parse_int(ostd::ConstCharRange s) {
    if (s.empty()) return 0;
    return parseint(s.data());
}

float parsefloat(char const *s) {
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

bool cs_check_num(ostd::ConstCharRange s) {
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

void Ident::get_cstr(TaggedValue &v) const {
    switch (get_valtype()) {
    case VAL_MACRO:
        v.set_macro(val.code, val.len);
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
        v.set_macro(val.code, val.len);
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

void Ident::clean_code() {
    ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(code);
    if (bcode) {
        bcode_decr(bcode);
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

static void compilestatements(GenState &gs, int rettype, int brak = '\0', int prevargs = 0);
static inline char const *compileblock(GenState &gs, char const *p, int rettype = RET_NULL, int brak = '\0');

void GenState::gen_int(ostd::ConstCharRange word) {
    gen_int(cs_parse_int(word));
}

void GenState::gen_float(ostd::ConstCharRange word) {
    gen_float(cs_parse_float(word));
}

void GenState::gen_value(int wordtype, ostd::ConstCharRange word) {
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
    bcode_incr(code);
    return code;
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
