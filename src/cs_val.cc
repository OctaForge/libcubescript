#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_util.hh"

namespace cscript {

template<typename T>
struct stor_priv_t {
    cs_shared_state *state;
    T val;
};

template<typename T, typename U>
static inline T &csv_get(U &stor) {
    /* ugly, but internal and unlikely to cause bugs */
    return const_cast<T &>(reinterpret_cast<stor_priv_t<T> const &>(stor).val);
}

template<typename T>
static inline void csv_cleanup(cs_value_type tv, T &stor) {
    switch (tv) {
        case cs_value_type::String:
            reinterpret_cast<cs_strref *>(&stor)->~cs_strref();
            break;
        case cs_value_type::Code: {
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

cs_value::cs_value(cs_state &st): cs_value(*st.p_state) {}

cs_value::cs_value(cs_shared_state &st):
    p_stor(), p_len(0), p_type(cs_value_type::Null)
{
    reinterpret_cast<stor_priv_t<void *> *>(&p_stor)->state = &st;
}

cs_value::~cs_value() {
    csv_cleanup(p_type, p_stor);
}

cs_value::cs_value(cs_value const &v): cs_value(*v.state()) {
    *this = v;
}

cs_value &cs_value::operator=(cs_value const &v) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Null;
    switch (v.get_type()) {
        case cs_value_type::Int:
        case cs_value_type::Float:
        case cs_value_type::Ident:
            p_len = v.p_len;
            p_type = v.p_type;
            p_stor = v.p_stor;
            break;
        case cs_value_type::String:
            p_type = cs_value_type::String;
            p_len = v.p_len;
            new (&p_stor) cs_strref{
                *reinterpret_cast<cs_strref const *>(&v.p_stor)
            };
            break;
        case cs_value_type::Code:
            set_code(cs_copy_code(v.get_code()));
            break;
        default:
            break;
    }
    return *this;
}

cs_value_type cs_value::get_type() const {
    return p_type;
}

void cs_value::set_int(cs_int val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Int;
    csv_get<cs_int>(p_stor) = val;
}

void cs_value::set_float(cs_float val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Float;
    csv_get<cs_float>(p_stor) = val;
}

void cs_value::set_str(ostd::string_range val) {
    csv_cleanup(p_type, p_stor);
    new (&p_stor) cs_strref{*state(), val};
    p_type = cs_value_type::String;
    p_len = val.size();
}

void cs_value::set_null() {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Null;
}

void cs_value::set_code(cs_bcode *val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Code;
    csv_get<cs_bcode *>(p_stor) = val;
}

void cs_value::set_ident(cs_ident *val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::Ident;
    csv_get<cs_ident *>(p_stor) = val;
}

void cs_value::force_null() {
    if (get_type() == cs_value_type::Null) {
        return;
    }
    set_null();
}

cs_float cs_value::force_float() {
    cs_float rf = 0.0f;
    switch (get_type()) {
        case cs_value_type::Int:
            rf = csv_get<cs_int>(p_stor);
            break;
        case cs_value_type::String:
            rf = cs_parse_float(ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
            break;
        case cs_value_type::Float:
            return csv_get<cs_float>(p_stor);
        default:
            break;
    }
    set_float(rf);
    return rf;
}

cs_int cs_value::force_int() {
    cs_int ri = 0;
    switch (get_type()) {
        case cs_value_type::Float:
            ri = csv_get<cs_float>(p_stor);
            break;
        case cs_value_type::String:
            ri = cs_parse_int(ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
            break;
        case cs_value_type::Int:
            return csv_get<cs_int>(p_stor);
        default:
            break;
    }
    set_int(ri);
    return ri;
}

ostd::string_range cs_value::force_str() {
    cs_string rs;
    switch (get_type()) {
        case cs_value_type::Float:
            rs = floatstr(csv_get<cs_float>(p_stor));
            break;
        case cs_value_type::Int:
            rs = intstr(csv_get<cs_int>(p_stor));
            break;
        case cs_value_type::String:
            return ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            );
        default:
            break;
    }
    set_str(std::move(rs));
    return ostd::string_range(
        csv_get<char const *>(p_stor),
        csv_get<char const *>(p_stor) + p_len
    );
}

cs_int cs_value::get_int() const {
    switch (get_type()) {
        case cs_value_type::Float:
            return cs_int(csv_get<cs_float>(p_stor));
        case cs_value_type::Int:
            return csv_get<cs_int>(p_stor);
        case cs_value_type::String:
            return cs_parse_int(ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            break;
    }
    return 0;
}

cs_float cs_value::get_float() const {
    switch (get_type()) {
        case cs_value_type::Float:
            return csv_get<cs_float>(p_stor);
        case cs_value_type::Int:
            return cs_float(csv_get<cs_int>(p_stor));
        case cs_value_type::String:
            return cs_parse_float(ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            break;
    }
    return 0.0f;
}

cs_bcode *cs_value::get_code() const {
    if (get_type() != cs_value_type::Code) {
        return nullptr;
    }
    return csv_get<cs_bcode *>(p_stor);
}

cs_ident *cs_value::get_ident() const {
    if (get_type() != cs_value_type::Ident) {
        return nullptr;
    }
    return csv_get<cs_ident *>(p_stor);
}

cs_string cs_value::get_str() const {
    switch (get_type()) {
        case cs_value_type::String:
            return cs_string{csv_get<char const *>(p_stor), p_len};
        case cs_value_type::Int:
            return intstr(csv_get<cs_int>(p_stor));
        case cs_value_type::Float:
            return floatstr(csv_get<cs_float>(p_stor));
        default:
            break;
    }
    return cs_string("");
}

ostd::string_range cs_value::get_strr() const {
    switch (get_type()) {
        case cs_value_type::String:
            return ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor)+ p_len
            );
        default:
            break;
    }
    return ostd::string_range();
}

void cs_value::get_val(cs_value &r) const {
    switch (get_type()) {
        case cs_value_type::String:
            r = *this;
            break;
        case cs_value_type::Int:
            r.set_int(csv_get<cs_int>(p_stor));
            break;
        case cs_value_type::Float:
            r.set_float(csv_get<cs_float>(p_stor));
            break;
        default:
            r.set_null();
            break;
    }
}

OSTD_EXPORT bool cs_code_is_empty(cs_bcode *code) {
    if (!code) {
        return true;
    }
    return (
        *reinterpret_cast<uint32_t *>(code) & CsCodeOpMask
    ) == CsCodeExit;
}

bool cs_value::code_is_empty() const {
    if (get_type() != cs_value_type::Code) {
        return true;
    }
    return cscript::cs_code_is_empty(csv_get<cs_bcode *>(p_stor));
}

static inline bool cs_get_bool(ostd::string_range s) {
    if (s.empty()) {
        return false;
    }
    ostd::string_range end = s;
    cs_int ival = cs_parse_int(end, &end);
    if (end.empty()) {
        return !!ival;
    }
    end = s;
    cs_float fval = cs_parse_float(end, &end);
    if (end.empty()) {
        return !!fval;
    }
    return true;
}

bool cs_value::get_bool() const {
    switch (get_type()) {
        case cs_value_type::Float:
            return csv_get<cs_float>(p_stor) != 0;
        case cs_value_type::Int:
            return csv_get<cs_int>(p_stor) != 0;
        case cs_value_type::String:
            return cs_get_bool(ostd::string_range(
                csv_get<char const *>(p_stor),
                csv_get<char const *>(p_stor) + p_len
            ));
        default:
            return false;
    }
}

/* stacked value for easy stack management */

cs_stacked_value::cs_stacked_value(cs_state &cs, cs_ident *id):
    cs_value(cs), p_a(nullptr), p_stack{cs}, p_pushed(false)
{
    set_alias(id);
}

cs_stacked_value::~cs_stacked_value() {
    pop();
    static_cast<cs_value *>(this)->~cs_value();
}

cs_stacked_value &cs_stacked_value::operator=(cs_value const &v) {
    *static_cast<cs_value *>(this) = v;
    return *this;
}

cs_stacked_value &cs_stacked_value::operator=(cs_value &&v) {
    *static_cast<cs_value *>(this) = std::move(v);
    return *this;
}

bool cs_stacked_value::set_alias(cs_ident *id) {
    if (!id || !id->is_alias()) {
        return false;
    }
    p_a = static_cast<cs_alias *>(id);
    return true;
}

cs_alias *cs_stacked_value::get_alias() const {
    return p_a;
}

bool cs_stacked_value::has_alias() const {
    return p_a != nullptr;
}

bool cs_stacked_value::push() {
    if (!p_a) {
        return false;
    }
    cs_alias_internal::push_arg(p_a, *this, p_stack);
    p_pushed = true;
    return true;
}

bool cs_stacked_value::pop() {
    if (!p_pushed || !p_a) {
        return false;
    }
    cs_alias_internal::pop_arg(p_a);
    p_pushed = false;
    return true;
}

} /* namespace cscript */
