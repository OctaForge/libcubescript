#include <cubescript/cubescript.hh>

#include <cmath>
#include <cctype>

#include "cs_std.hh"

namespace cscript {

/* string/word parsers are also useful to have public */

LIBCUBESCRIPT_EXPORT char const *cs_parse_string(
    cs_state &cs, std::string_view str, size_t &nlines
) {
    size_t nl = 0;
    nlines = nl;
    if (str.empty() || (str.front() != '\"')) {
        return str.data();
    }
    char const *beg = str.begin();
    char const *end = str.end();
    char const *orig = beg++;
    ++nl;
    while (beg != end) {
        switch (*beg) {
            case '\r':
            case '\n':
            case '\"':
                goto end;
            case '^':
            case '\\': {
                bool needn = (*beg == '\\');
                if (++beg == end) {
                    goto end;
                }
                if ((*beg == '\r') || (*beg == '\n')) {
                    char c = *beg++;
                    ++nl;
                    if ((beg != end) && (c == '\r') && (*beg == '\n')) {
                        ++beg;
                    }
                } else if (needn) {
                    goto end;
                } else {
                    ++beg;
                }
                continue;
            }
            default:
                break;
        }
        ++beg;
    }
end:
    nlines = nl;
    if ((beg == end) || (*beg != '\"')) {
        throw cs_error{
            cs, "unfinished string '%s'",
            std::string_view{orig, std::size_t(beg - orig)}
        };
    }
    return ++beg;
}

LIBCUBESCRIPT_EXPORT char const *cs_parse_word(
    cs_state &cs, std::string_view str
) {
    char const *it = str.begin();
    char const *end = str.end();
    for (; it != end; ++it) {
        std::string_view chrs{"\"/;()[] \t\r\n"};
        it = std::find_first_of(it, end, chrs.begin(), chrs.end());
        if (it == end) {
            return it;
        }
        switch (*it) {
            case '"':
            case ';':
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                return it;
            case '/':
                if (((end - it) > 1) && (it[1] == '/')) {
                    return it;
                }
                break;
            case '[':
                ++it;
                it = cs_parse_word(cs, std::string_view{
                    it, std::size_t(end - it)
                });
                if ((it == end) || (*it != ']')) {
                    throw cs_error{cs, "missing \"]\""};
                }
                break;
            case '(':
                ++it;
                it = cs_parse_word(cs, std::string_view{
                    it, std::size_t(end - it)
                });
                if ((it == end) || (*it != ')')) {
                    throw cs_error{cs, "missing \")\""};
                }
                break;
            case ']':
            case ')':
                return it;
        }
    }
    return it;
}

static inline char const *p_skip_white(char const *beg, char const *end) {
    while ((beg != end) && isspace(*beg)) {
        ++beg;
    }
    return beg;
}

static inline void p_set_end(
    char const *nbeg, char const *nend, std::string_view *end
) {
    if (!end) {
        return;
    }
    *end = std::string_view{nbeg, nend};
}
/* this function assumes the input is definitely a hex digit */
static inline cs_int p_hexd_to_int(char c) {
    if (c >= 97) { /* a-f */
        return (c - 'a') + 10;
    } else if (c >= 65) { /* A-F */
        return (c - 'A') + 10;
    }
    /* 0-9 */
    return c - '0';
}

static inline bool p_check_neg(char const *&input) {
    bool neg = (*input == '-');
    if (neg || (*input == '+')) {
        ++input;
    }
    return neg;
}

cs_int parse_int(std::string_view input, std::string_view *endstr) {
    char const *beg = input.begin();
    char const *end = input.end();
    char const *orig = beg;
    beg = p_skip_white(beg, end);
    if (beg == end) {
        p_set_end(orig, end, endstr);
        return cs_int(0);
    }
    bool neg = p_check_neg(beg);
    cs_int ret = 0;
    char const *past = beg;
    if ((end - beg) >= 2) {
        std::string_view pfx = std::string_view{beg, 2};
        if ((pfx == "0x") || (pfx == "0X")) {
            beg += 2;
            past = beg;
            while ((past != end) && std::isxdigit(*past)) {
                ret = ret * 16 + p_hexd_to_int(*past++);
            }
            goto done;
        } else if ((pfx == "0b") || (pfx == "0B")) {
            beg += 2;
            past = beg;
            while ((past != end) && ((*past == '0') || (*past == '1'))) {
                ret = ret * 2 + (*past++ - '0');
            }
            goto done;
        }
    }
    while ((past != end) && std::isdigit(*past)) {
        ret = ret * 10 + (*past++ - '0');
    }
done:
    p_set_end((past == beg) ? orig : past, end, endstr);
    if (neg) {
        return -ret;
    }
    return ret;
}

template<bool Hex, char e1 = Hex ? 'p' : 'e', char e2 = Hex ? 'P' : 'E'>
static inline bool p_read_exp(char const *&beg, char const *end, cs_int &fn) {
    if (beg == end) {
        return true;
    }
    if ((*beg != e1) && (*beg != e2)) {
        return true;
    }
    if (++beg == end) {
        return false;
    }
    bool neg = p_check_neg(beg);
    if ((beg == end) || !std::isdigit(*beg)) {
        return false;
    }
    cs_int exp = 0;
    while ((beg != end) && std::isdigit(*beg)) {
        exp = exp * 10 + (*beg++ - '0');
    }
    if (neg) {
        exp = -exp;
    }
    fn += exp;
    return true;
}

template<bool Hex>
static inline bool parse_gen_float(
    char const *&beg, char const *end, std::string_view *endstr, cs_float &ret
) {
    auto read_digits = [&beg, end](double r, cs_int &n) {
        while (
            (beg != end) &&
            (Hex ? std::isxdigit(*beg) : std::isdigit(*beg))
        ) {
            if (Hex) {
                r = r * 16.0 + double(p_hexd_to_int(*beg));
            } else {
                r = r * 10.0 + double(*beg - '0');
            }
            ++n;
            ++beg;
        }
        return r;
    };
    cs_int wn = 0, fn = 0;
    double r = read_digits(0.0, wn);
    if ((beg != end) && (*beg == '.')) {
        ++beg;
        r = read_digits(r, fn);
    }
    if (!wn && !fn) {
        return false;
    }
    fn = -fn;
    p_set_end(beg, end, endstr); /* we have a valid number until here */
    if (p_read_exp<Hex>(beg, end, fn)) {
        p_set_end(beg, end, endstr);
    }
    if (Hex) {
        ret = cs_float(ldexp(r, fn * 4));
    } else {
        ret = cs_float(r * pow(10, fn));
    }
    return true;
}

cs_float parse_float(std::string_view input, std::string_view *endstr) {
    char const *beg = input.begin();
    char const *end = input.end();
    char const *orig = beg;
    beg = p_skip_white(beg, end);
    if (beg == end) {
        p_set_end(orig, end, endstr);
        return cs_float(0);
    }
    bool neg = p_check_neg(beg);
    cs_float ret = cs_float(0);
    if ((end - beg) >= 2) {
        std::string_view pfx = std::string_view{beg, 2};
        if ((pfx == "0x") || (pfx == "0X")) {
            beg += 2;
            if (!parse_gen_float<true>(beg, end, endstr, ret)) {
                p_set_end(orig, end, endstr);
                return ret;
            }
            goto done;
        }
    }
    if (!parse_gen_float<false>(beg, end, endstr, ret)) {
        p_set_end(orig, end, endstr);
        return ret;
    }
done:
    if (neg) {
        return -ret;
    }
    return ret;
}

bool is_valid_name(std::string_view s) {
    /* names cannot start with numbers (clashes with numeric literals) */
    if (std::isdigit(s[0])) {
        return false;
    }
    switch (s[0]) {
        /* more numeric literal clashes */
        case '+':
        case '-':
            return std::isdigit(s[1]) || ((s[1] == '.') && std::isdigit(s[2]));
        case '.':
            return std::isdigit(s[1]) != 0;
        /* other than that a name can be mostly anything */
        default:
            return true;
    }
}

/* list parser public implementation */

LIBCUBESCRIPT_EXPORT bool cs_list_parser::parse() {
    skip_until_item();
    if (p_input_beg == p_input_end) {
        return false;
    }
    switch (*p_input_beg) {
        case '"': {
            char const *qi = p_input_beg;
            p_input_beg = cs_parse_string(*p_state, get_input());
            p_quoted_item = std::string_view{qi, p_input_beg};
            p_item = p_quoted_item.substr(1, p_quoted_item.size() - 2);
            break;
        }
        case '(':
        case '[': {
            char btype = *p_input_beg;
            int brak = 1;
            char const *ibeg = p_input_beg++;
            for (;;) {
                std::string_view chrs{"\"/;()[]"};
                p_input_beg = std::find_first_of(
                    p_input_beg, p_input_end, chrs.begin(), chrs.end()
                );
                if (p_input_beg == p_input_end) {
                    return true;
                }
                char c = *p_input_beg++;
                switch (c) {
                    case '"':
                        /* the quote is needed in str parsing */
                        --p_input_beg;
                        p_input_beg = cs_parse_string(*p_state, get_input());
                        break;
                    case '/':
                        if (
                            (p_input_beg != p_input_end) &&
                            (*p_input_beg == '/')
                        ) {
                            p_input_beg = std::find(
                                p_input_beg, p_input_end, '\n'
                            );
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
            p_item = std::string_view{ibeg + 1, p_input_beg - 1};
            p_quoted_item = std::string_view{ibeg, p_input_beg};
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            char const *e = cs_parse_word(*p_state, get_input());
            p_quoted_item = p_item = std::string_view{p_input_beg, e};
            p_input_beg = e;
            break;
        }
    }
    skip_until_item();
    if ((p_input_beg != p_input_end) && (*p_input_beg == ';')) {
        ++p_input_beg;
    }
    return true;
}

LIBCUBESCRIPT_EXPORT std::size_t cs_list_parser::count() {
    size_t ret = 0;
    while (parse()) {
        ++ret;
    }
    return ret;
}

LIBCUBESCRIPT_EXPORT cs_strref cs_list_parser::get_item() const {
    if (!p_quoted_item.empty() && (p_quoted_item.front() == '"')) {
        cs_charbuf buf{*p_state};
        cs_unescape_string(std::back_inserter(buf), p_item);
        return cs_strref{*p_state, buf.str()};
    }
    return cs_strref{*p_state, p_item};
}

LIBCUBESCRIPT_EXPORT void cs_list_parser::skip_until_item() {
    for (;;) {
        while (p_input_beg != p_input_end) {
            char c = *p_input_beg;
            if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
                ++p_input_beg;
            } else {
                break;
            }
        }
        if ((p_input_end - p_input_beg) < 2) {
            break;
        }
        if ((p_input_beg[0] != '/') || (p_input_beg[1]) != '/') {
            break;
        }
        p_input_beg = std::find(p_input_beg, p_input_end, '\n');
    }
}

} /* namespace cscript */
