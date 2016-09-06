#include "cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

void CsValue::cleanup() {
    switch (get_type()) {
        case CsValueType::string:
            delete[] p_s;
            break;
        case CsValueType::code: {
            ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(p_code);
            if (bcode[-1] == CODE_START) {
                delete[] bcode;
            }
            break;
        }
        default:
            break;
    }
}

CsValueType CsValue::get_type() const {
    return p_type;
}

void CsValue::set_int(CsInt val) {
    p_type = CsValueType::integer;
    p_i = val;
}

void CsValue::set_float(CsFloat val) {
    p_type = CsValueType::number;
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
    p_type = CsValueType::null;
    p_code = nullptr;
}

void CsValue::set_code(CsBytecode *val) {
    p_type = CsValueType::code;
    p_code = val;
}

void CsValue::set_cstr(ostd::ConstCharRange val) {
    p_type = CsValueType::cstring;
    p_len = val.size();
    p_cstr = val.data();
}

void CsValue::set_mstr(ostd::CharRange val) {
    p_type = CsValueType::string;
    p_len = val.size();
    p_s = val.data();
}

void CsValue::set_ident(CsIdent *val) {
    p_type = CsValueType::ident;
    p_id = val;
}

void CsValue::set_macro(ostd::ConstCharRange val) {
    p_type = CsValueType::macro;
    p_len = val.size();
    p_cstr = val.data();
}

void CsValue::set(CsValue &tv) {
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
            rf = p_i;
            break;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            rf = cs_parse_float(ostd::ConstCharRange(p_s, p_len));
            break;
        case CsValueType::number:
            return p_f;
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
            ri = p_f;
            break;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            ri = cs_parse_int(ostd::ConstCharRange(p_s, p_len));
            break;
        case CsValueType::integer:
            return p_i;
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
            rs = ostd::move(floatstr(p_f));
            break;
        case CsValueType::integer:
            rs = ostd::move(intstr(p_i));
            break;
        case CsValueType::macro:
        case CsValueType::cstring:
            rs = ostd::ConstCharRange(p_s, p_len);
            break;
        case CsValueType::string:
            return ostd::ConstCharRange(p_s, p_len);
        default:
            break;
    }
    cleanup();
    set_str(ostd::move(rs));
    return ostd::ConstCharRange(p_s, p_len);
}

CsInt CsValue::get_int() const {
    switch (get_type()) {
        case CsValueType::number:
            return CsInt(p_f);
        case CsValueType::integer:
            return p_i;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_parse_int(ostd::ConstCharRange(p_s, p_len));
        default:
            break;
    }
    return 0;
}

CsFloat CsValue::get_float() const {
    switch (get_type()) {
        case CsValueType::number:
            return p_f;
        case CsValueType::integer:
            return CsFloat(p_i);
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_parse_float(ostd::ConstCharRange(p_s, p_len));
        default:
            break;
    }
    return 0.0f;
}

CsBytecode *CsValue::get_code() const {
    if (get_type() != CsValueType::code) {
        return nullptr;
    }
    return p_code;
}

CsIdent *CsValue::get_ident() const {
    if (get_type() != CsValueType::ident) {
        return nullptr;
    }
    return p_id;
}

CsString CsValue::get_str() const {
    switch (get_type()) {
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return ostd::ConstCharRange(p_s, p_len);
        case CsValueType::integer:
            return intstr(p_i);
        case CsValueType::number:
            return floatstr(p_f);
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
            return ostd::ConstCharRange(p_s, p_len);
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
            r.set_str(ostd::ConstCharRange(p_s, p_len));
            break;
        case CsValueType::integer:
            r.set_int(p_i);
            break;
        case CsValueType::number:
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
    if (get_type() != CsValueType::code) {
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
        case CsValueType::number:
            return p_f != 0;
        case CsValueType::integer:
            return p_i != 0;
        case CsValueType::string:
        case CsValueType::macro:
        case CsValueType::cstring:
            return cs_get_bool(ostd::ConstCharRange(p_s, p_len));
        default:
            return false;
    }
}

} /* namespace cscript */
