#include <cubescript/cubescript.hh>
#include "cs_util.hh"
#include "cs_vm.hh"

#include <ctype.h>
#include <math.h>

namespace cscript {

static inline void p_skip_white(ostd::string_range &v) {
    while (!v.empty() && isspace(*v)) {
        ++v;
    }
}

static inline void p_set_end(
    const ostd::string_range &v, ostd::string_range *end
) {
    if (!end) {
        return;
    }
    *end = v;
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

static inline bool p_check_neg(ostd::string_range &input) {
    bool neg = (*input == '-');
    if (neg || (*input == '+')) {
        ++input;
    }
    return neg;
}

cs_int cs_parse_int(ostd::string_range input, ostd::string_range *end) {
    ostd::string_range orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return cs_int(0);
    }
    bool neg = p_check_neg(input);
    cs_int ret = 0;
    ostd::string_range past = input;
    if (input.size() >= 2) {
        ostd::string_range pfx = input.slice(0, 2);
        if ((pfx == "0x") || (pfx == "0X")) {
            input = input.slice(2, input.size());
            past = input;
            while (!past.empty() && isxdigit(*past)) {
                ret = ret * 16 + p_hexd_to_int(*past);
                ++past;
            }
            goto done;
        } else if ((pfx == "0b") || (pfx == "0B")) {
            input = input.slice(2, input.size());
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
    if (&past[0] == &input[0]) {
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
static inline bool p_read_exp(ostd::string_range &input, cs_int &fn) {
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
    cs_int exp = 0;
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
    ostd::string_range input, ostd::string_range *end, cs_float &ret
) {
    auto read_digits = [&input](double r, cs_int &n) {
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
    cs_int wn = 0, fn = 0;
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
        ret = cs_float(ldexp(r, fn * 4));
    } else {
        ret = cs_float(r * pow(10, fn));
    }
    return true;
}

cs_float cs_parse_float(ostd::string_range input, ostd::string_range *end) {
    ostd::string_range orig = input;
    p_skip_white(input);
    if (input.empty()) {
        p_set_end(orig, end);
        return cs_float(0);
    }
    bool neg = p_check_neg(input);
    cs_float ret = cs_float(0);
    if (input.size() >= 2) {
        ostd::string_range pfx = input.slice(0, 2);
        if ((pfx == "0x") || (pfx == "0X")) {
            input = input.slice(2, input.size());
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

/* string manager */

inline cs_strref_state *get_ref_state(char const *ptr) {
    return const_cast<cs_strref_state *>(
        reinterpret_cast<cs_strref_state const *>(ptr)
    ) - 1;
}

char const *cs_strman::add(ostd::string_range str) {
    auto it = counts.find(str);
    /* already present: just increment ref */
    if (it != counts.end()) {
        auto *st = it->second;
        /* having a null pointer is the same as non-existence */
        if (st) {
            ++st->refcount;
            return reinterpret_cast<char const *>(st + 1);
        }
    }
    /* not present: allocate brand new data */
    auto ss = str.size();
    auto mem = cstate->alloc(nullptr, 0, ss + sizeof(cs_strref_state) + 1);
    if (!mem) {
        throw cs_internal_error{"allocation failed"};
    }
    /* write length and refcount */
    auto *sst = static_cast<cs_strref_state *>(mem);
    sst->length = ss;
    sst->refcount = 1;
    /* write string data */
    auto *strp = reinterpret_cast<char *>(sst + 1);
    memcpy(strp, str.data(), ss);
    /* terminated for best compatibility */
    strp[ss] = '\0';
    /* store it */
    counts.emplace(ostd::string_range{strp, strp + ss}, sst);
    return strp;
}

char const *cs_strman::ref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    ++ss->refcount;
    return ptr;
}

void cs_strman::unref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    if (!--ss->refcount) {
        /* refcount zero, so ditch it
         * this path is a little slow...
         */
        auto sr = ostd::string_range{ptr, ptr + ss->length};
        auto it = counts.find(sr);
        if (it == counts.end()) {
            /* internal error: this should *never* happen */
            throw cs_internal_error{"no refcount"};
        }
        /* we're freeing the key */
        counts.erase(it);
        /* dealloc */
        cstate->alloc(ss, ss->length + sizeof(cs_strref_state) + 1, 0);
    }
}

char const *cs_strman::find(ostd::string_range str) const {
    auto it = counts.find(str);
    if (it == counts.end()) {
        return nullptr;
    }
    return reinterpret_cast<char const *>(it->second + 1);
}

ostd::string_range cs_strman::get(char const *ptr) const {
    auto *ss = get_ref_state(ptr);
    return ostd::string_range{ptr, ptr + ss->length};
}

namespace util {
    OSTD_EXPORT ostd::string_range parse_string(
        cs_state &cs, ostd::string_range str, size_t &nlines
    ) {
        size_t nl = 0;
        nlines = nl;
        if (str.empty() || (*str != '\"')) {
            return str;
        }
        ostd::string_range orig = str;
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
            throw cs_error(
                cs, "unfinished string '%s'", orig.slice(0, &str[0] - &orig[0])
            );
        }
        str.pop_front();
        return str;
    }

    OSTD_EXPORT ostd::string_range parse_word(
        cs_state &cs, ostd::string_range str
    ) {
        for (;;) {
            str = ostd::find_one_of(str, ostd::string_range("\"/;()[] \t\r\n"));
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
                    str.pop_front();
                    str = parse_word(cs, str);
                    if (str.empty() || (*str != ']')) {
                        throw cs_error(cs, "missing \"]\"");
                    }
                    break;
                case '(':
                    str.pop_front();
                    str = parse_word(cs, str);
                    if (str.empty() || (*str != ')')) {
                        throw cs_error(cs, "missing \")\"");
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

    void list_parser::skip() {
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

    bool list_parser::parse() {
        skip();
        if (p_input.empty()) {
            return false;
        }
        switch (*p_input) {
            case '"':
                p_quote = p_input;
                p_input = parse_string(p_state, p_input);
                p_quote = p_quote.slice(0, &p_input[0] - &p_quote[0]);
                p_item = p_quote.slice(1, p_quote.size() - 1);
                break;
            case '(':
            case '[': {
                p_quote = p_input;
                ++p_input;
                p_item = p_input;
                char btype = *p_quote;
                int brak = 1;
                for (;;) {
                    p_input = ostd::find_one_of(
                        p_input, ostd::string_range("\"/;()[]")
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
                p_item = p_item.slice(0, &p_input[0] - &p_item[0]);
                p_item.pop_back();
                p_quote = p_quote.slice(0, &p_input[0] - &p_quote[0]);
                break;
            }
            case ')':
            case ']':
                return false;
            default: {
                ostd::string_range e = parse_word(p_state, p_input);
                p_quote = p_item = p_input.slice(0, &e[0] - &p_input[0]);
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

    size_t list_parser::count() {
        size_t ret = 0;
        while (parse()) {
            ++ret;
        }
        return ret;
    }
} /* namespace util */

} /* namespace cscript */
