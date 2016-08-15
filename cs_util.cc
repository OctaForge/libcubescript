#include "cubescript_conf.hh"
#include "cs_util.hh"

#include <ctype.h>
#include <math.h>

namespace cscript {

static inline void p_skip_white(ostd::ConstCharRange &v) {
    while (!v.empty() && isspace(v.front())) {
        ++v;
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

static inline bool p_check_neg(ostd::ConstCharRange &input) {
    bool neg = (*input == '-');
    if (neg || (*input == '+')) {
        ++input;
    }
    return neg;
}

CsInt cs_parse_int(ostd::ConstCharRange input, ostd::ConstCharRange *end) {
    ostd::ConstCharRange orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return CsInt(0);
    }
    bool neg = p_check_neg(input);
    CsInt ret = 0;
    ostd::ConstCharRange past = input;
    if (input.size() >= 2) {
        ostd::ConstCharRange pfx = input.slice(0, 2);
        if ((pfx == "0x") || (pfx == "0X")) {
            input += 2;
            past = input;
            while (!past.empty() && isxdigit(*past)) {
                ret = ret * 16 + p_hexd_to_int(*past);
                ++past;
            }
            goto done;
        } else if ((pfx == "0b") || (pfx == "0B")) {
            input += 2;
            past = input;
            while (!past.empty() && ((*past == '0') || (*past == '1'))) {
                ret = ret * 2 + (*past - '0');
                ++past;
            }
            goto done;
        }
    }
    while (!past.empty() && isdigit(*past)) {
        ret = ret * 10 + (*past - '0');
        ++past;
    }
done:
    if (past.equals_front(input)) {
        p_set_end(orig, end);
    } else {
        p_set_end(past, end);
    }
    if (neg) {
        return -ret;
    }
    return ret;
}

template<char e1, char e2>
static inline bool p_read_exp(ostd::ConstCharRange &input, CsInt &fn) {
    if (input.empty()) {
        return true;
    }
    if ((*input != e1) && (*input != e2)) {
        return true;
    }
    ++input;
    if (input.empty()) {
        return false;
    }
    bool neg = p_check_neg(input);
    if (input.empty() || !isdigit(*input)) {
        return false;
    }
    CsInt exp = 0;
    while (!input.empty() && isdigit(*input)) {
        exp = exp * 10 + (*input - '0');
        ++input;
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
        while (!input.empty() && isxdigit(*input)) {
            r = r * 16.0 + double(p_hexd_to_int(*input));
            ++n;
            ++input;
        }
        return r;
    };
    CsInt wn = 0, fn = 0;
    double r = read_hd(0.0, wn);
    if (!input.empty() && (*input == '.')) {
        ++input;
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
        while (!input.empty() && isdigit(*input)) {
            r = r * 10.0 + double(*input - '0');
            ++n;
            ++input;
        }
        return r;
    };
    CsInt wn = 0, fn = 0;
    double r = read_hd(0.0, wn);
    if (!input.empty() && (*input == '.')) {
        ++input;
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

CsFloat cs_parse_float(ostd::ConstCharRange input, ostd::ConstCharRange *end) {
    ostd::ConstCharRange orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return CsFloat(0);
    }
    bool neg = p_check_neg(input);
    CsFloat ret = CsFloat(0);
    if (input.size() >= 2) {
        ostd::ConstCharRange pfx = input.slice(0, 2);
        if ((pfx == "0x") || (pfx == "0X")) {
            input += 2;
            if (!parse_hex_float(input, end, ret)) {
                p_set_end(orig, end);
                return ret;
            }
            goto done;
        }
    }
    if (!parse_dec_float(input, end, ret)) {
        p_set_end(orig, end);
        return ret;
    }
done:
    if (neg) {
        return -ret;
    }
    return ret;
}

} /* namespace cscript */
