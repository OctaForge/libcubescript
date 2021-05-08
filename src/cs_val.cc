#include <cubescript/cubescript.hh>
#include "cs_std.hh"
#include "cs_parser.hh"
#include "cs_state.hh"
#include "cs_strman.hh"

#include <cmath>
#include <cstdlib>
#include <iterator>

namespace cubescript {

static std::string_view intstr(integer_type v, charbuf &buf) {
    buf.reserve(32);
    int n = snprintf(buf.data(), 32, INTEGER_FORMAT, v);
    if (n > 32) {
        buf.reserve(n + 1);
        int nn = snprintf(buf.data(), n + 1, INTEGER_FORMAT, v);
        if ((nn > n) || (nn <= 0)) {
            n = -1;
        } else {
            n = nn;
        }
    }
    if (n <= 0) {
        abort(); /* unreachable, provided a well-formed format string */
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
        abort(); /* unreachable, provided a well-formed format string */
    }
    return std::string_view{buf.data(), std::size_t(n)};
}

template<typename T>
static inline void csv_cleanup(value_type tv, T *stor) {
    switch (tv) {
        case value_type::STRING:
            str_managed_unref(stor->s);
            break;
        case value_type::CODE: {
            bcode_unref(stor->b->raw());
            break;
        }
        default:
            break;
    }
}

any_value::any_value():
    p_stor{}, p_type{value_type::NONE}
{}

any_value::any_value(integer_type val):
    p_stor{}, p_type{value_type::INTEGER}
{
    p_stor.i = val;
}

any_value::any_value(float_type val):
    p_stor{}, p_type{value_type::FLOAT}
{
    p_stor.f = val;
}

any_value::any_value(std::string_view val, state &cs):
    p_stor{}, p_type{value_type::STRING}
{
    p_stor.s = state_p{cs}.ts().istate->strman->add(val);
}

any_value::any_value(string_ref const &val):
    p_stor{}, p_type{value_type::STRING}
{
    p_stor.s = str_managed_ref(val.p_str);
}

any_value::any_value(bcode_ref const &val):
    p_stor{}, p_type{value_type::CODE}
{
    bcode *p = bcode_p{val}.get();
    bcode_addref(p->raw());
    p_stor.b = p;
}

any_value::any_value(ident &val):
    p_stor{}, p_type{value_type::IDENT}
{
    p_stor.v = &val;
}

any_value::~any_value() {
    csv_cleanup(p_type, &p_stor);
}

any_value::any_value(any_value const &v): any_value{} {
    *this = v;
}

any_value::any_value(any_value &&v): any_value{} {
    *this = std::move(v);
}

any_value &any_value::operator=(any_value const &v) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::NONE;
    switch (v.type()) {
        case value_type::INTEGER:
        case value_type::FLOAT:
        case value_type::IDENT:
            p_type = v.p_type;
            std::memcpy(&p_stor, &v.p_stor, sizeof(p_stor));
            break;
        case value_type::STRING:
            p_type = value_type::STRING;
            p_stor.s = v.p_stor.s;
            str_managed_ref(p_stor.s);
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

any_value &any_value::operator=(integer_type val) {
    set_integer(val);
    return *this;
}

any_value &any_value::operator=(float_type val) {
    set_float(val);
    return *this;
}

any_value &any_value::operator=(string_ref const &val) {
    set_string(val);
    return *this;
}

any_value &any_value::operator=(bcode_ref const &val) {
    set_code(val);
    return *this;
}

any_value &any_value::operator=(ident &val) {
    set_ident(val);
    return *this;
}

value_type any_value::type() const {
    return p_type;
}

void any_value::set_integer(integer_type val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::INTEGER;
    p_stor.i = val;
}

void any_value::set_float(float_type val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::FLOAT;
    p_stor.f = val;
}

void any_value::set_string(std::string_view val, state &cs) {
    csv_cleanup(p_type, &p_stor);
    p_stor.s = state_p{cs}.ts().istate->strman->add(val);
    p_type = value_type::STRING;
}

void any_value::set_string(string_ref const &val) {
    csv_cleanup(p_type, &p_stor);
    p_stor.s = str_managed_ref(val.p_str);
    p_type = value_type::STRING;
}

void any_value::set_none() {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::NONE;
}

void any_value::set_code(bcode_ref const &val) {
    bcode *p = bcode_p{val}.get();
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::CODE;
    bcode_addref(p->raw());
    p_stor.b = p;
}

void any_value::set_ident(ident &val) {
    csv_cleanup(p_type, &p_stor);
    p_type = value_type::IDENT;
    p_stor.v = &val;
}

void any_value::force_none() {
    if (type() == value_type::NONE) {
        return;
    }
    set_none();
}

void any_value::force_plain() {
    switch (type()) {
        case value_type::FLOAT:
        case value_type::INTEGER:
        case value_type::STRING:
            return;
        default:
            break;
    }
    force_none();
}

float_type any_value::force_float() {
    float_type rf = 0.0f;
    switch (type()) {
        case value_type::INTEGER:
            rf = float_type(p_stor.i);
            break;
        case value_type::STRING:
            rf = parse_float(str_managed_view(p_stor.s));
            break;
        case value_type::FLOAT:
            return p_stor.f;
        default:
            break;
    }
    set_float(rf);
    return rf;
}

integer_type any_value::force_integer() {
    integer_type ri = 0;
    switch (type()) {
        case value_type::FLOAT:
            ri = integer_type(std::floor(p_stor.f));
            break;
        case value_type::STRING:
            ri = parse_int(str_managed_view(p_stor.s));
            break;
        case value_type::INTEGER:
            return p_stor.i;
        default:
            break;
    }
    set_integer(ri);
    return ri;
}

std::string_view any_value::force_string(state &cs) {
    charbuf rs{cs};
    std::string_view str;
    switch (type()) {
        case value_type::FLOAT:
            str = floatstr(p_stor.f, rs);
            break;
        case value_type::INTEGER:
            str = intstr(p_stor.i, rs);
            break;
        case value_type::STRING:
            return str_managed_view(p_stor.s);
        default:
            str = rs.str();
            break;
    }
    set_string(str, cs);
    return str_managed_view(p_stor.s);
}

bcode_ref any_value::force_code(state &cs, std::string_view source) {
    switch (type()) {
        case value_type::CODE:
            return bcode_p::make_ref(p_stor.b);
        default:
            break;
    }
    gen_state gs{state_p{cs}.ts()};
    gs.gen_main(get_string(cs), source);
    auto bc = gs.steal_ref();
    set_code(bc);
    return bc;
}

ident &any_value::force_ident(state &cs) {
    switch (type()) {
        case value_type::IDENT:
            return *p_stor.v;
        default:
            break;
    }
    auto &id = state_p{cs}.ts().istate->new_ident(
        cs, get_string(cs), IDENT_FLAG_UNKNOWN
    );
    set_ident(id);
    return id;
}

integer_type any_value::get_integer() const {
    switch (type()) {
        case value_type::FLOAT:
            return integer_type(std::floor(p_stor.f));
        case value_type::INTEGER:
            return p_stor.i;
        case value_type::STRING:
            return parse_int(str_managed_view(p_stor.s));
        default:
            break;
    }
    return 0;
}

float_type any_value::get_float() const {
    switch (type()) {
        case value_type::FLOAT:
            return p_stor.f;
        case value_type::INTEGER:
            return p_stor.i;
        case value_type::STRING:
            return parse_float(str_managed_view(p_stor.s));
        default:
            break;
    }
    return 0.0f;
}

bcode_ref any_value::get_code() const {
    if (type() != value_type::CODE) {
        return bcode_ref{};
    }
    return bcode_p::make_ref(p_stor.b);
}

ident &any_value::get_ident(state &cs) const {
    if (type() != value_type::IDENT) {
        return *state_p{cs}.ts().istate->id_dummy;
    }
    return *p_stor.v;
}

string_ref any_value::get_string(state &cs) const {
    switch (type()) {
        case value_type::STRING:
            return string_ref{p_stor.s};
        case value_type::INTEGER: {
            charbuf rs{cs};
            return string_ref{cs, intstr(p_stor.i, rs)};
        }
        case value_type::FLOAT: {
            charbuf rs{cs};
            return string_ref{cs, floatstr(p_stor.f, rs)};
        }
        default:
            break;
    }
    return string_ref{cs, ""};
}

any_value any_value::get_plain() const {
    switch (type()) {
        case value_type::STRING:
        case value_type::INTEGER:
        case value_type::FLOAT:
            return *this;
        default:
            break;
    }
    return any_value{};
}

bool any_value::get_bool() const {
    switch (type()) {
        case value_type::FLOAT:
            return p_stor.f != 0;
        case value_type::INTEGER:
            return p_stor.i != 0;
        case value_type::STRING: {
            std::string_view s = str_managed_view(p_stor.s);
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
    state &cs, span_type<any_value> vals, std::string_view sep
) {
    charbuf buf{cs};
    for (std::size_t i = 0; i < vals.size(); ++i) {
        switch (vals[i].type()) {
            case value_type::INTEGER:
            case value_type::FLOAT:
            case value_type::STRING: {
                auto val = any_value{vals[i]};
                auto str = val.force_string(cs);
                std::copy(str.begin(), str.end(), std::back_inserter(buf));
                break;
            }
            default:
                break;
        }
        if (i == (vals.size() - 1)) {
            break;
        }
        std::copy(sep.begin(), sep.end(), std::back_inserter(buf));
    }
    return string_ref{cs, buf.str()};
}

} /* namespace cubescript */
