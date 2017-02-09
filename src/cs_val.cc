#include "cubescript/cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

template<typename T, typename U>
static inline T &csv_get(U &stor) {
    /* ugly, but internal and unlikely to cause bugs */
    return const_cast<T &>(reinterpret_cast<T const &>(stor));
}

template<typename T>
static inline void csv_cleanup(CsValueType tv, T &stor) {
    switch (tv) {
        case CsValueType::String:
            delete[] csv_get<char *>(stor);
            break;
        case CsValueType::Code: {
            uint32_t *bcode = csv_get<uint32_t *>(stor);
            if (bcode[-1] == CsCodeStart) {
                delete[] &bcode[-1];
            }
            break;
        }
        default:
            break;
    }
}

CsValue::CsValue():
    p_stor(), p_len(0), p_type(CsValueType::Null)
{}

CsValue::~CsValue() {
    csv_cleanup(p_type, p_stor);
}

CsValue::CsValue(CsValue const &v): CsValue() {
    *this = v;
}

CsValue::CsValue(CsValue &&v): CsValue() {
    *this = std::move(v);
}

CsValue &CsValue::operator=(CsValue const &v) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Null;
    switch (v.get_type()) {
        case CsValueType::Int:
        case CsValueType::Float:
        case CsValueType::Ident:
            p_len = v.p_len;
            p_type = v.p_type;
            p_stor = v.p_stor;
            break;
        case CsValueType::String:
        case CsValueType::Cstring:
        case CsValueType::Macro:
            set_str(CsString{csv_get<char const *>(v.p_stor), v.p_len});
            break;
        case CsValueType::Code:
            set_code(cs_copy_code(v.get_code()));
            break;
        default:
            break;
    }
    return *this;
}

CsValue &CsValue::operator=(CsValue &&v) {
    csv_cleanup(p_type, p_stor);
    p_stor = v.p_stor;
    p_type = v.p_type;
    p_len = v.p_len;
    v.p_type = CsValueType::Null;
    return *this;
}

CsValueType CsValue::get_type() const {
    return p_type;
}

void CsValue::set_int(CsInt val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Int;
    csv_get<CsInt>(p_stor) = val;
}

void CsValue::set_float(CsFloat val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Float;
    csv_get<CsFloat>(p_stor) = val;
}

void CsValue::set_str(CsString val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::String;
    p_len = val.size();
    char *buf = new char[p_len + 1];
    memcpy(buf, val.data(), p_len + 1);
    csv_get<char *>(p_stor) = buf;
}

void CsValue::set_null() {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Null;
}

void CsValue::set_code(CsBytecode *val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Code;
    csv_get<CsBytecode *>(p_stor) = val;
}

void CsValue::set_cstr(ostd::ConstCharRange val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Cstring;
    p_len = val.size();
    csv_get<char const *>(p_stor) = val.data();
}

void CsValue::set_ident(CsIdent *val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Ident;
    csv_get<CsIdent *>(p_stor) = val;
}

void CsValue::set_macro(ostd::ConstCharRange val) {
    csv_cleanup(p_type, p_stor);
    p_type = CsValueType::Macro;
    p_len = val.size();
    csv_get<char const *>(p_stor) = val.data();
}

void CsValue::force_null() {
    if (get_type() == CsValueType::Null) {
        return;
    }
    set_null();
}

CsFloat CsValue::force_float() {
    CsFloat rf = 0.0f;
    switch (get_type()) {
        case CsValueType::Int:
            rf = csv_get<CsInt>(p_stor);
            break;
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            rf = cs_parse_float(ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
            break;
        case CsValueType::Float:
            return csv_get<CsFloat>(p_stor);
        default:
            break;
    }
    set_float(rf);
    return rf;
}

CsInt CsValue::force_int() {
    CsInt ri = 0;
    switch (get_type()) {
        case CsValueType::Float:
            ri = csv_get<CsFloat>(p_stor);
            break;
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            ri = cs_parse_int(ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
            break;
        case CsValueType::Int:
            return csv_get<CsInt>(p_stor);
        default:
            break;
    }
    set_int(ri);
    return ri;
}

ostd::ConstCharRange CsValue::force_str() {
    CsString rs;
    switch (get_type()) {
        case CsValueType::Float:
            rs = floatstr(csv_get<CsFloat>(p_stor));
            break;
        case CsValueType::Int:
            rs = intstr(csv_get<CsInt>(p_stor));
            break;
        case CsValueType::Macro:
        case CsValueType::Cstring:
            rs = ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            );
            break;
        case CsValueType::String:
            return ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            );
        default:
            break;
    }
    set_str(std::move(rs));
    return ostd::ConstCharRange(
        csv_get<char const *>(p_stor),
        csv_get<char const *>(p_stor) + p_len
    );
}

CsInt CsValue::get_int() const {
    switch (get_type()) {
        case CsValueType::Float:
            return CsInt(csv_get<CsFloat>(p_stor));
        case CsValueType::Int:
            return csv_get<CsInt>(p_stor);
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            return cs_parse_int(ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            break;
    }
    return 0;
}

CsFloat CsValue::get_float() const {
    switch (get_type()) {
        case CsValueType::Float:
            return csv_get<CsFloat>(p_stor);
        case CsValueType::Int:
            return CsFloat(csv_get<CsInt>(p_stor));
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            return cs_parse_float(ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            break;
    }
    return 0.0f;
}

CsBytecode *CsValue::get_code() const {
    if (get_type() != CsValueType::Code) {
        return nullptr;
    }
    return csv_get<CsBytecode *>(p_stor);
}

CsIdent *CsValue::get_ident() const {
    if (get_type() != CsValueType::Ident) {
        return nullptr;
    }
    return csv_get<CsIdent *>(p_stor);
}

CsString CsValue::get_str() const {
    switch (get_type()) {
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            return CsString{csv_get<char const *>(p_stor), p_len};
        case CsValueType::Int:
            return intstr(csv_get<CsInt>(p_stor));
        case CsValueType::Float:
            return floatstr(csv_get<CsFloat>(p_stor));
        default:
            break;
    }
    return CsString("");
}

ostd::ConstCharRange CsValue::get_strr() const {
    switch (get_type()) {
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            return ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor)+ p_len
            );
        default:
            break;
    }
    return ostd::ConstCharRange();
}

void CsValue::get_val(CsValue &r) const {
    switch (get_type()) {
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            r.set_str(
                CsString{csv_get<char const *>(p_stor), p_len}
            );
            break;
        case CsValueType::Int:
            r.set_int(csv_get<CsInt>(p_stor));
            break;
        case CsValueType::Float:
            r.set_float(csv_get<CsFloat>(p_stor));
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
        *reinterpret_cast<uint32_t *>(code) & CsCodeOpMask
    ) == CsCodeExit;
}

bool CsValue::code_is_empty() const {
    if (get_type() != CsValueType::Code) {
        return true;
    }
    return cscript::cs_code_is_empty(csv_get<CsBytecode *>(p_stor));
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
        case CsValueType::Float:
            return csv_get<CsFloat>(p_stor) != 0;
        case CsValueType::Int:
            return csv_get<CsInt>(p_stor) != 0;
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            return cs_get_bool(ostd::ConstCharRange(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            return false;
    }
}

/* stacked value for easy stack management */

CsStackedValue::CsStackedValue(CsIdent *id):
    CsValue(), p_a(nullptr), p_stack(), p_pushed(false)
{
    set_alias(id);
}

CsStackedValue::~CsStackedValue() {
    pop();
    static_cast<CsValue *>(this)->~CsValue();
}

CsStackedValue &CsStackedValue::operator=(CsValue const &v) {
    *static_cast<CsValue *>(this) = v;
    return *this;
}

CsStackedValue &CsStackedValue::operator=(CsValue &&v) {
    *static_cast<CsValue *>(this) = std::move(v);
    return *this;
}

bool CsStackedValue::set_alias(CsIdent *id) {
    if (!id || !id->is_alias()) {
        return false;
    }
    p_a = static_cast<CsAlias *>(id);
    return true;
}

CsAlias *CsStackedValue::get_alias() const {
    return p_a;
}

bool CsStackedValue::has_alias() const {
    return p_a != nullptr;
}

bool CsStackedValue::push() {
    if (!p_a) {
        return false;
    }
    CsAliasInternal::push_arg(p_a, *this, p_stack);
    p_pushed = true;
    return true;
}

bool CsStackedValue::pop() {
    if (!p_pushed || !p_a) {
        return false;
    }
    CsAliasInternal::pop_arg(p_a);
    p_pushed = false;
    return true;
}

} /* namespace cscript */
