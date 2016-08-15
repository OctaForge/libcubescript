#include "cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

ostd::String intstr(CsInt v) {
    char buf[256];
    snprintf(buf, sizeof(buf), IntFormat, v);
    return buf;
}

ostd::String floatstr(CsFloat v) {
    char buf[256];
    snprintf(buf, sizeof(buf), v == CsInt(v) ? RoundFloatFormat : FloatFormat, v);
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

/* ID_IVAR */
Ident::Ident(ostd::ConstCharRange n, CsInt m, CsInt x, CsInt *s,
             VarCb f, int flagsv)
    : type(ID_IVAR), flags(flagsv | (m > x ? IDF_READONLY : 0)), name(n),
      minval(m), maxval(x), cb_var(ostd::move(f)) {
    storage.ip = s;
}

/* ID_FVAR */
Ident::Ident(ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat *s,
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
Ident::Ident(ostd::ConstCharRange n, CsInt a, int flagsv)
    : type(ID_ALIAS), valtype(VAL_INT), flags(flagsv), name(n), code(nullptr),
      stack(nullptr) {
    val.i = a;
}
Ident::Ident(ostd::ConstCharRange n, CsFloat a, int flagsv)
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
    case ID_IVAR:
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
    case ID_IVAR:
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
        case ID_IVAR:
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

void CsState::print_var_int(Ident *id, CsInt i) {
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

void CsState::print_var_float(Ident *id, CsFloat f) {
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
    case ID_IVAR:
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

CsFloat TaggedValue::force_float() {
    CsFloat rf = 0.0f;
    switch (get_type()) {
    case VAL_INT:
        rf = i;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        rf = parser::parse_float(s);
        break;
    case VAL_FLOAT:
        return f;
    }
    cleanup();
    set_float(rf);
    return rf;
}

CsInt TaggedValue::force_int() {
    CsInt ri = 0;
    switch (get_type()) {
    case VAL_FLOAT:
        ri = f;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        ri = parser::parse_int(s);
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

static inline CsInt cs_get_int(IdentValue const &v, int type) {
    switch (type) {
    case VAL_FLOAT:
        return CsInt(v.f);
    case VAL_INT:
        return v.i;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return parser::parse_int(v.s);
    }
    return 0;
}

CsInt TaggedValue::get_int() const {
    return cs_get_int(*this, get_type());
}

CsInt Ident::get_int() const {
    return cs_get_int(val, get_valtype());
}

static inline CsFloat cs_get_float(IdentValue const &v, int type) {
    switch (type) {
    case VAL_FLOAT:
        return v.f;
    case VAL_INT:
        return CsFloat(v.i);
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return parser::parse_float(v.s);
    }
    return 0.0f;
}

CsFloat TaggedValue::get_float() const {
    return cs_get_float(*this, get_type());
}

CsFloat Ident::get_float() const {
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
    if (s.empty()) {
        return false;
    }
    ostd::ConstCharRange end = s;
    CsInt ival = parser::parse_int(end, &end);
    if (end.empty()) {
        return !!ival;
    }
    end = s;
    CsFloat fval = parser::parse_float(end, &end);
    if (end.empty()) {
        return !!fval;
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

void CsState::set_var_int(ostd::ConstCharRange name, CsInt v,
                          bool dofunc, bool doclamp) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_IVAR)
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

void CsState::set_var_float(ostd::ConstCharRange name, CsFloat v,
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

ostd::Maybe<CsInt> CsState::get_var_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_IVAR)
        return ostd::nothing;
    return *id->storage.ip;
}

ostd::Maybe<CsFloat> CsState::get_var_float(ostd::ConstCharRange name) {
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

ostd::Maybe<CsInt> CsState::get_var_min_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_IVAR)
        return ostd::nothing;
    return id->minval;
}

ostd::Maybe<CsInt> CsState::get_var_max_int(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_IVAR)
        return ostd::nothing;
    return id->maxval;
}

ostd::Maybe<CsFloat> CsState::get_var_min_float(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id || id->type != ID_FVAR)
        return ostd::nothing;
    return id->minvalf;
}

ostd::Maybe<CsFloat> CsState::get_var_max_float(ostd::ConstCharRange name) {
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

CsInt cs_clamp_var(CsState &cs, Ident *id, CsInt v) {
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

void CsState::set_var_int_checked(Ident *id, CsInt v) {
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
    CsInt v = args[0].force_int();
    if ((id->flags & IDF_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2)
            v |= args[2].force_int();
    }
    set_var_int_checked(id, v);
}

CsFloat cs_clamp_fvar(CsState &cs, Ident *id, CsFloat v) {
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

void CsState::set_var_float_checked(Ident *id, CsFloat v) {
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

static inline void cs_set_iter(Ident &id, CsInt i, IdentStack &stack) {
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

static inline void cs_do_loop(CsState &cs, Ident &id, CsInt offset, CsInt n,
                              CsInt step, Bytecode *cond, Bytecode *body) {
    if (n <= 0 || (id.type != ID_ALIAS))
        return;
    IdentStack stack;
    for (CsInt i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        if (cond && !cs.run_bool(cond)) break;
        cs.run_int(body);
    }
    id.pop_arg();
}

static inline void cs_loop_conc(
    CsState &cs, TaggedValue &res, Ident &id, CsInt offset, CsInt n,
    CsInt step, Bytecode *body, bool space
) {
    if (n <= 0 || id.type != ID_ALIAS)
        return;
    IdentStack stack;
    ostd::Vector<char> s;
    for (CsInt i = 0; i < n; ++i) {
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

    CS_CMD_CASE("case", "i", CsInt, args[0].get_int(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_int() == val)));

    CS_CMD_CASE("casef", "f", CsFloat, args[0].get_float(),
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
