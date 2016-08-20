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
    if (isdigit(s[0])) {
        return true;
    }
    switch (s[0]) {
        case '+':
        case '-':
            return isdigit(s[1]) || ((s[1] == '.') && isdigit(s[2]));
        case '.':
            return isdigit(s[1]) != 0;
        default:
            return false;
    }
}

Ident::Ident(IdentType tp, ostd::ConstCharRange nm, int fl):
    p_name(nm), p_type(int(tp)), p_flags(fl)
{}

Var::Var(IdentType tp, ostd::ConstCharRange name, VarCb f, int fl):
    Ident(tp, name, fl), cb_var(ostd::move(f))
{}

Ivar::Ivar(
    ostd::ConstCharRange name, CsInt m, CsInt x, CsInt *s, VarCb f, int fl
):
    Var(IdentType::ivar, name, ostd::move(f), fl | ((m > x) ? IDF_READONLY : 0)),
    p_storage(s), p_minval(m), p_maxval(x), p_overrideval(0)
{}

Fvar::Fvar(
    ostd::ConstCharRange name, CsFloat m, CsFloat x, CsFloat *s, VarCb f, int fl
):
    Var(IdentType::fvar, name, ostd::move(f), fl | ((m > x) ? IDF_READONLY : 0)),
    p_storage(s), p_minval(m), p_maxval(x), p_overrideval(0)
{}

Svar::Svar(ostd::ConstCharRange name, char **s, VarCb f, int fl):
    Var(IdentType::svar, name, ostd::move(f), fl),
    p_storage(s), p_overrideval(nullptr)
{}

Alias::Alias(ostd::ConstCharRange name, char *a, int fl):
    Ident(IdentType::alias, name, fl),
    code(nullptr), stack(nullptr)
{
    val_v.set_mstr(a);
}
Alias::Alias(ostd::ConstCharRange name, CsInt a, int fl):
    Ident(IdentType::alias, name, fl),
    code(nullptr), stack(nullptr)
{
    val_v.set_int(a);
}
Alias::Alias(ostd::ConstCharRange name, CsFloat a, int fl):
    Ident(IdentType::alias, name, fl),
    code(nullptr), stack(nullptr)
{
    val_v.set_float(a);
}
Alias::Alias(ostd::ConstCharRange name, int fl):
    Ident(IdentType::alias, name, fl),
    code(nullptr), stack(nullptr)
{
    val_v.set_null();
}
Alias::Alias(ostd::ConstCharRange name, CsValue const &v, int fl):
    Ident(IdentType::alias, name, fl),
    code(nullptr), stack(nullptr), val_v(v)
{}

Command::Command(
    int tp, ostd::ConstCharRange name, ostd::ConstCharRange args,
    ostd::Uint32 amask, int nargs, CmdFunc f
):
    Ident(IdentType::command, name, 0),
    cargs(!args.empty() ? cs_dup_ostr(args) : nullptr),
    argmask(amask), numargs(nargs), cb_cftv(ostd::move(f))
{
    p_type = tp;
}

bool Ident::is_alias() const {
    return get_type() == IdentType::alias;
}

Alias *Ident::get_alias() {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<Alias *>(this);
}

Alias const *Ident::get_alias() const {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<Alias const *>(this);
}

bool Ident::is_command() const {
    return get_type() == IdentType::command;
}

bool Ident::is_special() const {
    return get_type() == IdentType::special;
}

bool Ident::is_var() const {
    IdentType tp = get_type();
    return (tp >= IdentType::ivar) && (tp <= IdentType::svar);
}

Var *Ident::get_var() {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<Var *>(this);
}

Var const *Ident::get_var() const {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<Var const *>(this);
}

bool Ident::is_ivar() const {
    return get_type() == IdentType::ivar;
}

Ivar *Ident::get_ivar() {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<Ivar *>(this);
}

Ivar const *Ident::get_ivar() const {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<Ivar const *>(this);
}

bool Ident::is_fvar() const {
    return get_type() == IdentType::fvar;
}

Fvar *Ident::get_fvar() {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<Fvar *>(this);
}

Fvar const *Ident::get_fvar() const {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<Fvar const *>(this);
}

bool Ident::is_svar() const {
    return get_type() == IdentType::svar;
}

Svar *Ident::get_svar() {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<Svar *>(this);
}

Svar const *Ident::get_svar() const {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<Svar const *>(this);
}

CsInt Ivar::get_val_min() const {
    return p_minval;
}
CsInt Ivar::get_val_max() const {
    return p_maxval;
}

CsInt Ivar::get_var_value() const {
    return *p_storage;
}

CsFloat Fvar::get_val_min() const {
    return p_minval;
}
CsFloat Fvar::get_val_max() const {
    return p_maxval;
}

CsFloat Fvar::get_var_value() const {
    return *p_storage;
}

ostd::ConstCharRange Svar::get_var_value() const {
    return *p_storage;
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
    add_ident<Ivar>("numargs", MaxArguments, 0, &numargs);
    add_ident<Ivar>("dbgalias", 0, 1000, &dbgalias);
    cs_init_lib_base(*this);
}

CsState::~CsState() {
    for (auto &p: idents.iter()) {
        Ident *i = p.second;
        Alias *a = i->get_alias();
        if (a) {
            a->force_null();
            delete[] reinterpret_cast<ostd::Uint32 *>(a->code);
        } else if (i->is_command() || i->is_special()) {
            delete[] static_cast<Command *>(i)->cargs;
        }
        delete i;
    }
}

void CsState::clear_override(Ident &id) {
    if (!(id.get_flags() & IDF_OVERRIDDEN)) {
        return;
    }
    switch (id.get_type()) {
        case IdentType::alias: {
            Alias &a = static_cast<Alias &>(id);
            a.cleanup_value();
            a.clean_code();
            a.set_value_str("");
            break;
        }
        case IdentType::ivar: {
            Ivar &iv = static_cast<Ivar &>(id);
            *iv.p_storage = iv.p_overrideval;
            iv.changed();
            break;
        }
        case IdentType::fvar: {
            Fvar &fv = static_cast<Fvar &>(id);
            *fv.p_storage = fv.p_overrideval;
            fv.changed();
            break;
        }
        case IdentType::svar: {
            Svar &sv = static_cast<Svar &>(id);
            delete[] *sv.p_storage;
            *sv.p_storage = sv.p_overrideval;
            sv.changed();
            break;
        }
        default:
            break;
    }
    id.p_flags &= ~IDF_OVERRIDDEN;
}

void CsState::clear_overrides() {
    for (auto &p: idents.iter()) {
        clear_override(*(p.second));
    }
}

Ident *CsState::add_ident(Ident *id) {
    if (!id) {
        return nullptr;
    }
    idents[id->get_name()] = id;
    id->p_index = identmap.size();
    return identmap.push(id);
}

Ident *CsState::new_ident(ostd::ConstCharRange name, int flags) {
    Ident *id = get_ident(name);
    if (!id) {
        if (cs_check_num(name)) {
            cs_debug_code(
                *this, "number %s is not a valid identifier name", name
            );
            return dummy;
        }
        id = add_ident<Alias>(name, flags);
    }
    return id;
}

Ident *CsState::force_ident(CsValue &v) {
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
    Ident *id = get_ident(name);
    if (!id) {
        return false;
    }
    if (id->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable %s is read only", id->get_name());
        return false;
    }
    clear_override(*id);
    return true;
}

void CsState::touch_var(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (id && id->is_var()) {
        static_cast<Var *>(id)->changed();
    }
}

void CsState::set_alias(ostd::ConstCharRange name, CsValue &v) {
    Ident *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case IdentType::alias: {
                Alias *a = static_cast<Alias *>(id);
                if (a->get_index() < MaxArguments) {
                    a->set_arg(*this, v);
                } else {
                    a->set_alias(*this, v);
                }
                return;
            }
            case IdentType::ivar:
                set_var_int_checked(static_cast<Ivar *>(id), v.get_int());
                break;
            case IdentType::fvar:
                set_var_float_checked(static_cast<Fvar *>(id), v.get_float());
                break;
            case IdentType::svar:
                set_var_str_checked(static_cast<Svar *>(id), v.get_str());
                break;
            default:
                cs_debug_code(
                    *this, "cannot redefine builtin %s with an alias",
                    id->get_name()
                );
                break;
        }
        v.cleanup();
    } else if (cs_check_num(name)) {
        cs_debug_code(*this, "cannot alias number %s", name);
        v.cleanup();
    } else {
        add_ident<Alias>(name, v, identflags);
    }
}

void CsState::print_var_int(Ivar *iv, CsInt i) {
    if (i < 0) {
        writefln("%s = %d", iv->get_name(), i);
        return;
    }
    if (iv->get_flags() & IDF_HEX) {
        if (iv->get_val_max() == 0xFFFFFF) {
            writefln(
                "%s = 0x%.6X (%d, %d, %d)", iv->get_name(),
                i, (i >> 16) & 0xFF, (i >> 8) & 0xFF, i & 0xFF
            );
        } else {
            writefln("%s = 0x%X", iv->get_name(), i);
        }
    } else {
        writefln("%s = %d", iv->get_name(), i);
    }
}

void CsState::print_var_float(Fvar *fv, CsFloat f) {
    writefln("%s = %s", fv->get_name(), floatstr(f));
}

void CsState::print_var_str(Svar *sv, ostd::ConstCharRange s) {
    if (ostd::find(s, '"').empty()) {
        writefln("%s = \"%s\"", sv->get_name(), s);
    } else {
        writefln("%s = [%s]", sv->get_name(), s);
    }
}

void CsState::print_var(Var *v) {
    switch (v->get_type()) {
        case IdentType::ivar: {
            Ivar *iv = static_cast<Ivar *>(v);
            print_var_int(iv, *iv->p_storage);
            break;
        }
        case IdentType::fvar: {
            Fvar *fv = static_cast<Fvar *>(v);
            print_var_float(fv, *fv->p_storage);
            break;
        }
        case IdentType::svar: {
            Svar *sv = static_cast<Svar *>(v);
            print_var_str(sv, *sv->p_storage);
            break;
        }
        default:
            break;
    }
}

void CsValue::cleanup() {
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

void CsValue::force_null() {
    if (get_type() == VAL_NULL) {
        return;
    }
    cleanup();
    set_null();
}

CsFloat CsValue::force_float() {
    CsFloat rf = 0.0f;
    switch (get_type()) {
        case VAL_INT:
            rf = i;
            break;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            rf = cs_parse_float(s);
            break;
        case VAL_FLOAT:
            return f;
    }
    cleanup();
    set_float(rf);
    return rf;
}

CsInt CsValue::force_int() {
    CsInt ri = 0;
    switch (get_type()) {
        case VAL_FLOAT:
            ri = f;
            break;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            ri = cs_parse_int(s);
            break;
        case VAL_INT:
            return i;
    }
    cleanup();
    set_int(ri);
    return ri;
}

ostd::ConstCharRange CsValue::force_str() {
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

CsInt CsValue::get_int() const {
    switch (get_type()) {
        case VAL_FLOAT:
            return CsInt(f);
        case VAL_INT:
            return i;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return cs_parse_int(s);
    }
    return 0;
}

CsFloat CsValue::get_float() const {
    switch (get_type()) {
        case VAL_FLOAT:
            return f;
        case VAL_INT:
            return CsFloat(i);
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return cs_parse_float(s);
    }
    return 0.0f;
}

Bytecode *CsValue::get_code() const {
    if (get_type() != VAL_CODE) {
        return nullptr;
    }
    return const_cast<Bytecode *>(code);
}

Ident *CsValue::get_ident() const {
    if (get_type() != VAL_IDENT) {
        return nullptr;
    }
    return id;
}

ostd::String CsValue::get_str() const {
    switch (get_type()) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return ostd::ConstCharRange(s, len);
    case VAL_INT:
        return intstr(i);
    case VAL_FLOAT:
        return floatstr(f);
    }
    return ostd::String("");
}

ostd::ConstCharRange CsValue::get_strr() const {
    switch (get_type()) {
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return ostd::ConstCharRange(s, len);
        default:
            break;
    }
    return ostd::ConstCharRange();
}

void CsValue::get_val(CsValue &r) const {
    switch (get_type()) {
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR: {
            r.set_str(ostd::ConstCharRange(s, len));
            break;
        }
        case VAL_INT:
            r.set_int(i);
            break;
        case VAL_FLOAT:
            r.set_float(f);
            break;
        default:
            r.set_null();
            break;
    }
}

OSTD_EXPORT bool code_is_empty(Bytecode const *code) {
    if (!code) {
        return true;
    }
    return (
        *reinterpret_cast<ostd::Uint32 const *>(code) & CODE_OP_MASK
    ) == CODE_EXIT;
}

bool CsValue::code_is_empty() const {
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
    CsInt ival = cs_parse_int(end, &end);
    if (end.empty()) {
        return !!ival;
    }
    end = s;
    CsFloat fval = cs_parse_float(end, &end);
    if (end.empty()) {
        return !!fval;
    }
    return true;
}

bool CsValue::get_bool() const {
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

void Alias::get_cstr(CsValue &v) const {
    switch (val_v.get_type()) {
        case VAL_MACRO:
            v.set_macro(val_v.code, val_v.len);
            break;
        case VAL_STR:
        case VAL_CSTR:
            v.set_cstr(ostd::ConstCharRange(val_v.s, val_v.len));
            break;
        case VAL_INT:
            v.set_str(ostd::move(intstr(val_v.i)));
            break;
        case VAL_FLOAT:
            v.set_str(ostd::move(floatstr(val_v.f)));
            break;
        default:
            v.set_cstr("");
            break;
    }
}

void Alias::get_cval(CsValue &v) const {
    switch (val_v.get_type()) {
        case VAL_MACRO:
            v.set_macro(val_v.code, val_v.len);
            break;
        case VAL_STR:
        case VAL_CSTR:
            v.set_cstr(ostd::ConstCharRange(val_v.s, val_v.len));
            break;
        case VAL_INT:
            v.set_int(val_v.i);
            break;
        case VAL_FLOAT:
            v.set_float(val_v.f);
            break;
        default:
            v.set_null();
            break;
    }
}

void Alias::clean_code() {
    ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(code);
    if (bcode) {
        bcode_decr(bcode);
        code = nullptr;
    }
}

void Alias::push_arg(CsValue const &v, IdentStack &st, bool um) {
    st.val_s = val_v;
    st.next = stack;
    stack = &st;
    set_value(v);
    clean_code();
    if (um) {
        p_flags &= ~IDF_UNKNOWN;
    }
}

void Alias::pop_arg() {
    if (!stack) {
        return;
    }
    IdentStack *st = stack;
    cleanup_value();
    set_value(*stack);
    clean_code();
    stack = st->next;
}

void Alias::undo_arg(IdentStack &st) {
    IdentStack *prev = stack;
    st.val_s = val_v;
    st.next = prev;
    stack = prev->next;
    set_value(*prev);
    clean_code();
}

void Alias::redo_arg(IdentStack const &st) {
    IdentStack *prev = st.next;
    prev->val_s = val_v;
    stack = prev;
    set_value(st);
    clean_code();
}

void Alias::set_arg(CsState &cs, CsValue &v) {
    if (cs.p_stack->usedargs & (1 << get_index())) {
        cleanup_value();
        set_value(v);
        clean_code();
    } else {
        push_arg(v, cs.p_stack->argstack[get_index()], false);
        cs.p_stack->usedargs |= 1 << get_index();
    }
}

void Alias::set_alias(CsState &cs, CsValue &v) {
    cleanup_value();
    set_value(v);
    clean_code();
    p_flags = (p_flags & cs.identflags) | cs.identflags;
}

IdentType Ident::get_type() const {
    if (p_type > ID_ALIAS) {
        return IdentType::special;
    }
    return IdentType(p_type);
}

ostd::ConstCharRange Ident::get_name() const {
    return p_name;
}

int Ident::get_flags() const {
    return p_flags;
}

int Ident::get_index() const {
    return p_index;
}

template<typename SF, typename RF, typename CF>
static inline bool cs_override_var(
    CsState &cs, Var *v, int &vflags, SF sf, RF rf, CF cf
) {
    if ((cs.identflags & IDF_OVERRIDDEN) || (vflags & IDF_OVERRIDE)) {
        if (vflags & IDF_PERSIST) {
            cs_debug_code(
                cs, "cannot override persistent variable '%s'", v->get_name()
            );
            return false;
        }
        if (!(vflags & IDF_OVERRIDDEN)) {
            sf();
            vflags |= IDF_OVERRIDDEN;
        } else {
            cf();
        }
    } else {
        if (vflags & IDF_OVERRIDDEN) {
            rf();
            vflags &= ~IDF_OVERRIDDEN;
        }
        cf();
    }
    return true;
}

void CsState::set_var_int(
    ostd::ConstCharRange name, CsInt v, bool dofunc, bool doclamp
) {
    Ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return;
    }
    Ivar *iv = static_cast<Ivar *>(id);
    bool success = cs_override_var(
        *this, iv, iv->p_flags,
        [&iv]() { iv->p_overrideval = *iv->p_storage; },
        []() {}, []() {}
    );
    if (!success) {
        return;
    }
    if (doclamp) {
        *iv->p_storage = ostd::clamp(v, iv->get_val_min(), iv->get_val_max());
    } else {
        *iv->p_storage = v;
    }
    if (dofunc) {
        iv->changed();
    }
}

void CsState::set_var_float(
    ostd::ConstCharRange name, CsFloat v, bool dofunc, bool doclamp
) {
    Ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return;
    }
    Fvar *fv = static_cast<Fvar *>(id);
    bool success = cs_override_var(
        *this, fv, fv->p_flags,
        [&fv]() { fv->p_overrideval = *fv->p_storage; },
        []() {}, []() {}
    );
    if (!success) {
        return;
    }
    if (doclamp) {
        *fv->p_storage = ostd::clamp(v, fv->get_val_min(), fv->get_val_max());
    } else {
        *fv->p_storage = v;
    }
    if (dofunc) {
        fv->changed();
    }
}

void CsState::set_var_str(
    ostd::ConstCharRange name, ostd::ConstCharRange v, bool dofunc
) {
    Ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return;
    }
    Svar *sv = static_cast<Svar *>(id);
    bool success = cs_override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = *sv->p_storage; },
        [&sv]() { delete[] sv->p_overrideval; },
        [&sv]() { delete[] *sv->p_storage; }
    );
    if (!success) {
        return;
    }
    *sv->p_storage = cs_dup_ostr(v);
    if (dofunc) {
        sv->changed();
    }
}

ostd::Maybe<CsInt> CsState::get_var_int(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return *static_cast<Ivar *>(id)->p_storage;
}

ostd::Maybe<CsFloat> CsState::get_var_float(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return *static_cast<Fvar *>(id)->p_storage;
}

ostd::Maybe<ostd::String> CsState::get_var_str(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return ostd::nothing;
    }
    return ostd::String(*static_cast<Svar *>(id)->p_storage);
}

ostd::Maybe<CsInt> CsState::get_var_min_int(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return static_cast<Ivar *>(id)->get_val_min();
}

ostd::Maybe<CsInt> CsState::get_var_max_int(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return static_cast<Ivar *>(id)->get_val_max();
}

ostd::Maybe<CsFloat> CsState::get_var_min_float(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return static_cast<Fvar *>(id)->get_val_min();
}

ostd::Maybe<CsFloat> CsState::get_var_max_float(ostd::ConstCharRange name) {
    Ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return static_cast<Fvar *>(id)->get_val_max();
}

ostd::Maybe<ostd::String>
CsState::get_alias_val(ostd::ConstCharRange name) {
    Alias *a = get_alias(name);
    if (!a) {
        return ostd::nothing;
    }
    if (
        (a->get_index() < MaxArguments) &&
        !(p_stack->usedargs & (1 << a->get_index()))
    ) {
        return ostd::nothing;
    }
    return ostd::move(a->val_v.get_str());
}

CsInt cs_clamp_var(CsState &cs, Ivar *iv, CsInt v) {
    if (v < iv->get_val_min()) {
        v = iv->get_val_min();
    } else if (v > iv->get_val_max()) {
        v = iv->get_val_max();
    } else {
        return v;
    }
    cs_debug_code(
        cs,
        (iv->get_flags() & IDF_HEX)
            ? (
                (iv->get_val_min() <= 255)
                    ? "valid range for '%s' is %d..0x%X"
                    : "valid range for '%s' is 0x%X..0x%X"
            )
            : "valid range for '%s' is %d..%d",
        iv->get_name(), iv->get_val_min(), iv->get_val_max()
    );
    return v;
}

void CsState::set_var_int_checked(Ivar *iv, CsInt v) {
    if (iv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", iv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, iv, iv->p_flags,
        [&iv]() { iv->p_overrideval = *iv->p_storage; },
        []() {}, []() {}
    );
    if (!success) {
        return;
    }
    if ((v < iv->get_val_min()) || (v > iv->get_val_max())) {
        v = cs_clamp_var(*this, iv, v);
    }
    *iv->p_storage = v;
    iv->changed();
}

void CsState::set_var_int_checked(Ivar *iv, CsValueRange args) {
    CsInt v = args[0].force_int();
    if ((iv->get_flags() & IDF_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2) {
            v |= args[2].force_int();
        }
    }
    set_var_int_checked(iv, v);
}

CsFloat cs_clamp_fvar(CsState &cs, Fvar *fv, CsFloat v) {
    if (v < fv->get_val_min()) {
        v = fv->get_val_min();
    } else if (v > fv->get_val_max()) {
        v = fv->get_val_max();
    } else {
        return v;
    }
    cs_debug_code(
        cs, "valid range for '%s' is %s..%s", floatstr(fv->get_val_min()),
        floatstr(fv->get_val_max())
    );
    return v;
}

void CsState::set_var_float_checked(Fvar *fv, CsFloat v) {
    if (fv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", fv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, fv, fv->p_flags,
        [&fv]() { fv->p_overrideval = *fv->p_storage; },
        []() {}, []() {}
    );
    if (!success) {
        return;
    }
    if ((v < fv->get_val_min()) || (v > fv->get_val_max())) {
        v = cs_clamp_fvar(*this, fv, v);
    }
    *fv->p_storage = v;
    fv->changed();
}

void CsState::set_var_str_checked(Svar *sv, ostd::ConstCharRange v) {
    if (sv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", sv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = *sv->p_storage; },
        [&sv]() { delete[] sv->p_overrideval; },
        [&sv]() { delete[] *sv->p_storage; }
    );
    if (!success) {
        return;
    }
    *sv->p_storage = cs_dup_ostr(v);
    sv->changed();
}

static bool cs_add_command(
    CsState &cs, ostd::ConstCharRange name, ostd::ConstCharRange args,
    CmdFunc func, int type = ID_COMMAND
) {
    ostd::Uint32 argmask = 0;
    int nargs = 0;
    for (ostd::ConstCharRange fmt(args); !fmt.empty(); ++fmt) {
        switch (*fmt) {
            case 'i':
            case 'b':
            case 'f':
            case 'F':
            case 't':
            case 'T':
            case 'E':
            case 'N':
            case 'D':
                if (nargs < MaxArguments) {
                    nargs++;
                }
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
                if (nargs < MaxArguments) {
                    fmt.push_front_n(fmt.front() - '0' + 1);
                }
                break;
            case 'C':
            case 'V':
                break;
            default:
                ostd::err.writefln(
                    "builtin %s declared with illegal type: %c",
                    name, fmt.front()
                );
                return false;
        }
    }
    cs.add_ident<Command>(type, name, args, argmask, nargs, ostd::move(func));
    return true;
}

bool CsState::add_command(
    ostd::ConstCharRange name, ostd::ConstCharRange args, CmdFunc func
) {
    return cs_add_command(*this, name, args, ostd::move(func));
}

void cs_init_lib_io(CsState &cs) {
    cs_add_command(cs, "exec", "sb", [&cs](CsValueRange args, CsValue &res) {
        auto file = args[0].get_strr();
        bool ret = cs.run_file(file);
        if (!ret) {
            if (args[1].get_int()) {
                ostd::err.writefln("could not run file \"%s\"", file);
            }
            res.set_int(0);
        } else {
            res.set_int(1);
        }
    });

    cs_add_command(cs, "echo", "C", [](CsValueRange args, CsValue &) {
        ostd::writeln(args[0].get_strr());
    });
}

static inline void cs_set_iter(Alias &a, CsInt i, IdentStack &stack) {
    if (a.stack == &stack) {
        a.cleanup_value();
        a.val_v.set_int(i);
        return;
    }
    CsValue v;
    v.set_int(i);
    a.push_arg(v, stack);
}

static inline void cs_do_loop(
    CsState &cs, Ident &id, CsInt offset, CsInt n, CsInt step,
    Bytecode *cond, Bytecode *body
) {
    if (n <= 0 || !id.is_alias()) {
        return;
    }
    Alias &a = static_cast<Alias &>(id);
    IdentStack stack;
    for (CsInt i = 0; i < n; ++i) {
        cs_set_iter(a, offset + i * step, stack);
        if (cond && !cs.run_bool(cond)) {
            break;
        }
        cs.run_int(body);
    }
    a.pop_arg();
}

static inline void cs_loop_conc(
    CsState &cs, CsValue &res, Ident &id, CsInt offset, CsInt n,
    CsInt step, Bytecode *body, bool space
) {
    if (n <= 0 || !id.is_alias()) {
        return;
    }
    Alias &a = static_cast<Alias &>(id);
    IdentStack stack;
    ostd::Vector<char> s;
    for (CsInt i = 0; i < n; ++i) {
        cs_set_iter(a, offset + i * step, stack);
        CsValue v;
        cs.run_ret(body, v);
        ostd::String vstr = ostd::move(v.get_str());
        if (space && i) {
            s.push(' ');
        }
        s.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n > 0) {
        a.pop_arg();
    }
    s.push('\0');
    ostd::Size len = s.size() - 1;
    res.set_mstr(ostd::CharRange(s.disown(), len));
}

void cs_init_lib_base(CsState &cs) {
    cs_add_command(cs, "do", "e", [&cs](CsValueRange args, CsValue &res) {
        cs.run_ret(args[0].get_code(), res);
    }, ID_DO);

    cs_add_command(cs, "doargs", "e", [&cs](CsValueRange args, CsValue &res) {
        if (cs.p_stack != &cs.noalias) {
            cs_do_args(cs, [&]() { cs.run_ret(args[0].get_code(), res); });
        } else {
            cs.run_ret(args[0].get_code(), res);
        }
    }, ID_DOARGS);

    cs_add_command(cs, "if", "tee", [&cs](CsValueRange args, CsValue &res) {
        cs.run_ret((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    }, ID_IF);

    cs_add_command(cs, "result", "T", [](CsValueRange args, CsValue &res) {
        CsValue &v = args[0];
        res = v;
        v.set_null();
    }, ID_RESULT);

    cs_add_command(cs, "!", "t", [](CsValueRange args, CsValue &res) {
        res.set_int(!args[0].get_bool());
    }, ID_NOT);

    cs_add_command(cs, "&&", "E1V", [&cs](CsValueRange args, CsValue &res) {
        if (args.empty()) {
            res.set_int(1);
        } else {
            for (ostd::Size i = 0; i < args.size(); ++i) {
                if (i) {
                    res.cleanup();
                }
                if (args[i].get_type() == VAL_CODE) {
                    cs.run_ret(args[i].code, res);
                } else {
                    res = args[i];
                }
                if (!res.get_bool()) {
                    break;
                }
            }
        }
    }, ID_AND);

    cs_add_command(cs, "||", "E1V", [&cs](CsValueRange args, CsValue &res) {
        if (args.empty()) {
            res.set_int(0);
        } else {
            for (ostd::Size i = 0; i < args.size(); ++i) {
                if (i) {
                    res.cleanup();
                }
                if (args[i].get_type() == VAL_CODE) {
                    cs.run_ret(args[i].code, res);
                } else {
                    res = args[i];
                }
                if (res.get_bool()) {
                    break;
                }
            }
        }
    }, ID_OR);

    cs_add_command(cs, "?", "tTT", [](CsValueRange args, CsValue &res) {
        res.set(args[0].get_bool() ? args[1] : args[2]);
    });

    cs_add_command(cs, "cond", "ee2V", [&cs](CsValueRange args, CsValue &res) {
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

    cs_add_command(cs, "case", "ite2V", [&cs](CsValueRange args, CsValue &res) {
        CsInt val = args[0].get_int();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_int() == val)) {
                cs.run_ret(args[i + 1].code, res);
                return;
            }
        }
    });

    cs_add_command(cs, "casef", "fte2V", [&cs](CsValueRange args, CsValue &res) {
        CsFloat val = args[0].get_float();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_float() == val)) {
                cs.run_ret(args[i + 1].code, res);
                return;
            }
        }
    });

    cs_add_command(cs, "cases", "ste2V", [&cs](CsValueRange args, CsValue &res) {
        ostd::String val = args[0].get_str();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_str() == val)) {
                cs.run_ret(args[i + 1].code, res);
                return;
            }
        }
    });

    cs_add_command(cs, "pushif", "rTe", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        CsValue &v = args[1];
        Bytecode *code = args[2].get_code();
        if (!id->is_alias() || (id->get_index() < MaxArguments)) {
            return;
        }
        Alias *a = static_cast<Alias *>(id);
        if (v.get_bool()) {
            IdentStack stack;
            a->push_arg(v, stack);
            v.set_null();
            cs.run_ret(code, res);
            a->pop_arg();
        }
    });

    cs_add_command(cs, "loop", "rie", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    cs_add_command(cs, "loop+", "riie", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    cs_add_command(cs, "loop*", "riie", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    cs_add_command(cs, "loop+*", "riiie", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile", "riee", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    cs_add_command(cs, "loopwhile+", "riiee", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile*", "riiee", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    cs_add_command(cs, "loopwhile+*", "riiiee", [&cs](CsValueRange args, CsValue &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    cs_add_command(cs, "while", "ee", [&cs](CsValueRange args, CsValue &) {
        Bytecode *cond = args[0].get_code(), *body = args[1].get_code();
        while (cs.run_bool(cond)) {
            cs.run_int(body);
        }
    });

    cs_add_command(cs, "loopconcat", "rie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat+", "riie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat*", "riie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcat+*", "riiie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), true
        );
    });

    cs_add_command(cs, "loopconcatword", "rie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword+", "riie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword*", "riie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), false
        );
    });

    cs_add_command(cs, "loopconcatword+*", "riiie", [&cs](CsValueRange args, CsValue &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });

    cs_add_command(cs, "nodebug", "e", [&cs](CsValueRange args, CsValue &res) {
        ++cs.nodebug;
        cs.run_ret(args[0].get_code(), res);
        --cs.nodebug;
    });

    cs_add_command(cs, "push", "rTe", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        if (!id->is_alias() || (id->get_index() < MaxArguments)) {
            return;
        }
        Alias *a = static_cast<Alias *>(id);
        IdentStack stack;
        CsValue &v = args[1];
        a->push_arg(v, stack);
        v.set_null();
        cs.run_ret(args[2].get_code(), res);
        a->pop_arg();
    });

    cs_add_command(cs, "local", nullptr, nullptr, ID_LOCAL);

    cs_add_command(cs, "resetvar", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_int(cs.reset_var(args[0].get_strr()));
    });

    cs_add_command(cs, "alias", "sT", [&cs](CsValueRange args, CsValue &) {
        CsValue &v = args[1];
        cs.set_alias(args[0].get_strr(), v);
        v.set_null();
    });

    cs_add_command(cs, "getvarmin", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_int(cs.get_var_min_int(args[0].get_strr()).value_or(0));
    });
    cs_add_command(cs, "getvarmax", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_int(cs.get_var_max_int(args[0].get_strr()).value_or(0));
    });
    cs_add_command(cs, "getfvarmin", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_float(cs.get_var_min_float(args[0].get_strr()).value_or(0.0f));
    });
    cs_add_command(cs, "getfvarmax", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_float(cs.get_var_max_float(args[0].get_strr()).value_or(0.0f));
    });

    cs_add_command(cs, "identexists", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_int(cs.have_ident(args[0].get_strr()));
    });

    cs_add_command(cs, "getalias", "s", [&cs](CsValueRange args, CsValue &res) {
        res.set_str(ostd::move(cs.get_alias_val(args[0].get_strr()).value_or("")));
    });
}

void cs_init_lib_math(CsState &cs);
void cs_init_lib_string(CsState &cs);
void cs_init_lib_list(CsState &cs);

OSTD_EXPORT void CsState::init_libs(int libs) {
    if (libs & CS_LIB_IO) {
        cs_init_lib_io(*this);
    }
    if (libs & CS_LIB_MATH) {
        cs_init_lib_math(*this);
    }
    if (libs & CS_LIB_STRING) {
        cs_init_lib_string(*this);
    }
    if (libs & CS_LIB_LIST) {
        cs_init_lib_list(*this);
    }
}

} /* namespace cscript */
