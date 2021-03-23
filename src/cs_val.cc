#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_std.hh"

#include <cmath>

namespace cscript {

static std::string_view intstr(cs_int v, cs_charbuf &buf) {
    buf.reserve(32);
    int n = snprintf(buf.data(), 32, CS_INT_FORMAT, v);
    if (n > 32) {
        buf.reserve(n + 1);
        int nn = snprintf(buf.data(), n + 1, CS_INT_FORMAT, v);
        if ((nn > n) || (nn <= 0)) {
            n = -1;
        } else {
            n = nn;
        }
    }
    if (n <= 0) {
        throw cs_internal_error{"format error"};
    }
    return std::string_view{buf.data(), std::size_t(n)};
}

static std::string_view floatstr(cs_float v, cs_charbuf &buf) {
    buf.reserve(32);
    int n;
    if (v == std::floor(v)) {
        n = snprintf(buf.data(), 32, CS_ROUND_FLOAT_FORMAT, v);
    } else {
        n = snprintf(buf.data(), 32, CS_FLOAT_FORMAT, v);
    }
    if (n > 32) {
        buf.reserve(n + 1);
        int nn;
        if (v == std::floor(v)) {
            nn = snprintf(buf.data(), n + 1, CS_ROUND_FLOAT_FORMAT, v);
        } else {
            nn = snprintf(buf.data(), n + 1, CS_FLOAT_FORMAT, v);
        }
        if ((nn > n) || (nn <= 0)) {
            n = -1;
        } else {
            n = nn;
        }
    }
    if (n <= 0) {
        throw cs_internal_error{"format error"};
    }
    return std::string_view{buf.data(), std::size_t(n)};
}

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
        case cs_value_type::STRING:
            reinterpret_cast<cs_strref *>(&stor)->~cs_strref();
            break;
        case cs_value_type::CODE: {
            bcode_unref(csv_get<uint32_t *>(stor));
            break;
        }
        default:
            break;
    }
}

cs_value::cs_value(cs_state &st): cs_value(*st.p_state) {}

cs_value::cs_value(cs_shared_state &st):
    p_stor(), p_type(cs_value_type::NONE)
{
    reinterpret_cast<stor_priv_t<void *> *>(&p_stor)->state = &st;
}

cs_value::~cs_value() {
    csv_cleanup(p_type, p_stor);
}

cs_value::cs_value(cs_value const &v): cs_value(*v.state()) {
    *this = v;
}

cs_value::cs_value(cs_value &&v): cs_value(*v.state()) {
    *this = std::move(v);
}

cs_value &cs_value::operator=(cs_value const &v) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::NONE;
    switch (v.get_type()) {
        case cs_value_type::INT:
        case cs_value_type::FLOAT:
        case cs_value_type::IDENT:
            p_type = v.p_type;
            p_stor = v.p_stor;
            break;
        case cs_value_type::STRING:
            p_type = cs_value_type::STRING;
            new (&p_stor) cs_strref{
                *reinterpret_cast<cs_strref const *>(&v.p_stor)
            };
            break;
        case cs_value_type::CODE:
            set_code(v.get_code());
            break;
        default:
            break;
    }
    return *this;
}

cs_value &cs_value::operator=(cs_value &&v) {
    *this = v;
    v.set_none();
    return *this;
}

cs_value_type cs_value::get_type() const {
    return p_type;
}

void cs_value::set_int(cs_int val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::INT;
    csv_get<cs_int>(p_stor) = val;
}

void cs_value::set_float(cs_float val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::FLOAT;
    csv_get<cs_float>(p_stor) = val;
}

void cs_value::set_str(std::string_view val) {
    csv_cleanup(p_type, p_stor);
    new (&p_stor) cs_strref{state(), val};
    p_type = cs_value_type::STRING;
}

void cs_value::set_str(cs_strref const &val) {
    csv_cleanup(p_type, p_stor);
    new (&p_stor) cs_strref{val};
    p_type = cs_value_type::STRING;
}

void cs_value::set_none() {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::NONE;
}

void cs_value::set_code(cs_bcode *val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::CODE;
    bcode_ref(val->get_raw());
    csv_get<cs_bcode *>(p_stor) = val;
}

void cs_value::set_ident(cs_ident *val) {
    csv_cleanup(p_type, p_stor);
    p_type = cs_value_type::IDENT;
    csv_get<cs_ident *>(p_stor) = val;
}

void cs_value::force_none() {
    if (get_type() == cs_value_type::NONE) {
        return;
    }
    set_none();
}

cs_float cs_value::force_float() {
    cs_float rf = 0.0f;
    switch (get_type()) {
        case cs_value_type::INT:
            rf = csv_get<cs_int>(p_stor);
            break;
        case cs_value_type::STRING:
            rf = parse_float(
                *reinterpret_cast<cs_strref const *>(&p_stor)
            );
            break;
        case cs_value_type::FLOAT:
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
        case cs_value_type::FLOAT:
            ri = csv_get<cs_float>(p_stor);
            break;
        case cs_value_type::STRING:
            ri = parse_int(
                *reinterpret_cast<cs_strref const *>(&p_stor)
            );
            break;
        case cs_value_type::INT:
            return csv_get<cs_int>(p_stor);
        default:
            break;
    }
    set_int(ri);
    return ri;
}

std::string_view cs_value::force_str() {
    cs_charbuf rs{state()};
    std::string_view str;
    switch (get_type()) {
        case cs_value_type::FLOAT:
            str = floatstr(csv_get<cs_float>(p_stor), rs);
            break;
        case cs_value_type::INT:
            str = intstr(csv_get<cs_int>(p_stor), rs);
            break;
        case cs_value_type::STRING:
            return *reinterpret_cast<cs_strref const *>(&p_stor);
        default:
            str = rs.str();
            break;
    }
    set_str(str);
    return std::string_view(*reinterpret_cast<cs_strref const *>(&p_stor));
}

cs_int cs_value::get_int() const {
    switch (get_type()) {
        case cs_value_type::FLOAT:
            return cs_int(csv_get<cs_float>(p_stor));
        case cs_value_type::INT:
            return csv_get<cs_int>(p_stor);
        case cs_value_type::STRING:
            return parse_int(
                *reinterpret_cast<cs_strref const *>(&p_stor)
            );
        default:
            break;
    }
    return 0;
}

cs_float cs_value::get_float() const {
    switch (get_type()) {
        case cs_value_type::FLOAT:
            return csv_get<cs_float>(p_stor);
        case cs_value_type::INT:
            return cs_float(csv_get<cs_int>(p_stor));
        case cs_value_type::STRING:
            return parse_float(
                *reinterpret_cast<cs_strref const *>(&p_stor)
            );
        default:
            break;
    }
    return 0.0f;
}

cs_bcode *cs_value::get_code() const {
    if (get_type() != cs_value_type::CODE) {
        return nullptr;
    }
    return csv_get<cs_bcode *>(p_stor);
}

cs_ident *cs_value::get_ident() const {
    if (get_type() != cs_value_type::IDENT) {
        return nullptr;
    }
    return csv_get<cs_ident *>(p_stor);
}

cs_strref cs_value::get_str() const {
    switch (get_type()) {
        case cs_value_type::STRING:
            return *reinterpret_cast<cs_strref const *>(&p_stor);
        case cs_value_type::INT: {
            cs_charbuf rs{state()};
            return cs_strref{state(), intstr(csv_get<cs_int>(p_stor), rs)};
        }
        case cs_value_type::FLOAT: {
            cs_charbuf rs{state()};
            return cs_strref{state(), floatstr(csv_get<cs_float>(p_stor), rs)};
        }
        default:
            break;
    }
    return cs_strref{state(), ""};
}

void cs_value::get_val(cs_value &r) const {
    switch (get_type()) {
        case cs_value_type::STRING:
            r = *this;
            break;
        case cs_value_type::INT:
            r.set_int(csv_get<cs_int>(p_stor));
            break;
        case cs_value_type::FLOAT:
            r.set_float(csv_get<cs_float>(p_stor));
            break;
        default:
            r.set_none();
            break;
    }
}

LIBCUBESCRIPT_EXPORT bool cs_code_is_empty(cs_bcode *code) {
    if (!code) {
        return true;
    }
    return (*code->get_raw() & CS_CODE_OP_MASK) == CS_CODE_EXIT;
}

bool cs_value::code_is_empty() const {
    if (get_type() != cs_value_type::CODE) {
        return true;
    }
    return cscript::cs_code_is_empty(csv_get<cs_bcode *>(p_stor));
}

static inline bool cs_get_bool(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    std::string_view end = s;
    cs_int ival = parse_int(end, &end);
    if (end.empty()) {
        return !!ival;
    }
    end = s;
    cs_float fval = parse_float(end, &end);
    if (end.empty()) {
        return !!fval;
    }
    return true;
}

bool cs_value::get_bool() const {
    switch (get_type()) {
        case cs_value_type::FLOAT:
            return csv_get<cs_float>(p_stor) != 0;
        case cs_value_type::INT:
            return csv_get<cs_int>(p_stor) != 0;
        case cs_value_type::STRING:
            return cs_get_bool(
                *reinterpret_cast<cs_strref const *>(&p_stor)
            );
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
    static_cast<cs_alias_impl *>(p_a)->push_arg(*this, p_stack);
    p_pushed = true;
    return true;
}

bool cs_stacked_value::pop() {
    if (!p_pushed || !p_a) {
        return false;
    }
    static_cast<cs_alias_impl *>(p_a)->pop_arg();
    p_pushed = false;
    return true;
}

} /* namespace cscript */
