#include <cubescript/cubescript.hh>
#include "cs_util.hh"
#include "cs_vm.hh"

#include <cctype>
#include <cmath>
#include <iterator>
#include <algorithm>

namespace cscript {

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
    *end = std::string_view{nbeg, std::size_t(nend - nbeg)};
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

cs_int cs_parse_int(std::string_view input, std::string_view *endstr) {
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
            while ((past != end) && isxdigit(*past)) {
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
    while ((past != end) && isdigit(*past)) {
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
    if ((beg == end) || !isdigit(*beg)) {
        return false;
    }
    cs_int exp = 0;
    while ((beg != end) && isdigit(*beg)) {
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
        while ((beg != end) && (Hex ? isxdigit(*beg) : isdigit(*beg))) {
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

cs_float cs_parse_float(std::string_view input, std::string_view *endstr) {
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

/* string manager */

inline cs_strref_state *get_ref_state(char const *ptr) {
    return const_cast<cs_strref_state *>(
        reinterpret_cast<cs_strref_state const *>(ptr)
    ) - 1;
}

char const *cs_strman::add(std::string_view str) {
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
    auto strp = alloc_buf(ss);
    /* write string data, it's already pre-terminated */
    memcpy(strp, str.data(), ss);
    /* store it */
    counts.emplace(std::string_view{strp, ss}, get_ref_state(strp));
    return strp;
}

char const *cs_strman::ref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    ++ss->refcount;
    return ptr;
}

char const *cs_strman::steal(char *ptr) {
    auto *ss = get_ref_state(ptr);
    auto sr = std::string_view{ptr, ss->length};
    /* much like add(), but we already have memory */
    auto it = counts.find(sr);
    if (it != counts.end()) {
        auto *st = it->second;
        if (st) {
            ++st->refcount;
            /* the buffer is superfluous now */
            cstate->alloc(ss, ss->length + sizeof(cs_strref_state) + 1, 0);
            return reinterpret_cast<char const *>(st + 1);
        }
    }
    ss->refcount = 1;
    counts.emplace(sr, ss);
    return ptr;
}

void cs_strman::unref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    if (!--ss->refcount) {
        /* refcount zero, so ditch it
         * this path is a little slow...
         */
        auto sr = std::string_view{ptr, ss->length};
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

char const *cs_strman::find(std::string_view str) const {
    auto it = counts.find(str);
    if (it == counts.end()) {
        return nullptr;
    }
    return reinterpret_cast<char const *>(it->second + 1);
}

std::string_view cs_strman::get(char const *ptr) const {
    auto *ss = get_ref_state(ptr);
    return std::string_view{ptr, ss->length};
}

char *cs_strman::alloc_buf(std::size_t len) const {
    auto mem = cstate->alloc(nullptr, 0, len + sizeof(cs_strref_state) + 1);
    if (!mem) {
        throw cs_internal_error{"allocation failed"};
    }
    /* write length and initial refcount */
    auto *sst = static_cast<cs_strref_state *>(mem);
    sst->length = len;
    sst->refcount = 1;
    /* pre-terminate */
    auto *strp = reinterpret_cast<char *>(sst + 1);
    strp[len] = '\0';
    /* now the user can fill it */
    return strp;
};

/* strref */

cs_strref::cs_strref(cs_shared_state &cs, std::string_view str):
    p_state{&cs}
{
    p_str = cs.strman->add(str);
}

cs_strref::cs_strref(cs_state &cs, std::string_view str):
    p_state{cs.p_state}
{
    p_str = p_state->strman->add(str);
}

cs_strref::cs_strref(cs_strref const &ref): p_state{ref.p_state}, p_str{ref.p_str}
{
    p_state->strman->ref(p_str);
}

/* this can be used by friends to do quick cs_strref creation */
cs_strref::cs_strref(char const *p, cs_shared_state &cs):
    p_state{&cs}
{
    p_str = p_state->strman->ref(p);
}

cs_strref::~cs_strref() {
    p_state->strman->unref(p_str);
}

cs_strref &cs_strref::operator=(cs_strref const &ref) {
    p_str = ref.p_str;
    p_state = ref.p_state;
    p_state->strman->ref(p_str);
    return *this;
}

cs_strref::operator std::string_view() const {
    return p_state->strman->get(p_str);
}

bool cs_strref::operator==(cs_strref const &s) const {
    return p_str == s.p_str;
}

namespace util {
    OSTD_EXPORT char const *parse_string(
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
            throw cs_error(
                cs, "unfinished string '%s'",
                std::string_view{orig, std::size_t(beg - orig)}
            );
        }
        return ++beg;
    }

    OSTD_EXPORT char const *parse_word(cs_state &cs, std::string_view str) {
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
                    it = parse_word(cs, std::string_view{
                        it, std::size_t(end - it)
                    });
                    if ((it == end) || (*it != ']')) {
                        throw cs_error(cs, "missing \"]\"");
                    }
                    break;
                case '(':
                    ++it;
                    it = parse_word(cs, std::string_view{
                        it, std::size_t(end - it)
                    });
                    if ((it == end) || (*it != ')')) {
                        throw cs_error(cs, "missing \")\"");
                    }
                    break;
                case ']':
                case ')':
                    return it;
            }
        }
        return it;
    }
} /* namespace util */

OSTD_EXPORT bool list_parse(cs_list_parse_state &ps, cs_state &cs) {
    list_find_item(ps);
    if (ps.input_beg == ps.input_end) {
        return false;
    }
    switch (*ps.input_beg) {
        case '"': {
            char const *qi = ps.input_beg;
            ps.input_beg = util::parse_string(cs, ps.get_input());
            ps.quoted_item = std::string_view{
                qi, std::size_t(ps.input_beg - qi)
            };
            ps.item = ps.quoted_item.substr(1, ps.quoted_item.size() - 2);
            break;
        }
        case '(':
        case '[': {
            char btype = *ps.input_beg;
            int brak = 1;
            char const *ibeg = ps.input_beg++;
            for (;;) {
                std::string_view chrs{"\"/;()[]"};
                ps.input_beg = std::find_first_of(
                    ps.input_beg, ps.input_end, chrs.begin(), chrs.end()
                );
                if (ps.input_beg == ps.input_end) {
                    return true;
                }
                char c = *ps.input_beg++;
                switch (c) {
                    case '"':
                        /* the quote is needed in str parsing */
                        --ps.input_beg;
                        ps.input_beg = util::parse_string(cs, ps.get_input());
                        break;
                    case '/':
                        if (
                            (ps.input_beg != ps.input_end) &&
                            (*ps.input_beg == '/')
                        ) {
                            ps.input_beg = std::find(
                                ps.input_beg, ps.input_end, '\n'
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
            ps.item = std::string_view{
                ibeg + 1, std::size_t(ps.input_beg - ibeg - 2)
            };
            ps.quoted_item = std::string_view{
                ibeg, std::size_t(ps.input_beg - ibeg)
            };
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            char const *e = util::parse_word(cs, ps.get_input());
            ps.quoted_item = ps.item = std::string_view{
                ps.input_beg, std::size_t(e - ps.input_beg)
            };
            ps.input_beg = e;
            break;
        }
    }
    list_find_item(ps);
    if ((ps.input_beg != ps.input_end) && (*ps.input_beg == ';')) {
        ++ps.input_beg;
    }
    return true;
}

OSTD_EXPORT std::size_t list_count(cs_list_parse_state &ps, cs_state &cs) {
    size_t ret = 0;
    while (list_parse(ps, cs)) {
        ++ret;
    }
    return ret;
}

OSTD_EXPORT cs_strref list_get_item(cs_list_parse_state &ps, cs_state &cs) {
    if (!ps.quoted_item.empty() && (ps.quoted_item.front() == '"')) {
        cs_charbuf buf{cs};
        util::unescape_string(std::back_inserter(buf), ps.item);
        return cs_strref{cs, buf.str()};
    }
    return cs_strref{cs, ps.item};
}

OSTD_EXPORT void list_find_item(cs_list_parse_state &ps) {
    for (;;) {
        while (ps.input_beg != ps.input_end) {
            char c = *ps.input_beg;
            if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
                ++ps.input_beg;
            } else {
                break;
            }
        }
        if ((ps.input_end - ps.input_beg) < 2) {
            break;
        }
        if ((ps.input_beg[0] != '/') || (ps.input_beg[1]) != '/') {
            break;
        }
        ps.input_beg = std::find(ps.input_beg, ps.input_end, '\n');
    }
}

OSTD_EXPORT cs_strref value_list_concat(
    cs_state &cs, std::span<cs_value> vals, std::string_view sep
) {
    cs_charbuf buf{cs};
    for (std::size_t i = 0; i < vals.size(); ++i) {
        switch (vals[i].get_type()) {
            case cs_value_type::INT:
            case cs_value_type::FLOAT:
            case cs_value_type::STRING: {
                cs_value v{vals[i]};
                auto str = v.force_str();
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
    return cs_strref{cs, buf.str()};
}

} /* namespace cscript */
