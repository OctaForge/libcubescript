#include "cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

CsString intstr(CsInt v) {
    char buf[256];
    snprintf(buf, sizeof(buf), IntFormat, v);
    return buf;
}

CsString floatstr(CsFloat v) {
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

CsIdent::CsIdent(CsIdentType tp, ostd::ConstCharRange nm, int fl):
    p_name(nm), p_type(int(tp)), p_flags(fl)
{}

CsVar::CsVar(CsIdentType tp, ostd::ConstCharRange name, CsVarCb f, int fl):
    CsIdent(tp, name, fl), cb_var(ostd::move(f))
{}

CsIvar::CsIvar(
    ostd::ConstCharRange name, CsInt m, CsInt x, CsInt v, CsVarCb f, int fl
):
    CsVar(CsIdentType::ivar, name, ostd::move(f), fl | ((m > x) ? IDF_READONLY : 0)),
    p_storage(v), p_minval(m), p_maxval(x), p_overrideval(0)
{}

CsFvar::CsFvar(
    ostd::ConstCharRange name, CsFloat m, CsFloat x, CsFloat v, CsVarCb f, int fl
):
    CsVar(CsIdentType::fvar, name, ostd::move(f), fl | ((m > x) ? IDF_READONLY : 0)),
    p_storage(v), p_minval(m), p_maxval(x), p_overrideval(0)
{}

CsSvar::CsSvar(ostd::ConstCharRange name, ostd::ConstCharRange v, CsVarCb f, int fl):
    CsVar(CsIdentType::svar, name, ostd::move(f), fl),
    p_storage(v), p_overrideval()
{}
CsSvar::CsSvar(ostd::ConstCharRange name, CsString &&v, CsVarCb f, int fl):
    CsVar(CsIdentType::svar, name, ostd::move(f), fl),
    p_storage(ostd::forward<CsString &&>(v)), p_overrideval()
{}

CsAlias::CsAlias(ostd::ConstCharRange name, char *a, int fl):
    CsIdent(CsIdentType::alias, name, fl),
    p_acode(nullptr), p_astack(nullptr)
{
    val_v.set_mstr(a);
}
CsAlias::CsAlias(ostd::ConstCharRange name, CsInt a, int fl):
    CsIdent(CsIdentType::alias, name, fl),
    p_acode(nullptr), p_astack(nullptr)
{
    val_v.set_int(a);
}
CsAlias::CsAlias(ostd::ConstCharRange name, CsFloat a, int fl):
    CsIdent(CsIdentType::alias, name, fl),
    p_acode(nullptr), p_astack(nullptr)
{
    val_v.set_float(a);
}
CsAlias::CsAlias(ostd::ConstCharRange name, int fl):
    CsIdent(CsIdentType::alias, name, fl),
    p_acode(nullptr), p_astack(nullptr)
{
    val_v.set_null();
}
CsAlias::CsAlias(ostd::ConstCharRange name, CsValue const &v, int fl):
    CsIdent(CsIdentType::alias, name, fl),
    val_v(v), p_acode(nullptr), p_astack(nullptr)
{}

Command::Command(
    int tp, ostd::ConstCharRange name, ostd::ConstCharRange args,
    ostd::Uint32 amask, int nargs, CmdFunc f
):
    CsIdent(CsIdentType::command, name, 0),
    cargs(!args.empty() ? cs_dup_ostr(args) : nullptr),
    argmask(amask), numargs(nargs), cb_cftv(ostd::move(f))
{
    p_type = tp;
}

bool CsIdent::is_alias() const {
    return get_type() == CsIdentType::alias;
}

CsAlias *CsIdent::get_alias() {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<CsAlias *>(this);
}

CsAlias const *CsIdent::get_alias() const {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<CsAlias const *>(this);
}

bool CsIdent::is_command() const {
    return get_type() == CsIdentType::command;
}

bool CsIdent::is_special() const {
    return get_type() == CsIdentType::special;
}

bool CsIdent::is_var() const {
    CsIdentType tp = get_type();
    return (tp >= CsIdentType::ivar) && (tp <= CsIdentType::svar);
}

CsVar *CsIdent::get_var() {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<CsVar *>(this);
}

CsVar const *CsIdent::get_var() const {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<CsVar const *>(this);
}

bool CsIdent::is_ivar() const {
    return get_type() == CsIdentType::ivar;
}

CsIvar *CsIdent::get_ivar() {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<CsIvar *>(this);
}

CsIvar const *CsIdent::get_ivar() const {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<CsIvar const *>(this);
}

bool CsIdent::is_fvar() const {
    return get_type() == CsIdentType::fvar;
}

CsFvar *CsIdent::get_fvar() {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<CsFvar *>(this);
}

CsFvar const *CsIdent::get_fvar() const {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<CsFvar const *>(this);
}

bool CsIdent::is_svar() const {
    return get_type() == CsIdentType::svar;
}

CsSvar *CsIdent::get_svar() {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<CsSvar *>(this);
}

CsSvar const *CsIdent::get_svar() const {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<CsSvar const *>(this);
}

CsInt CsIvar::get_val_min() const {
    return p_minval;
}
CsInt CsIvar::get_val_max() const {
    return p_maxval;
}

CsInt CsIvar::get_value() const {
    return p_storage;
}
void CsIvar::set_value(CsInt val) {
    p_storage = val;
}

CsFloat CsFvar::get_val_min() const {
    return p_minval;
}
CsFloat CsFvar::get_val_max() const {
    return p_maxval;
}

CsFloat CsFvar::get_value() const {
    return p_storage;
}
void CsFvar::set_value(CsFloat val) {
    p_storage = val;
}

ostd::ConstCharRange CsSvar::get_value() const {
    return p_storage.iter();
}
void CsSvar::set_value(ostd::ConstCharRange val) {
    p_storage = val;
}
void CsSvar::set_value(CsString &&val) {
    p_storage = ostd::forward<CsString &&>(val);
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
    CsIdent *id = new_ident("//dummy");
    assert(id->get_index() == DummyIdx);

    id = add_ident<CsIvar>("numargs", MaxArguments, 0, 0);
    assert(id->get_index() == NumargsIdx);

    id = add_ident<CsIvar>("dbgalias", 0, 1000, 4);
    assert(id->get_index() == DbgaliasIdx);

    cs_init_lib_base(*this);
}

CsState::~CsState() {
    for (auto &p: idents.iter()) {
        CsIdent *i = p.second;
        CsAlias *a = i->get_alias();
        if (a) {
            a->force_null();
            a->clean_code();
        } else if (i->is_command() || i->is_special()) {
            delete[] static_cast<Command *>(i)->cargs;
        }
        delete i;
    }
}

void CsState::clear_override(CsIdent &id) {
    if (!(id.get_flags() & IDF_OVERRIDDEN)) {
        return;
    }
    switch (id.get_type()) {
        case CsIdentType::alias: {
            CsAlias &a = static_cast<CsAlias &>(id);
            a.cleanup_value();
            a.clean_code();
            a.set_value_str("");
            break;
        }
        case CsIdentType::ivar: {
            CsIvar &iv = static_cast<CsIvar &>(id);
            iv.set_value(iv.p_overrideval);
            iv.changed();
            break;
        }
        case CsIdentType::fvar: {
            CsFvar &fv = static_cast<CsFvar &>(id);
            fv.set_value(fv.p_overrideval);
            fv.changed();
            break;
        }
        case CsIdentType::svar: {
            CsSvar &sv = static_cast<CsSvar &>(id);
            sv.set_value(sv.p_overrideval);
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

CsIdent *CsState::add_ident(CsIdent *id) {
    if (!id) {
        return nullptr;
    }
    idents[id->get_name()] = id;
    id->p_index = identmap.size();
    return identmap.push(id);
}

CsIdent *CsState::new_ident(ostd::ConstCharRange name, int flags) {
    CsIdent *id = get_ident(name);
    if (!id) {
        if (cs_check_num(name)) {
            cs_debug_code(
                *this, "number %s is not a valid identifier name", name
            );
            return identmap[DummyIdx];
        }
        id = add_ident<CsAlias>(name, flags);
    }
    return id;
}

CsIdent *CsState::force_ident(CsValue &v) {
    switch (v.get_type()) {
        case VAL_IDENT:
            return v.get_ident();
        case VAL_MACRO:
        case VAL_CSTR:
        case VAL_STR: {
            CsIdent *id = new_ident(v.get_strr());
            v.cleanup();
            v.set_ident(id);
            return id;
        }
    }
    v.cleanup();
    v.set_ident(identmap[DummyIdx]);
    return identmap[DummyIdx];
}

bool CsState::reset_var(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
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
    CsIdent *id = get_ident(name);
    if (id && id->is_var()) {
        static_cast<CsVar *>(id)->changed();
    }
}

void CsState::set_alias(ostd::ConstCharRange name, CsValue &v) {
    CsIdent *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case CsIdentType::alias: {
                CsAlias *a = static_cast<CsAlias *>(id);
                if (a->get_index() < MaxArguments) {
                    a->set_arg(*this, v);
                } else {
                    a->set_alias(*this, v);
                }
                return;
            }
            case CsIdentType::ivar:
                set_var_int_checked(static_cast<CsIvar *>(id), v.get_int());
                break;
            case CsIdentType::fvar:
                set_var_float_checked(static_cast<CsFvar *>(id), v.get_float());
                break;
            case CsIdentType::svar:
                set_var_str_checked(static_cast<CsSvar *>(id), v.get_str());
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
        add_ident<CsAlias>(name, v, identflags);
    }
}

void CsState::print_var_int(CsIvar *iv, CsInt i) {
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

void CsState::print_var_float(CsFvar *fv, CsFloat f) {
    writefln("%s = %s", fv->get_name(), floatstr(f));
}

void CsState::print_var_str(CsSvar *sv, ostd::ConstCharRange s) {
    if (ostd::find(s, '"').empty()) {
        writefln("%s = \"%s\"", sv->get_name(), s);
    } else {
        writefln("%s = [%s]", sv->get_name(), s);
    }
}

void CsState::print_var(CsVar *v) {
    switch (v->get_type()) {
        case CsIdentType::ivar: {
            CsIvar *iv = static_cast<CsIvar *>(v);
            print_var_int(iv, iv->get_value());
            break;
        }
        case CsIdentType::fvar: {
            CsFvar *fv = static_cast<CsFvar *>(v);
            print_var_float(fv, fv->get_value());
            break;
        }
        case CsIdentType::svar: {
            CsSvar *sv = static_cast<CsSvar *>(v);
            print_var_str(sv, sv->get_value());
            break;
        }
        default:
            break;
    }
}

void CsValue::cleanup() {
    switch (get_type()) {
        case VAL_STR:
            delete[] p_s;
            break;
        case VAL_CODE:
            ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(p_code);
            if (bcode[-1] == CODE_START) {
                delete[] bcode;
            }
            break;
    }
}

int CsValue::get_type() const {
    return p_type;
}

void CsValue::set_int(CsInt val) {
    p_type = VAL_INT;
    p_i = val;
}

void CsValue::set_float(CsFloat val) {
    p_type = VAL_FLOAT;
    p_f = val;
}

void CsValue::set_str(CsString val) {
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

void CsValue::set_null() {
    p_type = VAL_NULL;
    p_code = nullptr;
}

void CsValue::set_code(CsBytecode *val) {
    p_type = VAL_CODE;
    p_code = val;
}

void CsValue::set_cstr(ostd::ConstCharRange val) {
    p_type = VAL_CSTR;
    p_len = val.size();
    p_cstr = val.data();
}

void CsValue::set_mstr(ostd::CharRange val) {
    p_type = VAL_STR;
    p_len = val.size();
    p_s = val.data();
}

void CsValue::set_ident(CsIdent *val) {
    p_type = VAL_IDENT;
    p_id = val;
}

void CsValue::set_macro(ostd::ConstCharRange val) {
    p_type = VAL_MACRO;
    p_len = val.size();
    p_cstr = val.data();
}

void CsValue::set(CsValue &tv) {
    *this = tv;
    tv.p_type = VAL_NULL;
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
            rf = p_i;
            break;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            rf = cs_parse_float(ostd::ConstCharRange(p_s, p_len));
            break;
        case VAL_FLOAT:
            return p_f;
    }
    cleanup();
    set_float(rf);
    return rf;
}

CsInt CsValue::force_int() {
    CsInt ri = 0;
    switch (get_type()) {
        case VAL_FLOAT:
            ri = p_f;
            break;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            ri = cs_parse_int(ostd::ConstCharRange(p_s, p_len));
            break;
        case VAL_INT:
            return p_i;
    }
    cleanup();
    set_int(ri);
    return ri;
}

ostd::ConstCharRange CsValue::force_str() {
    CsString rs;
    switch (get_type()) {
        case VAL_FLOAT:
            rs = ostd::move(floatstr(p_f));
            break;
        case VAL_INT:
            rs = ostd::move(intstr(p_i));
            break;
        case VAL_MACRO:
        case VAL_CSTR:
            rs = ostd::ConstCharRange(p_s, p_len);
            break;
        case VAL_STR:
            return ostd::ConstCharRange(p_s, p_len);
    }
    cleanup();
    set_str(ostd::move(rs));
    return ostd::ConstCharRange(p_s, p_len);
}

CsInt CsValue::get_int() const {
    switch (get_type()) {
        case VAL_FLOAT:
            return CsInt(p_f);
        case VAL_INT:
            return p_i;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return cs_parse_int(ostd::ConstCharRange(p_s, p_len));
    }
    return 0;
}

CsFloat CsValue::get_float() const {
    switch (get_type()) {
        case VAL_FLOAT:
            return p_f;
        case VAL_INT:
            return CsFloat(p_i);
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return cs_parse_float(ostd::ConstCharRange(p_s, p_len));
    }
    return 0.0f;
}

CsBytecode *CsValue::get_code() const {
    if (get_type() != VAL_CODE) {
        return nullptr;
    }
    return p_code;
}

CsIdent *CsValue::get_ident() const {
    if (get_type() != VAL_IDENT) {
        return nullptr;
    }
    return p_id;
}

CsString CsValue::get_str() const {
    switch (get_type()) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return ostd::ConstCharRange(p_s, p_len);
    case VAL_INT:
        return intstr(p_i);
    case VAL_FLOAT:
        return floatstr(p_f);
    }
    return CsString("");
}

ostd::ConstCharRange CsValue::get_strr() const {
    switch (get_type()) {
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return ostd::ConstCharRange(p_s, p_len);
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
            r.set_str(ostd::ConstCharRange(p_s, p_len));
            break;
        }
        case VAL_INT:
            r.set_int(p_i);
            break;
        case VAL_FLOAT:
            r.set_float(p_f);
            break;
        default:
            r.set_null();
            break;
    }
}

OSTD_EXPORT bool cs_code_is_empty(CsBytecode *code) {
    if (!code) {
        return true;
    }
    return (
        *reinterpret_cast<ostd::Uint32 *>(code) & CODE_OP_MASK
    ) == CODE_EXIT;
}

bool CsValue::code_is_empty() const {
    if (get_type() != VAL_CODE) {
        return true;
    }
    return cscript::cs_code_is_empty(p_code);
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
            return p_f != 0;
        case VAL_INT:
            return p_i != 0;
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            return cs_get_bool(ostd::ConstCharRange(p_s, p_len));
        default:
            return false;
    }
}

void CsAlias::get_cstr(CsValue &v) const {
    switch (val_v.get_type()) {
        case VAL_MACRO:
            v.set_macro(val_v.get_strr());
            break;
        case VAL_STR:
        case VAL_CSTR:
            v.set_cstr(val_v.get_strr());
            break;
        case VAL_INT:
            v.set_str(ostd::move(intstr(val_v.get_int())));
            break;
        case VAL_FLOAT:
            v.set_str(ostd::move(floatstr(val_v.get_float())));
            break;
        default:
            v.set_cstr("");
            break;
    }
}

void CsAlias::get_cval(CsValue &v) const {
    switch (val_v.get_type()) {
        case VAL_MACRO:
            v.set_macro(val_v.get_strr());
            break;
        case VAL_STR:
        case VAL_CSTR:
            v.set_cstr(val_v.get_strr());
            break;
        case VAL_INT:
            v.set_int(val_v.get_int());
            break;
        case VAL_FLOAT:
            v.set_float(val_v.get_float());
            break;
        default:
            v.set_null();
            break;
    }
}

void CsAlias::clean_code() {
    ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(p_acode);
    if (bcode) {
        bcode_decr(bcode);
        p_acode = nullptr;
    }
}

CsBytecode *CsAlias::compile_code(CsState &cs) {
    if (!p_acode) {
        p_acode = reinterpret_cast<CsBytecode *>(compilecode(cs, val_v.get_str()));
    }
    return p_acode;
}

void CsAlias::push_arg(CsValue const &v, CsIdentStack &st, bool um) {
    if (p_astack == &st) {
        /* prevent cycles and unnecessary code elsewhere */
        cleanup_value();
        set_value(v);
        clean_code();
        return;
    }
    st.val_s = val_v;
    st.next = p_astack;
    p_astack = &st;
    set_value(v);
    clean_code();
    if (um) {
        p_flags &= ~IDF_UNKNOWN;
    }
}

void CsAlias::pop_arg() {
    if (!p_astack) {
        return;
    }
    CsIdentStack *st = p_astack;
    cleanup_value();
    set_value(*p_astack);
    clean_code();
    p_astack = st->next;
}

void CsAlias::undo_arg(CsIdentStack &st) {
    CsIdentStack *prev = p_astack;
    st.val_s = val_v;
    st.next = prev;
    p_astack = prev->next;
    set_value(*prev);
    clean_code();
}

void CsAlias::redo_arg(CsIdentStack const &st) {
    CsIdentStack *prev = st.next;
    prev->val_s = val_v;
    p_astack = prev;
    set_value(st);
    clean_code();
}

void CsAlias::set_arg(CsState &cs, CsValue &v) {
    if (cs.p_stack->usedargs & (1 << get_index())) {
        cleanup_value();
        set_value(v);
        clean_code();
    } else {
        push_arg(v, cs.p_stack->argstack[get_index()], false);
        cs.p_stack->usedargs |= 1 << get_index();
    }
}

void CsAlias::set_alias(CsState &cs, CsValue &v) {
    cleanup_value();
    set_value(v);
    clean_code();
    p_flags = (p_flags & cs.identflags) | cs.identflags;
}

CsIdentType CsIdent::get_type() const {
    if (p_type > ID_ALIAS) {
        return CsIdentType::special;
    }
    return CsIdentType(p_type);
}

ostd::ConstCharRange CsIdent::get_name() const {
    return p_name;
}

int CsIdent::get_flags() const {
    return p_flags;
}

int CsIdent::get_index() const {
    return p_index;
}

template<typename SF>
static inline bool cs_override_var(CsState &cs, CsVar *v, int &vflags, SF sf) {
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
        }
    } else {
        if (vflags & IDF_OVERRIDDEN) {
            vflags &= ~IDF_OVERRIDDEN;
        }
    }
    return true;
}

void CsState::set_var_int(
    ostd::ConstCharRange name, CsInt v, bool dofunc, bool doclamp
) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return;
    }
    CsIvar *iv = static_cast<CsIvar *>(id);
    bool success = cs_override_var(
        *this, iv, iv->p_flags,
        [&iv]() { iv->p_overrideval = iv->get_value(); }
    );
    if (!success) {
        return;
    }
    if (doclamp) {
        iv->set_value(ostd::clamp(v, iv->get_val_min(), iv->get_val_max()));
    } else {
        iv->set_value(v);
    }
    if (dofunc) {
        iv->changed();
    }
}

void CsState::set_var_float(
    ostd::ConstCharRange name, CsFloat v, bool dofunc, bool doclamp
) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return;
    }
    CsFvar *fv = static_cast<CsFvar *>(id);
    bool success = cs_override_var(
        *this, fv, fv->p_flags,
        [&fv]() { fv->p_overrideval = fv->get_value(); }
    );
    if (!success) {
        return;
    }
    if (doclamp) {
        fv->set_value(ostd::clamp(v, fv->get_val_min(), fv->get_val_max()));
    } else {
        fv->set_value(v);
    }
    if (dofunc) {
        fv->changed();
    }
}

void CsState::set_var_str(
    ostd::ConstCharRange name, ostd::ConstCharRange v, bool dofunc
) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_svar()) {
        return;
    }
    CsSvar *sv = static_cast<CsSvar *>(id);
    bool success = cs_override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = sv->get_value(); }
    );
    if (!success) {
        return;
    }
    sv->set_value(v);
    if (dofunc) {
        sv->changed();
    }
}

ostd::Maybe<CsInt> CsState::get_var_int(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return static_cast<CsIvar *>(id)->get_value();
}

ostd::Maybe<CsFloat> CsState::get_var_float(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return static_cast<CsFvar *>(id)->get_value();
}

ostd::Maybe<CsString> CsState::get_var_str(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_svar()) {
        return ostd::nothing;
    }
    return CsString(static_cast<CsSvar *>(id)->get_value());
}

ostd::Maybe<CsInt> CsState::get_var_min_int(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return static_cast<CsIvar *>(id)->get_val_min();
}

ostd::Maybe<CsInt> CsState::get_var_max_int(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return ostd::nothing;
    }
    return static_cast<CsIvar *>(id)->get_val_max();
}

ostd::Maybe<CsFloat> CsState::get_var_min_float(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return static_cast<CsFvar *>(id)->get_val_min();
}

ostd::Maybe<CsFloat> CsState::get_var_max_float(ostd::ConstCharRange name) {
    CsIdent *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return ostd::nothing;
    }
    return static_cast<CsFvar *>(id)->get_val_max();
}

ostd::Maybe<CsString>
CsState::get_alias_val(ostd::ConstCharRange name) {
    CsAlias *a = get_alias(name);
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

CsInt cs_clamp_var(CsState &cs, CsIvar *iv, CsInt v) {
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

void CsState::set_var_int_checked(CsIvar *iv, CsInt v) {
    if (iv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", iv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, iv, iv->p_flags,
        [&iv]() { iv->p_overrideval = iv->get_value(); }
    );
    if (!success) {
        return;
    }
    if ((v < iv->get_val_min()) || (v > iv->get_val_max())) {
        v = cs_clamp_var(*this, iv, v);
    }
    iv->set_value(v);
    iv->changed();
}

void CsState::set_var_int_checked(CsIvar *iv, CsValueRange args) {
    CsInt v = args[0].force_int();
    if ((iv->get_flags() & IDF_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2) {
            v |= args[2].force_int();
        }
    }
    set_var_int_checked(iv, v);
}

CsFloat cs_clamp_fvar(CsState &cs, CsFvar *fv, CsFloat v) {
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

void CsState::set_var_float_checked(CsFvar *fv, CsFloat v) {
    if (fv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", fv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, fv, fv->p_flags,
        [&fv]() { fv->p_overrideval = fv->get_value(); }
    );
    if (!success) {
        return;
    }
    if ((v < fv->get_val_min()) || (v > fv->get_val_max())) {
        v = cs_clamp_fvar(*this, fv, v);
    }
    fv->set_value(v);
    fv->changed();
}

void CsState::set_var_str_checked(CsSvar *sv, ostd::ConstCharRange v) {
    if (sv->get_flags() & IDF_READONLY) {
        cs_debug_code(*this, "variable '%s' is read only", sv->get_name());
        return;
    }
    bool success = cs_override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = sv->get_value(); }
    );
    if (!success) {
        return;
    }
    sv->set_value(v);
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

static inline void cs_set_iter(CsAlias &a, CsInt i, CsIdentStack &stack) {
    CsValue v;
    v.set_int(i);
    a.push_arg(v, stack);
}

static inline void cs_do_loop(
    CsState &cs, CsIdent &id, CsInt offset, CsInt n, CsInt step,
    CsBytecode *cond, CsBytecode *body
) {
    if (n <= 0 || !id.is_alias()) {
        return;
    }
    CsAlias &a = static_cast<CsAlias &>(id);
    CsIdentStack stack;
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
    CsState &cs, CsValue &res, CsIdent &id, CsInt offset, CsInt n,
    CsInt step, CsBytecode *body, bool space
) {
    if (n <= 0 || !id.is_alias()) {
        return;
    }
    CsAlias &a = static_cast<CsAlias &>(id);
    CsIdentStack stack;
    CsVector<char> s;
    for (CsInt i = 0; i < n; ++i) {
        cs_set_iter(a, offset + i * step, stack);
        CsValue v;
        cs.run_ret(body, v);
        CsString vstr = ostd::move(v.get_str());
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
                CsBytecode *code = args[i].get_code();
                if (code) {
                    cs.run_ret(code, res);
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
                CsBytecode *code = args[i].get_code();
                if (code) {
                    cs.run_ret(code, res);
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
                if (cs.run_bool(args[i].get_code())) {
                    cs.run_ret(args[i + 1].get_code(), res);
                    break;
                }
            } else {
                cs.run_ret(args[i].get_code(), res);
                break;
            }
        }
    });

    cs_add_command(cs, "case", "ite2V", [&cs](CsValueRange args, CsValue &res) {
        CsInt val = args[0].get_int();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_int() == val)) {
                cs.run_ret(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    cs_add_command(cs, "casef", "fte2V", [&cs](CsValueRange args, CsValue &res) {
        CsFloat val = args[0].get_float();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_float() == val)) {
                cs.run_ret(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    cs_add_command(cs, "cases", "ste2V", [&cs](CsValueRange args, CsValue &res) {
        CsString val = args[0].get_str();
        for (ostd::Size i = 1; (i + 1) < args.size(); i += 2) {
            if ((args[i].get_type() == VAL_NULL) || (args[i].get_str() == val)) {
                cs.run_ret(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    cs_add_command(cs, "pushif", "rTe", [&cs](CsValueRange args, CsValue &res) {
        CsIdent *id = args[0].get_ident();
        CsValue &v = args[1];
        CsBytecode *code = args[2].get_code();
        if (!id->is_alias() || (id->get_index() < MaxArguments)) {
            return;
        }
        CsAlias *a = static_cast<CsAlias *>(id);
        if (v.get_bool()) {
            CsIdentStack stack;
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
        CsBytecode *cond = args[0].get_code(), *body = args[1].get_code();
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
        CsIdent *id = args[0].get_ident();
        if (!id->is_alias() || (id->get_index() < MaxArguments)) {
            return;
        }
        CsAlias *a = static_cast<CsAlias *>(id);
        CsIdentStack stack;
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
