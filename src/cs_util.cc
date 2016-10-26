#include "cubescript/cubescript.hh"
#include "cs_util.hh"

#include <ctype.h>
#include <math.h>

namespace cscript {

static inline void p_skip_white(ostd::ConstCharRange &v) {
    while (!v.empty() && isspace(*v)) {
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

template<bool Hex, char e1 = Hex ? 'p' : 'e', char e2 = Hex ? 'P' : 'E'>
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

template<bool Hex>
static inline bool parse_gen_float(
    ostd::ConstCharRange input, ostd::ConstCharRange *end, CsFloat &ret
) {
    auto read_digits = [&input](double r, CsInt &n) {
        while (!input.empty() && (Hex ? isxdigit(*input) : isdigit(*input))) {
            if (Hex) {
                r = r * 16.0 + double(p_hexd_to_int(*input));
            } else {
                r = r * 10.0 + double(*input - '0');
            }
            ++n;
            ++input;
        }
        return r;
    };
    CsInt wn = 0, fn = 0;
    double r = read_digits(0.0, wn);
    if (!input.empty() && (*input == '.')) {
        ++input;
        r = read_digits(r, fn);
    }
    if (!wn && !fn) {
        return false;
    }
    fn = -fn;
    p_set_end(input, end); /* we have a valid number until here */
    if (p_read_exp<Hex>(input, fn)) {
        p_set_end(input, end);
    }
    if (Hex) {
        ret = CsFloat(ldexp(r, fn * 4));
    } else {
        ret = CsFloat(r * pow(10, fn));
    }
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
            if (!parse_gen_float<true>(input, end, ret)) {
                p_set_end(orig, end);
                return ret;
            }
            goto done;
        }
    }
    if (!parse_gen_float<false>(input, end, ret)) {
        p_set_end(orig, end);
        return ret;
    }
done:
    if (neg) {
        return -ret;
    }
    return ret;
}

namespace util {
    OSTD_EXPORT ostd::ConstCharRange parse_string(
        CsState &cs, ostd::ConstCharRange str, ostd::Size &nlines
    ) {
        ostd::Size nl = 0;
        nlines = nl;
        if (str.empty() || (*str != '\"')) {
            return str;
        }
        ostd::ConstCharRange orig = str;
        ++str;
        ++nl;
        while (!str.empty()) {
            switch (*str) {
                case '\r':
                case '\n':
                case '\"':
                    goto end;
                case '^':
                case '\\': {
                    bool needn = (*str == '\\');
                    ++str;
                    if (str.empty()) {
                        goto end;
                    }
                    if ((*str == '\r') || (*str == '\n')) {
                        char c = *str;
                        ++str;
                        ++nl;
                        if (!str.empty() && (c == '\r') && (*str == '\n')) {
                            ++str;
                        }
                    } else if (needn) {
                        goto end;
                    } else {
                        ++str;
                    }
                    continue;
                }
            }
            ++str;
        }
end:
        nlines = nl;
        if (str.empty() || (*str != '\"')) {
            throw CsErrorException(
                cs, "unfinished string '%s'", ostd::slice_until(orig, str)
            );
        }
        return str + 1;
    }

    OSTD_EXPORT ostd::ConstCharRange parse_word(
        CsState &cs, ostd::ConstCharRange str
    ) {
        for (;;) {
            str = ostd::find_one_of(str, ostd::ConstCharRange("\"/;()[] \t\r\n"));
            if (str.empty()) {
                return str;
            }
            switch (*str) {
                case '"':
                case ';':
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                    return str;
                case '/':
                    if ((str.size() > 1) && (str[1] == '/')) {
                        return str;
                    }
                    break;
                case '[':
                    str = parse_word(cs, str + 1);
                    if (str.empty() || (*str != ']')) {
                        throw CsErrorException(cs, "missing \"]\"");
                    }
                    break;
                case '(':
                    str = parse_word(cs, str + 1);
                    if (str.empty() || (*str != ')')) {
                        throw CsErrorException(cs, "missing \")\"");
                    }
                    break;
                case ']':
                case ')':
                    return str;
            }
            ++str;
        }
        return str;
    }

    void ListParser::skip() {
        for (;;) {
            while (!p_input.empty()) {
                char c = *p_input;
                if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
                    ++p_input;
                } else {
                    break;
                }
            }
            if ((p_input.size() < 2) || (p_input[0] != '/') || (p_input[1] != '/')) {
                break;
            }
            p_input = ostd::find(p_input, '\n');
        }
    }

    bool ListParser::parse() {
        skip();
        if (p_input.empty()) {
            return false;
        }
        switch (*p_input) {
            case '"':
                quote = p_input;
                p_input = parse_string(p_state, p_input);
                quote = ostd::slice_until(quote, p_input);
                item = quote.slice(1, quote.size() - 1);
                break;
            case '(':
            case '[': {
                quote = p_input;
                ++p_input;
                item = p_input;
                char btype = *quote;
                int brak = 1;
                for (;;) {
                    p_input = ostd::find_one_of(
                        p_input, ostd::ConstCharRange("\"/;()[]")
                    );
                    if (p_input.empty()) {
                        return true;
                    }
                    char c = *p_input;
                    ++p_input;
                    switch (c) {
                        case '"':
                            p_input = parse_string(p_state, p_input);
                            break;
                        case '/':
                            if (!p_input.empty() && (*p_input == '/')) {
                                p_input = ostd::find(p_input, '\n');
                            }
                            break;
                        case '(':
                        case '[':
                            brak += (c == btype);
                            break;
                        case ')':
                            if ((btype == '(') && (--brak <= 0)) {
                                goto endblock;
                            }
                            break;
                        case ']':
                            if ((btype == '[') && (--brak <= 0)) {
                                goto endblock;
                            }
                            break;
                    }
                }
endblock:
                item = ostd::slice_until(item, p_input);
                item.pop_back();
                quote = ostd::slice_until(quote, p_input);
                break;
            }
            case ')':
            case ']':
                return false;
            default: {
                ostd::ConstCharRange e = parse_word(p_state, p_input);
                quote = item = ostd::slice_until(p_input, e);
                p_input = e;
                break;
            }
        }
        skip();
        if (!p_input.empty() && (*p_input == ';')) {
            ++p_input;
        }
        return true;
    }

    ostd::Size ListParser::parse(ostd::Size n) {
        ostd::Size ret = 0;
        for (ostd::Size i = 0; i < n; ++i) {
            if (!parse()) {
                return ret;
            }
            ++ret;
        }
        return ret;
    }

    ostd::Size ListParser::count() {
        ostd::Size ret = 0;
        while (parse()) {
            ++ret;
        }
        return ret;
    }
} /* namespace util */

} /* namespace cscript */
