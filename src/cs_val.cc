#include <cubescript/cubescript.hh>
#include "cs_gen.hh"
#include "cs_std.hh"
#include "cs_parser.hh"
#include "cs_state.hh"

#include <cmath>
#include <iterator>

namespace cubescript {

static std::string_view intstr(integer_type v, charbuf &buf) {
    buf.reserve(32);
    int n = snprintf(buf.data(), 32, INT_FORMAT, v);
    if (n > 32) {
        buf.reserve(n + 1);
        int nn = snprintf(buf.data(), n + 1, INT_FORMAT, v);
        if ((nn > n) || (nn <= 0)) {
            n = -1;
        } else {
            n = nn;
        }
    }
    if (n <= 0) {
        throw internal_error{"format error"};
    }
    return std::string_view{buf.data(), std::size_t(n)};
}

static std::string_view floatstr(float_type v, charbuf &buf) {
    buf.reserve(32);
    int n;
    if (v == std::floor(v)) {
        n = snprintf(buf.data(), 32, ROUND_FLOAT_FORMAT, v);
    } else {
        n = snprintf(buf.data(), 32, FLOAT_FORMAT, v);
    }
    if (n > 32) {
        buf.reserve(n + 1);
        int nn;
        if (v == std::floor(v)) {
            nn = snprintf(buf.data(), n + 1, ROUND_FLOAT_FORMAT, v);
        } else {
            nn = snprintf(buf.data(), n + 1, FLOAT_FORMAT, v);
        }
        if ((nn > n) || (nn <= 0)) {
            n = -1;
        } else {
            n = nn;
        }
    }
    if (n <= 0) {
        throw internal_error{"format error"};
    }
    return std::string_view{buf.data(), std::size_t(n)};
}

template<typename T>
struct stor_priv_t {
    internal_state *state;
    T val;
};

template<typename T, typename U>
static inline T &csv_get(U *stor) {
    /* ugly, but internal and unlikely to cause bugs */
    return const_cast<T &>(std::launder(
        reinterpret_cast<stor_priv_t<T> const *>(stor)
    )->val);
}

template<typename T>
static inline void csv_cleanup(value_type tv, T *stor) {
    switch (tv) {
        case value_type::STRING:
            std::launder(reinterpret_cast<string_ref *>(stor))->~string_ref();
            break;
        case value_type::CODE: {
            bcode_unref(csv_get<uint32_t *>(stor));
            break;
        }
        default:
            break;
    }
}

any_value::any_value(state &st): any_value(*state_p{st}.ts().istate) {}

any_value::any_value(internal_state &st):
    p_stor(), p_type(value_type::NONE)
{
    std::launder(reinterpret_cast<stor_priv_t<void *> *>(&p_stor))->state = &st;
}

any_value::~any_value() {
    csv_cleanup(p_type, &p_stor);
}

any_value::any_value(any_value const &v): any_value(*v.get_state()) {
    *this = v;
}

any_value::any_value(any_value &&v): any_value(*v.get_state()) {
    *this = std::move(v);
}

any_value &any_value::operator=(any_value const &v) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::NONE;
    switch (v.get_type()) {
        case value_type::INT:
        case value_type::FLOAT:
        case value_type::IDENT:
            p_type = v.p_type;
            p_stor = v.p_stor;
            break;
        case value_type::STRING:
            p_type = value_type::STRING;
            new (&p_stor) string_ref{
                *std::launder(reinterpret_cast<string_ref const *>(&v.p_stor))
            };
            break;
        case value_type::CODE:
            set_code(v.get_code());
            break;
        default:
            break;
    }
    return *this;
}

any_value &any_value::operator=(any_value &&v) {
    *this = v;
    v.set_none();
    return *this;
}

value_type any_value::get_type() const {
    return p_type;
}

void any_value::set_int(integer_type val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::INT;
    csv_get<integer_type>(&p_stor) = val;
}

void any_value::set_float(float_type val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::FLOAT;
    csv_get<float_type>(&p_stor) = val;
}

void any_value::set_str(std::string_view val) {
    csv_cleanup(p_type, &p_stor);
    new (&p_stor) string_ref{get_state(), val};
    p_type = value_type::STRING;
}

void any_value::set_str(string_ref const &val) {
    csv_cleanup(p_type, &p_stor);
    new (&p_stor) string_ref{val};
    p_type = value_type::STRING;
}

void any_value::set_none() {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::NONE;
}

void any_value::set_code(bcode_ref const &val) {
    bcode *p = val;
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::CODE;
    bcode_addref(p->get_raw());
    csv_get<bcode *>(&p_stor) = p;
}

void any_value::set_ident(ident *val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::IDENT;
    csv_get<ident *>(&p_stor) = val;
}

void any_value::force_none() {
    if (get_type() == value_type::NONE) {
        return;
    }
    set_none();
}

float_type any_value::force_float() {
    float_type rf = 0.0f;
    switch (get_type()) {
        case value_type::INT:
            rf = float_type(csv_get<integer_type>(&p_stor));
            break;
        case value_type::STRING:
            rf = parse_float(
                *std::launder(reinterpret_cast<string_ref const *>(&p_stor))
            );
            break;
        case value_type::FLOAT:
            return csv_get<float_type>(&p_stor);
        default:
            break;
    }
    set_float(rf);
    return rf;
}

integer_type any_value::force_int() {
    integer_type ri = 0;
    switch (get_type()) {
        case value_type::FLOAT:
            ri = integer_type(std::floor(csv_get<float_type>(&p_stor)));
            break;
        case value_type::STRING:
            ri = parse_int(
                *std::launder(reinterpret_cast<string_ref const *>(&p_stor))
            );
            break;
        case value_type::INT:
            return csv_get<integer_type>(&p_stor);
        default:
            break;
    }
    set_int(ri);
    return ri;
}

std::string_view any_value::force_str() {
    charbuf rs{get_state()};
    std::string_view str;
    switch (get_type()) {
        case value_type::FLOAT:
            str = floatstr(csv_get<float_type>(&p_stor), rs);
            break;
        case value_type::INT:
            str = intstr(csv_get<integer_type>(&p_stor), rs);
            break;
        case value_type::STRING:
            return *std::launder(reinterpret_cast<string_ref const *>(&p_stor));
        default:
            str = rs.str();
            break;
    }
    set_str(str);
    return std::string_view(*std::launder(
        reinterpret_cast<string_ref const *>(&p_stor)
    ));
}

bcode_ref any_value::force_code(state &cs) {
    switch (get_type()) {
        case value_type::CODE:
            return bcode_ref{csv_get<bcode *>(&p_stor)};
        default:
            break;
    }
    codegen_state gs{state_p{cs}.ts()};
    gs.code.reserve(64);
    gs.gen_main(get_str());
    gs.done();
    uint32_t *cbuf = bcode_alloc(state_p{cs}.ts().istate, gs.code.size());
    std::memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(std::uint32_t));
    auto *bc = reinterpret_cast<bcode *>(cbuf + 1);
    set_code(bc);
    return bcode_ref{bc};
}

ident &any_value::force_ident(state &cs) {
    switch (get_type()) {
        case value_type::IDENT:
            return *csv_get<ident *>(&p_stor);
        default:
            break;
    }
    auto &id = state_p{cs}.ts().istate->new_ident(
        cs, get_str(), IDENT_FLAG_UNKNOWN
    );
    set_ident(&id);
    return id;
}

integer_type any_value::get_int() const {
    switch (get_type()) {
        case value_type::FLOAT:
            return integer_type(csv_get<float_type>(&p_stor));
        case value_type::INT:
            return csv_get<integer_type>(&p_stor);
        case value_type::STRING:
            return parse_int(
                *std::launder(reinterpret_cast<string_ref const *>(&p_stor))
            );
        default:
            break;
    }
    return 0;
}

float_type any_value::get_float() const {
    switch (get_type()) {
        case value_type::FLOAT:
            return csv_get<float_type>(&p_stor);
        case value_type::INT:
            return float_type(csv_get<integer_type>(&p_stor));
        case value_type::STRING:
            return parse_float(
                *std::launder(reinterpret_cast<string_ref const *>(&p_stor))
            );
        default:
            break;
    }
    return 0.0f;
}

bcode_ref any_value::get_code() const {
    if (get_type() != value_type::CODE) {
        return bcode_ref{};
    }
    return bcode_ref{csv_get<bcode *>(&p_stor)};
}

ident *any_value::get_ident() const {
    if (get_type() != value_type::IDENT) {
        return nullptr;
    }
    return csv_get<ident *>(&p_stor);
}

string_ref any_value::get_str() const {
    switch (get_type()) {
        case value_type::STRING:
            return *std::launder(reinterpret_cast<string_ref const *>(&p_stor));
        case value_type::INT: {
            charbuf rs{get_state()};
            return string_ref{
                get_state(), intstr(csv_get<integer_type>(&p_stor), rs)
            };
        }
        case value_type::FLOAT: {
            charbuf rs{get_state()};
            return string_ref{
                get_state(), floatstr(csv_get<float_type>(&p_stor), rs)
            };
        }
        default:
            break;
    }
    return string_ref{get_state(), ""};
}

void any_value::get_val(any_value &r) const {
    switch (get_type()) {
        case value_type::STRING:
            r = *this;
            break;
        case value_type::INT:
            r.set_int(csv_get<integer_type>(&p_stor));
            break;
        case value_type::FLOAT:
            r.set_float(csv_get<float_type>(&p_stor));
            break;
        default:
            r.set_none();
            break;
    }
}

bool any_value::get_bool() const {
    switch (get_type()) {
        case value_type::FLOAT:
            return csv_get<float_type>(&p_stor) != 0;
        case value_type::INT:
            return csv_get<integer_type>(&p_stor) != 0;
        case value_type::STRING: {
            std::string_view s = *std::launder(
                reinterpret_cast<string_ref const *>(&p_stor)
            );
            if (s.empty()) {
                return false;
            }
            std::string_view end = s;
            integer_type ival = parse_int(end, &end);
            if (end.empty()) {
                return !!ival;
            }
            end = s;
            float_type fval = parse_float(end, &end);
            if (end.empty()) {
                return !!fval;
            }
            return true;
        }
        default:
            return false;
    }
}

/* public utilities */

LIBCUBESCRIPT_EXPORT string_ref concat_values(
    state &cs, std::span<any_value> vals, std::string_view sep
) {
    charbuf buf{cs};
    for (std::size_t i = 0; i < vals.size(); ++i) {
        switch (vals[i].get_type()) {
            case value_type::INT:
            case value_type::FLOAT:
            case value_type::STRING:
                std::ranges::copy(
                    any_value{vals[i]}.force_str(), std::back_inserter(buf)
                );
                break;
            default:
                break;
        }
        if (i == (vals.size() - 1)) {
            break;
        }
        std::ranges::copy(sep, std::back_inserter(buf));
    }
    return string_ref{cs, buf.str()};
}

} /* namespace cubescript */
