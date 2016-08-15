#include "cubescript_conf.hh"
#include "cs_util.hh"

#include <ctype.h>
#include <math.h>

namespace cscript {
namespace parser {

static inline void p_skip_white(ostd::ConstCharRange &v) {
    while (!v.empty() && isspace(v.front())) {
        v.pop_front();
    }
}

static inline void p_set_end(
    const ostd::ConstCharRange &v, ostd::ConstCharRange *end
) {
    if (!end) {
        return;
    }
    *end = v;
}

static inline CsInt p_hexd_to_int(char c) {
    if ((c >= 48) && (c <= 57)) { /* 0-9 */
        return c - '0';
    }
    if ((c >= 65) && (c <= 70)) { /* A-F */
        return (c - 'A') + 10;
    }
    if ((c >= 97) && (c <= 102)) { /* a-f */
        return (c - 'a') + 10;
    }
    return 0;
}

static inline CsInt p_decd_to_int(char c) {
    return c - '0';
}

static inline CsInt p_bind_to_int(char c) {
    return c - '0';
}

static inline bool p_is_hexdigit(char c) {
    return isxdigit(c);
}

static inline bool p_is_decdigit(char c) {
    return isdigit(c);
}

static inline bool p_is_bindigit(char c) {
    return (c == '0') || (c == '1');
}

CsInt parse_int(ostd::ConstCharRange input, ostd::ConstCharRange *end) {
    ostd::ConstCharRange orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return CsInt(0);
    }
    /* 1 for false, -1 for true */
    int neg = -(int(input.front() == '-') * 2 - 1);
    if (neg < 0 || (input.front() == '+')) {
        input.pop_front();
    }
    CsInt ret = 0;
    ostd::ConstCharRange past = input;
    if (input.size() >= 2) {
        ostd::ConstCharRange pfx = input.slice(0, 2);
        if ((pfx == "0x") || (pfx == "0X")) {
            input.pop_front_n(2);
            past = input;
            while (!past.empty() && p_is_hexdigit(past.front())) {
                ret = ret * 16 + p_hexd_to_int(past.front());
                past.pop_front();
            }
            goto done;
        } else if ((pfx == "0b") || (pfx == "0B")) {
            input.pop_front_n(2);
            past = input;
            while (!past.empty() && p_is_bindigit(past.front())) {
                ret = ret * 2 + p_bind_to_int(past.front());
                past.pop_front();
            }
            goto done;
        }
    }
    while (!past.empty() && p_is_decdigit(past.front())) {
        ret = ret * 10 + p_decd_to_int(past.front());
        past.pop_front();
    }
done:
    if (past.equals_front(input)) {
        p_set_end(orig, end);
    } else {
        p_set_end(past, end);
    }
    return ret * neg;
}

template<char e1, char e2>
static inline bool p_read_exp(ostd::ConstCharRange &input, CsInt &fn) {
    if (input.empty()) {
        return true;
    }
    if ((input.front() != e1) && (input.front() != e2)) {
        return true;
    }
    input.pop_front();
    if (input.empty()) {
        return false;
    }
    bool neg = (input.front() == '-');
    if (neg || input.front() == '+') {
        input.pop_front();
    }
    if (input.empty() || !p_is_decdigit(input.front())) {
        return false;
    }
    CsInt exp = 0;
    while (!input.empty() && p_is_decdigit(input.front())) {
        exp = exp * 10 + p_decd_to_int(input.front());
        input.pop_front();
    }
    if (neg) {
        exp = -exp;
    }
    fn += exp;
    return true;
}

static inline bool parse_hex_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end, CsFloat &ret
) {
    auto read_hd = [&input](double r, CsInt &n) {
        while (!input.empty() && p_is_hexdigit(input.front())) {
            r = r * 16.0 + double(p_hexd_to_int(input.front()));
            ++n;
            input.pop_front();
        }
        return r;
    };
    CsInt wn = 0, fn = 0;
    double r = read_hd(0.0, wn);
    if (!input.empty() && (input.front() == '.')) {
        input.pop_front();
        r = read_hd(r, fn);
    }
    if (!wn && !fn) {
        return false;
    }
    fn *= -4;
    p_set_end(input, end); /* we have a valid number until here */
    if (p_read_exp<'p', 'P'>(input, fn)) {
        p_set_end(input, end);
    }
    ret = CsFloat(ldexp(r, fn));
    return true;
}

static inline bool parse_dec_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end, CsFloat &ret
) {
    auto read_hd = [&input](double r, CsInt &n) {
        while (!input.empty() && p_is_decdigit(input.front())) {
            r = r * 10.0 + double(p_decd_to_int(input.front()));
            ++n;
            input.pop_front();
        }
        return r;
    };
    CsInt wn = 0, fn = 0;
    double r = read_hd(0.0, wn);
    if (!input.empty() && (input.front() == '.')) {
        input.pop_front();
        r = read_hd(r, fn);
    }
    if (!wn && !fn) {
        return false;
    }
    fn = -fn;
    p_set_end(input, end);
    if (p_read_exp<'e', 'E'>(input, fn)) {
        p_set_end(input, end);
    }
    ret = CsFloat(r * pow(10, fn));
    return true;
}

CsFloat parse_float(ostd::ConstCharRange input, ostd::ConstCharRange *end) {
    ostd::ConstCharRange orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return CsFloat(0);
    }
    bool neg = (input.front() == '-');
    if (neg || (input.front() == '+')) {
        input.pop_front();
    }
    bool hex = false;
    CsFloat ret = CsFloat(0);
    if (input.size() >= 2) {
        ostd::ConstCharRange pfx = input.slice(0, 2);
        if ((hex = ((pfx == "0x") || (pfx == "0X")))) {
            input.pop_front_n(2);
            if (!parse_hex_float(input, end, ret)) {
                p_set_end(orig, end);
                return ret;
            }
        }
    }
    if (!hex && !parse_dec_float(input, end, ret)) {
        p_set_end(orig, end);
        return ret;
    }
    if (neg) {
        return -ret;
    }
    return ret;
}

} /* namespace parser */
} /* namespace cscript */
