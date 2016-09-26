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
    static ostd::ConstCharRange cs_parse_str(
        CsState &cs, ostd::ConstCharRange str
    ) {
        ostd::ConstCharRange orig = str;
        ++str;
        while (!str.empty()) {
            switch (*str) {
                case '\r':
                case '\n':
                case '\"':
                    goto end;
                case '^':
                    ++str;
                    if (!str.empty()) {
                        break;
                    }
                    goto end;
                case '\\':
                    ++str;
                    if (!str.empty() && ((*str == '\r') || (*str == '\n'))) {
                        char c = *str;
                        ++str;
                        if (!str.empty() && (c == '\r') && (*str == '\n')) {
                            ++str;
                        }
                    }
                    continue;
            }
            ++str;
        }
end:
        if (str.empty() || (*str != '\"')) {
            throw CsErrorException(
                cs, "unfinished string '%s'", ostd::slice_until(orig, str)
            );
        }
        return str + 1;
    }

    static ostd::ConstCharRange cs_parse_word(ostd::ConstCharRange str) {
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
                    str = cs_parse_word(str + 1);
                    if (str.empty() || (*str != ']')) {
                        return str;
                    }
                    break;
                case '(':
                    str = cs_parse_word(str + 1);
                    if (str.empty() || (*str != ')')) {
                        return str;
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
            while (!input.empty()) {
                char c = *input;
                if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
                    ++input;
                } else {
                    break;
                }
            }
            if ((input.size() < 2) || (input[0] != '/') || (input[1] != '/')) {
                break;
            }
            input = ostd::find(input, '\n');
        }
    }

    bool ListParser::parse() {
        skip();
        if (input.empty()) {
            return false;
        }
        switch (*input) {
            case '"':
                quote = input;
                input = cs_parse_str(p_state, input);
                quote = ostd::slice_until(quote, input);
                item = quote.slice(1, quote.size() - 1);
                break;
            case '(':
            case '[': {
                quote = input;
                ++input;
                item = input;
                char btype = *quote;
                int brak = 1;
                for (;;) {
                    input = ostd::find_one_of(
                        input, ostd::ConstCharRange("\"/;()[]")
                    );
                    if (input.empty()) {
                        return true;
                    }
                    char c = *input;
                    ++input;
                    switch (c) {
                        case '"':
                            input = cs_parse_str(p_state, input);
                            break;
                        case '/':
                            if (!input.empty() && (*input == '/')) {
                                input = ostd::find(input, '\n');
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
                item = ostd::slice_until(item, input);
                item.pop_back();
                quote = ostd::slice_until(quote, input);
                break;
            }
            case ')':
            case ']':
                return false;
            default: {
                ostd::ConstCharRange e = cs_parse_word(input);
                quote = item = ostd::slice_until(input, e);
                input = e;
                break;
            }
        }
        skip();
        if (!input.empty() && (*input == ';')) {
            ++input;
        }
        return true;
    }

    CsString ListParser::element() {
        CsString s;
        s.reserve(item.size());
        if (!quote.empty() && (*quote == '"')) {
            auto writer = s.iter_cap();
            util::unescape_string(writer, item);
            writer.put('\0');
        } else {
            memcpy(s.data(), item.data(), item.size());
            s[item.size()] = '\0';
        }
        s.advance(item.size());
        return s;
    }

    ostd::Size list_length(CsState &cs, ostd::ConstCharRange s) {
        ListParser p(cs, s);
        ostd::Size ret = 0;
        while (p.parse()) {
            ++ret;
        }
        return ret;
    }

    ostd::Maybe<CsString> list_index(
        CsState &cs, ostd::ConstCharRange s, ostd::Size idx
    ) {
        ListParser p(cs, s);
        for (ostd::Size i = 0; i < idx; ++i) {
            if (!p.parse()) {
                return ostd::nothing;
            }
        }
        if (!p.parse()) {
            return ostd::nothing;
        }
        return ostd::move(p.element());
    }

    CsVector<CsString> list_explode(
        CsState &cs, ostd::ConstCharRange s, ostd::Size limit
    ) {
        CsVector<CsString> ret;
        ListParser p(cs, s);
        while ((ret.size() < limit) && p.parse()) {
            ret.push(ostd::move(p.element()));
        }
        return ret;
    }
} /* namespace util */

} /* namespace cscript */
