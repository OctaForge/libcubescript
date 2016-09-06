#include "cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

template<typename T, typename U>
inline T &csv_get(U &stor) {
    /* ugly, but internal and unlikely to cause bugs */
    return const_cast<T &>(reinterpret_cast<T const &>(stor));
}

void CsValue::cleanup() {
    switch (get_type()) {
        case CsValueType::string:
            delete[] csv_get<char *>(p_stor);
            break;
        case CsValueType::code: {
            ostd::Uint32 *bcode = csv_get<ostd::Uint32 *>(p_stor);
            if (bcode[-1] == CODE_START) {
                delete[] bcode;
            }
            break;
        }
        default:
            break;
    }
    p_type = CsValueType::null;
}

CsValueType CsValue::get_type() const {
    return p_type;
}

void CsValue::set_int(CsInt val) {
    cleanup();
    p_type = CsValueType::integer;
    csv_get<CsInt>(p_stor) = val;
}

void CsValue::set_float(CsFloat val) {
    cleanup();
    p_type = CsValueType::number;
    csv_get<CsFloat>(p_stor) = val;
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
    cleanup();
    csv_get<CsBytecode *>(p_stor) = nullptr;
}

void CsValue::set_code(CsBytecode *val) {
    cleanup();
    p_type = CsValueType::code;
    csv_get<CsBytecode *>(p_stor) = val;
}

void CsValue::set_cstr(ostd::ConstCharRange val) {
    cleanup();
    p_type = CsValueType::cstring;
    p_len = val.size();
    csv_get<char const *>(p_stor) = val.data();
}

void CsValue::set_mstr(ostd::CharRange val) {
    cleanup();
    p_type = CsValueType::string;
    p_len = val.size();
    csv_get<char *>(p_stor) = val.data();
}

void CsValue::set_ident(CsIdent *val) {
    cleanup();
    p_type = CsValueType::ident;
    csv_get<CsIdent *>(p_stor) = val;
}

void CsValue::set_macro(ostd::ConstCharRange val) {
    cleanup();
    p_type = CsValueType::macro;
    p_len = val.size();
    csv_get<char const *>(p_stor) = val.data();
}

void CsValue::set(CsValue &tv) {
    cleanup();
    *this = tv;
    tv.set_null();
}


void CsValue::force_null() {
    if (get_type() == CsValueType::null) {
        return;
    }
    cleanup();
    set_null();
}

CsFloat CsValue::force_float() {
    CsFloat rf = 0.0f;
    switch (get_type()) {
        case CsValueType::integer:
            rf = csv_get<CsInt>(p_stor);
            break;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            rf = cs_parse_float(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
            break;
        case CsValueType::number:
            return csv_get<CsFloat>(p_stor);
        default:
            break;
    }
    cleanup();
    set_float(rf);
    return rf;
}

CsInt CsValue::force_int() {
    CsInt ri = 0;
    switch (get_type()) {
        case CsValueType::number:
            ri = csv_get<CsFloat>(p_stor);
            break;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            ri = cs_parse_int(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
            break;
        case CsValueType::integer:
            return csv_get<CsInt>(p_stor);
        default:
            break;
    }
    cleanup();
    set_int(ri);
    return ri;
}

ostd::ConstCharRange CsValue::force_str() {
    CsString rs;
    switch (get_type()) {
        case CsValueType::number:
            rs = ostd::move(floatstr(csv_get<CsFloat>(p_stor)));
            break;
        case CsValueType::integer:
            rs = ostd::move(intstr(csv_get<CsInt>(p_stor)));
            break;
        case CsValueType::macro:
        case CsValueType::cstring:
            rs = ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len);
            break;
        case CsValueType::string:
            return ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len);
        default:
            break;
    }
    cleanup();
    set_str(ostd::move(rs));
    return ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len);
}

CsInt CsValue::get_int() const {
    switch (get_type()) {
        case CsValueType::number:
            return CsInt(csv_get<CsFloat>(p_stor));
        case CsValueType::integer:
            return csv_get<CsInt>(p_stor);
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_parse_int(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
        default:
            break;
    }
    return 0;
}

CsFloat CsValue::get_float() const {
    switch (get_type()) {
        case CsValueType::number:
            return csv_get<CsFloat>(p_stor);
        case CsValueType::integer:
            return CsFloat(csv_get<CsInt>(p_stor));
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_parse_float(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
        default:
            break;
    }
    return 0.0f;
}

CsBytecode *CsValue::get_code() const {
    if (get_type() != CsValueType::code) {
        return nullptr;
    }
    return csv_get<CsBytecode *>(p_stor);
}

CsIdent *CsValue::get_ident() const {
    if (get_type() != CsValueType::ident) {
        return nullptr;
    }
    return csv_get<CsIdent *>(p_stor);
}

CsString CsValue::get_str() const {
    switch (get_type()) {
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len);
        case CsValueType::integer:
            return intstr(csv_get<CsInt>(p_stor));
        case CsValueType::number:
            return floatstr(csv_get<CsFloat>(p_stor));
        default:
            break;
    }
    return CsString("");
}

ostd::ConstCharRange CsValue::get_strr() const {
    switch (get_type()) {
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len);
        default:
            break;
    }
    return ostd::ConstCharRange();
}

void CsValue::get_val(CsValue &r) const {
    switch (get_type()) {
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            r.set_str(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
            break;
        case CsValueType::integer:
            r.set_int(csv_get<CsInt>(p_stor));
            break;
        case CsValueType::number:
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
        *reinterpret_cast<ostd::Uint32 *>(code) & CODE_OP_MASK
    ) == CODE_EXIT;
}

bool CsValue::code_is_empty() const {
    if (get_type() != CsValueType::code) {
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
        case CsValueType::number:
            return csv_get<CsFloat>(p_stor) != 0;
        case CsValueType::integer:
            return csv_get<CsInt>(p_stor) != 0;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_get_bool(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
        default:
            return false;
    }
}

void CsValue::copy_arg(CsValue &r) const {
    r.cleanup();
    switch (get_type()) {
        case CsValueType::integer:
        case CsValueType::number:
        case CsValueType::ident:
            r = *this;
            break;
        case CsValueType::string:
        case CsValueType::cstring:
        case CsValueType::macro:
            r.set_str(
                ostd::ConstCharRange(csv_get<char const *>(p_stor), p_len)
            );
            break;
        case CsValueType::code:
            r.set_code(cs_copy_code(get_code()));
            break;
        default:
            r.set_null();
            break;
    }
}

} /* namespace cscript */
