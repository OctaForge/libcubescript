#include <cubescript/cubescript.hh>

#include <cmath>
#include <cctype>
#include <limits>
#include <iterator>

#include "cs_parser.hh"

namespace cubescript {

/* string/word parsers are also useful to have public */

LIBCUBESCRIPT_EXPORT char const *parse_string(
    state &cs, std::string_view str, size_t &nlines
) {
    size_t nl = 0;
    nlines = nl;
    if (str.empty() || (str.front() != '\"')) {
        return str.data();
    }
    char const *beg = &str[0];
    char const *end = &str[str.size()];
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
        throw error{
            cs, "unfinished string '%s'",
            std::string_view{orig, std::size_t(beg - orig)}
        };
    }
    return ++beg;
}

LIBCUBESCRIPT_EXPORT char const *parse_word(
    state &cs, std::string_view str
) {
    char const *it = &str[0];
    char const *end = &str[str.size()];
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
                    throw error{cs, "missing \"]\""};
                }
                break;
            case '(':
                ++it;
                it = parse_word(cs, std::string_view{
                    it, std::size_t(end - it)
                });
                if ((it == end) || (*it != ')')) {
                    throw error{cs, "missing \")\""};
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
static inline integer_type p_hexd_to_int(char c) {
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

integer_type parse_int(std::string_view input, std::string_view *endstr) {
    char const *beg = &input[0];
    char const *end = &input[input.size()];
    char const *orig = beg;
    beg = p_skip_white(beg, end);
    if (beg == end) {
        p_set_end(orig, end, endstr);
        return integer_type(0);
    }
    bool neg = p_check_neg(beg);
    integer_type ret = 0;
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
static inline bool p_read_exp(char const *&beg, char const *end, integer_type &fn) {
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
    integer_type exp = 0;
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
    char const *&beg, char const *end, std::string_view *endstr, float_type &ret
) {
    auto read_digits = [&beg, end](double r, integer_type &n) {
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
    integer_type wn = 0, fn = 0;
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
        ret = float_type(ldexp(r, fn * 4));
    } else {
        ret = float_type(r * pow(10, fn));
    }
    return true;
}

float_type parse_float(std::string_view input, std::string_view *endstr) {
    char const *beg = &input[0];
    char const *end = &input[input.size()];
    char const *orig = beg;
    beg = p_skip_white(beg, end);
    if (beg == end) {
        p_set_end(orig, end, endstr);
        return float_type(0);
    }
    bool neg = p_check_neg(beg);
    float_type ret = float_type(0);
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

std::string_view parser_state::get_str() {
    size_t nl;
    char const *beg = source;
    source = parse_string(
        *ts.pstate, std::string_view{source, std::size_t(send - source)}, nl
    );
    current_line += nl - 1;
    auto ret = std::string_view{beg, std::size_t(source - beg)};
    return ret.substr(1, ret.size() - 2);
}

charbuf parser_state::get_str_dup() {
    charbuf buf{ts};
    unescape_string(std::back_inserter(buf), get_str());
    return buf;
}

std::string_view parser_state::read_macro_name() {
    char const *op = source;
    char c = current();
    if (!isalpha(c) && (c != '_')) {
        return std::string_view{};
    }
    for (; isalnum(c) || (c == '_'); c = current()) {
        next_char();
    }
    return std::string_view{op, std::size_t(source - op)};
}

char parser_state::skip_until(std::string_view chars) {
    char c = current();
    while (c && (chars.find(c) == std::string_view::npos)) {
        next_char();
        c = current();
    }
    return c;
}

char parser_state::skip_until(char cf) {
    char c = current();
    while (c && (c != cf)) {
        next_char();
        c = current();
    }
    return c;
}

static bool is_hspace(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r');
}

void parser_state::skip_comments() {
    for (;;) {
        for (char c = current(); is_hspace(c); c = current()) {
            next_char();
        }
        if (current() == '\\') {
            char c = current(1);
            if ((c != '\r') && (c != '\n')) {
                throw error{*ts.pstate, "invalid line break"};
            }
            /* skip backslash */
            next_char();
            /* skip CR or LF */
            next_char();
            /* when CR, try also skipping LF; covers \r, \n, \r\n */
            if ((c == '\r') && (current(1) == '\n')) {
                next_char();
            }
            /* skip whitespace on new line */
            continue;
        }
        if ((current() != '/') || (current(1) != '/')) {
            return;
        }
        while (current() != '\n') {
            next_char();
        }
    }
}

std::string_view parser_state::get_word() {
    char const *beg = source;
    source = parse_word(
        *ts.pstate, std::string_view{source, std::size_t(send - source)}
    );
    return std::string_view{beg, std::size_t(source - beg)};
}

static bool compilearg(
    parser_state &gs, int wordtype, charbuf *word = nullptr
);

/* lookups that are invalid but not causing an error */
static void lookup_invalid(gen_state &gs, int ltype) {
    switch (ltype) {
        case VAL_POP:
            break;
        case VAL_NULL:
        case VAL_ANY:
        case VAL_WORD:
        case VAL_COND:
            gs.gen_val_null();
            break;
        default:
            gs.gen_val(ltype);
            break;
    }
}

/* on success, handles non-value return types not handled by type mask */
static void lookup_done(gen_state &gs, int ltype) {
    switch (ltype) {
        case VAL_POP:
            gs.gen_pop();
            break;
        case VAL_CODE:
            gs.gen_compile();
            break;
        case VAL_COND:
            gs.gen_compile(true);
            break;
        case VAL_IDENT:
            gs.gen_ident_lookup();
            break;
    }
}

/* parses $foo */
void parser_state::parse_lookup(int ltype) {
    charbuf lookup{gs.ts};
    next_char(); /* skip $ */
    switch (current()) {
        /* $(...), $[...] */
        case '(':
        case '[':
            if (!compilearg(*this, VAL_STRING)) {
                lookup_invalid(gs, ltype);
                return;
            }
            gs.gen_lookup_ident(ltype);
            lookup_done(gs, ltype);
            return;
        /* $$...; sub-lookup */
        case '$':
            parse_lookup(VAL_STRING);
            gs.gen_lookup_ident(ltype);
            lookup_done(gs, ltype);
            return;
        /* $"..."; like $foo but looser */
        case '\"':
            lookup = get_str_dup();
            lookup.push_back('\0');
            goto lookup_id;
        /* any other thing, presumably a valid name */
        default:
            break;
    }
    lookup.append(get_word());
    if (lookup.empty()) {
        lookup_invalid(gs, ltype);
        return;
    }
    lookup.push_back('\0');
lookup_id:
    /* fetch an ident or prepare a fresh one */
    ident &id = ts.istate->new_ident(
        *ts.pstate, lookup.str_term(), IDENT_FLAG_UNKNOWN
    );
    switch (id.get_type()) {
        case ident_type::IVAR:
            switch (ltype) {
                case VAL_CODE:
                case VAL_IDENT:
                    gs.gen_lookup_ivar(id, ltype);
                    break;
                default:
                    return;
            }
            lookup_done(gs, ltype);
            return;
        case ident_type::FVAR:
            switch (ltype) {
                case VAL_CODE:
                case VAL_IDENT:
                    gs.gen_lookup_fvar(id, ltype);
                    break;
                default:
                    return;
            }
            lookup_done(gs, ltype);
            return;
        case ident_type::SVAR:
            switch (ltype) {
                case VAL_POP:
                    return;
                default:
                    gs.gen_lookup_svar(id, ltype);
                    break;
            }
            lookup_done(gs, ltype);
            return;
        case ident_type::ALIAS:
            switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_COND:
                    gs.gen_lookup_alias(id, ltype);
                    break;
                default:
                    gs.gen_lookup_alias(id, ltype, VAL_STRING);
            }
            lookup_done(gs, ltype);
            return;
        case ident_type::COMMAND: {
            std::uint32_t comtype = BC_INST_COM, numargs = 0;
            auto fmt = static_cast<command_impl &>(id).get_args();
            for (char c: fmt) {
                switch (c) {
                    case 's':
                        gs.gen_val_string(std::string_view{});
                        numargs++;
                        break;
                    case 'i':
                        gs.gen_val_integer();
                        numargs++;
                        break;
                    case 'b':
                        gs.gen_val_integer(
                            std::numeric_limits<integer_type>::min()
                        );
                        numargs++;
                        break;
                    case 'f':
                        gs.gen_val_float();
                        numargs++;
                        break;
                    case 'F':
                        gs.gen_dup(VAL_FLOAT);
                        numargs++;
                        break;
                    case 'E':
                    case 't':
                        gs.gen_val_null();
                        numargs++;
                        break;
                    case 'e':
                        gs.gen_block();
                        numargs++;
                        break;
                    case 'r':
                        gs.gen_val_ident();
                        numargs++;
                        break;
                    case '$':
                        gs.gen_val_ident(id);
                        numargs++;
                        break;
                    case 'N':
                        gs.gen_val_integer(-1);
                        numargs++;
                        break;
                    case 'C':
                        comtype = BC_INST_COM_C;
                        break;
                    case 'V':
                        comtype = BC_INST_COM_V;
                        break;
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        break;
                }
            }
            gs.gen_command_call(id, comtype, ltype, numargs);
            gs.gen_push_result(ltype);
            lookup_done(gs, ltype);
            return;
        }
        default:
            break;
    }
    lookup_invalid(gs, ltype);
}

/* parses @... macro substitutions within block strings
 *
 * only called from within parse_blockarg
 */
bool parser_state::parse_subblock() {
    charbuf lookup{ts};
    switch (current()) {
        /* @(...) */
        case '(':
            return compilearg(*this, VAL_ANY);
        /* @[...]; like a variable lookup */
        case '[':
            if (!compilearg(*this, VAL_STRING)) {
                return false;
            }
            gs.gen_lookup_ident();
            return true;
        /* @"..."; like the above but easier (no inner compiles) */
        case '\"':
            lookup = get_str_dup();
            lookup.push_back('\0');
            goto lookup_id;
        /* anything else, presumably a valid name */
        default:
            break;
    }
    lookup.append(read_macro_name());
    if (lookup.empty()) {
        return false;
    }
    lookup.push_back('\0');
lookup_id:
    ident &id = ts.istate->new_ident(
        *ts.pstate, lookup.str_term(), IDENT_FLAG_UNKNOWN
    );
    switch (id.get_type()) {
        case ident_type::IVAR:
            gs.gen_lookup_ivar(id);
            return true;
        case ident_type::FVAR:
            gs.gen_lookup_fvar(id);
            return true;
        case ident_type::SVAR:
            gs.gen_lookup_svar(id);
            return true;
        case ident_type::ALIAS:
            gs.gen_lookup_alias(id);
            return true;
        default:
            break;
    }
    gs.gen_val_string(lookup.str_term());
    gs.gen_lookup_ident();
    return true;
}

/* [...] argument parser */
void parser_state::parse_blockarg(int ltype) {
    char const *start = source;
    /* current bracket level */
    std::size_t blevel = 1;
    std::size_t curline = current_line;
    /* number of strings to concatenate */
    std::size_t concs = 0;
    while (blevel > 0) {
        /* skip until a significant character */
        switch (skip_until("@\"/[]")) {
            /* EOS */
            case '\0':
                throw error{*ts.pstate, "missing \"]\""};
            /* inner string, parse through it */
            case '\"':
                get_str();
                break;
            /* possibly a comment */
            case '/':
                next_char();
                if (current() == '/') {
                    /* yup, just go until we reach a newline */
                    skip_until('\n');
                }
                break;
            /* keep it nice and balanced */
            case '[':
                next_char();
                ++blevel;
                break;
            case ']':
                next_char();
                --blevel;
                break;
            /* macro substitution */
            case '@': {
                char const *end = source;
                std::size_t alevel = 0;
                while (current() == '@') {
                    ++alevel;
                    next_char();
                }
                if (blevel > alevel) {
                    /* deeper block level than macro level
                     * we can't substitute at this point so leave it alone
                     */
                     continue;
                }
                if (blevel < alevel) {
                    /* shallower block level than macro level
                     * this is an error, we can't substitute this
                     */
                     throw error{*ts.pstate, "too many @s"};
                }
                /* generate a block string for everything until now */
                if (start != end) {
                    gs.gen_val_block(std::string_view{start, end});
                    ++concs;
                }
                if (parse_subblock()) {
                    ++concs;
                }
                if (concs) {
                    start = source;
                    curline = current_line;
                }
                break;
            }
            /* actually unreachable, we handle all incl. EOS */
            default:
                next_char();
                break;
        }
    }
    if ((source - 1) <= start) {
        /* possibly empty */
        goto done;
    }
    /* non-empty */
    if (!concs) {
        /* one contiguous string */
        switch (ltype) {
            case VAL_POP:
                /* ignore */
                return;
            case VAL_CODE:
            case VAL_COND: {
                /* compile */
                auto ret = gs.gen_block(
                    std::string_view{start, send}, curline, VAL_NULL, ']'
                );
                source = ret.second.data();
                send = source + ret.second.size();
                current_line = ret.first;
                return;
            }
            case VAL_IDENT:
                gs.gen_val_ident(std::string_view{start, source - 1});
                return;
            default:
                gs.gen_val_block(std::string_view{start, source - 1});
                goto done;
        }
    }
    gs.gen_val_block(std::string_view{start, source - 1});
    /* concat the pieces */
    gs.gen_concat(++concs, false, ltype);
done:
    bool got_val = (concs || ((source - 1) > start));
    /* handle the result */
    switch (ltype) {
        case VAL_POP:
            if (got_val) {
                gs.gen_pop();
            }
            break;
        case VAL_COND:
            if (!got_val) {
                gs.gen_val_null();
            } else {
                gs.gen_compile(true);
            }
            break;
        case VAL_CODE:
            if (!got_val) {
                gs.gen_block();
            } else {
                gs.gen_compile();
            }
            break;
        case VAL_IDENT:
            if (!got_val) {
                gs.gen_val_ident();
            } else {
                gs.gen_ident_lookup();
            }
            break;
        case VAL_STRING:
        case VAL_NULL:
        case VAL_ANY:
        case VAL_WORD:
            if (!got_val) {
                gs.gen_val_string();
            }
            break;
        default:
            if (!concs) {
                if ((source - 1) <= start) {
                    gs.gen_val(ltype);
                } else {
                    gs.gen_force(ltype);
                }
            }
            break;
    }
}

static bool compilearg(
    parser_state &gs, int wordtype, charbuf *word
) {
    gs.skip_comments();
    switch (gs.current()) {
        case '\"':
            switch (wordtype) {
                case VAL_POP:
                    gs.get_str();
                    break;
                case VAL_COND: {
                    size_t line = gs.current_line;
                    auto s = gs.get_str_dup();
                    if (!s.empty()) {
                        s.push_back('\0');
                        gs.gs.gen_block(s.str_term(), line);
                    } else {
                        gs.gs.gen_val_null();
                    }
                    break;
                }
                case VAL_CODE: {
                    auto s = gs.get_str_dup();
                    s.push_back('\0');
                    gs.gs.gen_block(s.str_term(), gs.current_line);
                    break;
                }
                case VAL_WORD:
                    if (word) {
                        *word = gs.get_str_dup();
                    }
                    break;
                case VAL_ANY:
                case VAL_STRING:
                    gs.gs.gen_val_string_unescape(gs.get_str());
                    break;
                default: {
                    int line = int(gs.current_line);
                    auto s = gs.get_str_dup();
                    s.push_back('\0');
                    gs.gs.gen_val(wordtype, s.str_term(), line);
                    break;
                }
            }
            return true;
        case '$':
            gs.parse_lookup(wordtype);
            return true;
        case '(': {
            gs.next_char();
            auto start = gs.gs.count();
            gs.parse_block(VAL_ANY, ')');
            if (gs.gs.count() > start) {
                gs.gs.gen_push_result(wordtype);
            } else {
                gs.gs.gen_val(wordtype);
                return true;
            }
            switch (wordtype) {
                case VAL_POP:
                    gs.gs.gen_pop();
                    break;
                case VAL_COND:
                    gs.gs.gen_compile(true);
                    break;
                case VAL_CODE:
                    gs.gs.gen_compile();
                    break;
                case VAL_IDENT:
                    gs.gs.gen_ident_lookup();
                    break;
            }
            return true;
        }
        case '[':
            gs.next_char();
            gs.parse_blockarg(wordtype);
            return true;
        default:
            switch (wordtype) {
                case VAL_POP: {
                    return !gs.get_word().empty();
                }
                case VAL_COND: {
                    size_t line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    gs.gs.gen_block(s, line);
                    return true;
                }
                case VAL_CODE: {
                    size_t line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    gs.gs.gen_block(s, line);
                    return true;
                }
                case VAL_WORD: {
                    auto w = gs.get_word();
                    if (word) {
                        word->clear();
                        word->append(w);
                    }
                    return !w.empty();
                }
                default: {
                    int line = int(gs.current_line);
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    gs.gs.gen_val(wordtype, s, line);
                    return true;
                }
            }
    }
}

static void compile_cmd(
    parser_state &gs, command_impl *id, ident &self, bool &more, int rettype,
    std::uint32_t limit = 0
) {
    std::uint32_t comtype = BC_INST_COM, numargs = 0, numcargs = 0, fakeargs = 0;
    bool rep = false;
    auto fmt = id->get_args();
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case 's': /* string */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_STRING);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_string();
                    fakeargs++;
                } else if ((it + 1) == fmt.end()) {
                    int numconc = 1;
                    for (;;) {
                        more = compilearg(gs, VAL_STRING);
                        if (!more) {
                            break;
                        }
                        numconc++;
                    }
                    if (numconc > 1) {
                        gs.gs.gen_concat(numconc, true, VAL_STRING);
                    }
                }
                numargs++;
                numcargs++;
                break;
            case 'i': /* integer */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_INT);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_integer();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'b': /* integer, INT_MIN default */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_INT);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_integer(std::numeric_limits<integer_type>::min());
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'f': /* float */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_FLOAT);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_float();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'F': /* float, prev-argument default */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_FLOAT);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_dup(VAL_FLOAT);
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 't': /* any arg */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_ANY);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_null();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'E': /* condition */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_COND);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_null();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'e': /* code */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_CODE);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_block();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case 'r': /* ident */
                if (more && (!limit || (numcargs < limit))) {
                    more = compilearg(gs, VAL_IDENT);
                }
                if (!more || (limit && (numcargs >= limit))) {
                    if (rep) {
                        break;
                    }
                    gs.gs.gen_val_ident();
                    fakeargs++;
                }
                numargs++;
                numcargs++;
                break;
            case '$': /* self */
                gs.gs.gen_val_ident(self);
                numargs++;
                break;
            case 'N': /* number of arguments */
                gs.gs.gen_val_integer(numargs - fakeargs);
                numargs++;
                break;
            case 'C': /* concatenated string */
                comtype = BC_INST_COM_C;
                if (more && (!limit || (numcargs < limit))) {
                    for (;;) {
                        more = compilearg(gs, VAL_ANY);
                        if (!more || (limit && (numcargs >= limit))) {
                            break;
                        }
                        numargs++;
                        numcargs++;
                    }
                }
                break;
            case 'V': /* varargs */
                comtype = BC_INST_COM_V;
                if (more && (!limit || (numcargs < limit))) {
                    for(;;) {
                        more = compilearg(gs, VAL_ANY);
                        if (!more || (limit && (numcargs >= limit))) {
                            break;
                        }
                        numargs++;
                        numcargs++;
                    }
                }
                break;
            case '1': /* vararg repetition */
            case '2':
            case '3':
            case '4':
                if (more && (!limit || (numcargs < limit))) {
                    int numrep = *it - '0' + 1;
                    it -= numrep;
                    rep = true;
                }
                break;
        }
    }
    gs.gs.gen_command_call(*id, comtype, rettype, numargs);
}

static void compile_alias(parser_state &gs, alias *id, bool &more) {
    std::uint32_t numargs = 0;
    for (;;) {
        more = compilearg(gs, VAL_ANY);
        if (!more) {
            break;
        }
        ++numargs;
    }
    gs.gs.gen_alias_call(*id, numargs);
}

static void compile_local(parser_state &gs, bool &more) {
    std::uint32_t numargs = 0;
    if (more) {
        for (;;) {
            more = compilearg(gs, VAL_IDENT);
            if (!more) {
                break;
            }
            numargs++;
        }
    }
    gs.gs.gen_local(numargs);
}

static void compile_do(
    parser_state &gs, bool args, int rettype, bool &more
) {
    if (more) {
        more = compilearg(gs, VAL_CODE);
    }
    if (!more) {
        gs.gs.gen_result_null(rettype);
    } else {
        gs.gs.gen_do(args, rettype);
    }
}

static void compile_if(
    parser_state &gs, ident *id, bool &more, int rettype
) {
    if (more) {
        /* condition */
        more = compilearg(gs, VAL_ANY);
    }
    if (!more) {
        /* no condition: expr is nothing */
        gs.gs.gen_result_null(rettype);
    } else {
        auto tpos = gs.gs.count();
        /* true block */
        more = compilearg(gs, VAL_CODE);
        if (!more) {
            /* we only had condition: expr is nothing */
            gs.gs.gen_pop();
            gs.gs.gen_result_null(rettype);
        } else {
            auto fpos = gs.gs.count();
            /* false block */
            more = compilearg(gs, VAL_CODE);
            if (!gs.gs.gen_if(tpos, more ? fpos : 0)) {
                /* can't fully compile: use a call */
                gs.gs.gen_command_call(*id, BC_INST_COM, rettype);
            }
        }
    }
}

static void compile_and_or(
    parser_state &gs, ident *id, bool &more, int rettype
) {
    std::uint32_t numargs = 0;
    if (more) {
        /* first */
        more = compilearg(gs, VAL_COND);
    }
    if (!more) {
        /* no first: generate true or false */
        if (ident_p{*id}.impl().p_type == ID_AND) {
            gs.gs.gen_result_true(rettype);
        } else {
            gs.gs.gen_result_false(rettype);
        }
    } else {
        numargs++;
        std::size_t start = gs.gs.count(), end = start;
        for (;;) {
            /* keep going as long as we only get blocks */
            more = compilearg(gs, VAL_COND);
            if (!more) {
                break;
            }
            numargs++;
            if (!gs.gs.is_block(end)) {
                break;
            }
            end = gs.gs.count();
        }
        if (more) {
            /* last parsed thing was not a block, fall back to call */
            for (;;) {
                /* but first, parse out the remainder of values */
                more = compilearg(gs, VAL_COND);
                if (!more) {
                    break;
                }
                numargs++;
            }
            gs.gs.gen_command_call(*id, BC_INST_COM_V, rettype, numargs);
        } else {
            /* all blocks and nothing left */
            gs.gs.gen_and_or((ident_p{*id}.impl().p_type != ID_AND), start);
        }
    }
}

void parser_state::parse_block(int rettype, int brak) {
    charbuf idname{gs.ts};
    for (;;) {
        skip_comments();
        idname.clear();
        size_t curline = current_line;
        bool more = compilearg(*this, VAL_WORD, &idname);
        if (!more) {
            goto endstatement;
        }
        skip_comments();
        if (current() == '=') {
            switch (current(1)) {
                case '/':
                    if (current(2) != '/') {
                        break;
                    }
                    [[fallthrough]];
                case ';':
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                case '\0':
                    next_char();
                    if (!idname.empty()) {
                        idname.push_back('\0');
                        ident &id = ts.istate->new_ident(
                            *ts.pstate, idname.str_term(), IDENT_FLAG_UNKNOWN
                        );
                        switch (id.get_type()) {
                            case ident_type::ALIAS:
                                more = compilearg(*this, VAL_ANY);
                                if (!more) {
                                    gs.gen_val_string();
                                }
                                gs.gen_assign_alias(id);
                                goto endstatement;
                            case ident_type::IVAR: {
                                auto *hid = ts.istate->cmd_ivar;
                                compile_cmd(
                                    *this, static_cast<command_impl *>(hid),
                                    id, more, rettype, 1
                                );
                                goto endstatement;
                            }
                            case ident_type::FVAR: {
                                auto *hid = ts.istate->cmd_fvar;
                                compile_cmd(
                                    *this, static_cast<command_impl *>(hid),
                                    id, more, rettype, 1
                                );
                                goto endstatement;
                            }
                            case ident_type::SVAR: {
                                auto *hid = ts.istate->cmd_svar;
                                compile_cmd(
                                    *this, static_cast<command_impl *>(hid),
                                    id, more, rettype, 1
                                );
                                goto endstatement;
                            }
                            default:
                                break;
                        }
                        gs.gen_val_string(idname.str_term());
                    }
                    more = compilearg(*this, VAL_ANY);
                    if (!more) {
                        gs.gen_val_string();
                    }
                    gs.gen_assign();
                    goto endstatement;
            }
        }
        if (idname.empty()) {
noid:
            std::uint32_t numargs = 0;
            for (;;) {
                more = compilearg(*this, VAL_ANY);
                if (!more) {
                    break;
                }
                ++numargs;
            }
            gs.gen_call(numargs);
        } else {
            idname.push_back('\0');
            ident *id = ts.pstate->get_ident(idname.str_term());
            if (!id) {
                if (is_valid_name(idname.str_term())) {
                    gs.gen_val_string(idname.str_term());
                    goto noid;
                }
                switch (rettype) {
                    case VAL_ANY: {
                        std::string_view end = idname.str_term();
                        integer_type val = parse_int(end, &end);
                        if (!end.empty()) {
                            gs.gen_val_string(idname.str_term());
                        } else {
                            gs.gen_val_integer(val);
                        }
                        break;
                    }
                    default:
                        gs.gen_val(rettype, idname.str_term(), int(curline));
                        break;
                }
                gs.gen_result();
            } else {
                switch (ident_p{*id}.impl().p_type) {
                    case ID_ALIAS:
                        compile_alias(
                            *this, static_cast<alias *>(id), more
                        );
                        break;
                    case ID_COMMAND:
                        compile_cmd(
                            *this, static_cast<command_impl *>(id), *id, more,
                            rettype
                        );
                        break;
                    case ID_LOCAL:
                        compile_local(*this, more);
                        break;
                    case ID_DO:
                        compile_do(*this, false, rettype, more);
                        break;
                    case ID_DOARGS:
                        compile_do(*this, true, rettype, more);
                        break;
                    case ID_IF:
                        compile_if(*this, id, more, rettype);
                        break;
                    case ID_BREAK:
                        gs.gen_break();
                        break;
                    case ID_CONTINUE:
                        gs.gen_continue();
                        break;
                    case ID_RESULT:
                        if (more) {
                            more = compilearg(*this, VAL_ANY);
                        }
                        if (!more) {
                            gs.gen_result_null(rettype);
                        } else {
                            gs.gen_result(rettype);
                        }
                        break;
                    case ID_NOT:
                        if (more) {
                            more = compilearg(*this, VAL_ANY);
                        }
                        if (!more) {
                            gs.gen_result_true(rettype);
                        } else {
                            gs.gen_not(rettype);
                        }
                        break;
                    case ID_AND:
                    case ID_OR:
                        compile_and_or(*this, id, more, rettype);
                        break;
                    case ID_IVAR: {
                        auto *hid = ts.istate->cmd_ivar;
                        compile_cmd(
                            *this, static_cast<command_impl *>(hid),
                            *id, more, rettype
                        );
                        break;
                    }
                    case ID_FVAR: {
                        auto *hid = ts.istate->cmd_fvar;
                        compile_cmd(
                            *this, static_cast<command_impl *>(hid),
                            *id, more, rettype
                        );
                        break;
                    }
                    case ID_SVAR: {
                        auto *hid = ts.istate->cmd_svar;
                        compile_cmd(
                            *this, static_cast<command_impl *>(hid),
                            *id, more, rettype
                        );
                        break;
                    }
                }
            }
        }
endstatement:
        if (more) {
            while (compilearg(*this, VAL_POP));
        }
        switch (skip_until(")];/\n")) {
            case '\0':
                if (current() != brak) {
                    throw error{*ts.pstate, "missing \"%c\"", char(brak)};
                    return;
                }
                return;
            case ')':
            case ']':
                if (current() == brak) {
                    next_char();
                    return;
                }
                throw error{*ts.pstate, "unexpected \"%c\"", current()};
                return;
            case '/':
                next_char();
                if (current() == '/') {
                    skip_until('\n');
                }
                goto endstatement;
            default:
                next_char();
                break;
        }
    }
}

/* list parser public implementation */

LIBCUBESCRIPT_EXPORT bool list_parser::parse() {
    skip_until_item();
    if (p_input_beg == p_input_end) {
        return false;
    }
    switch (*p_input_beg) {
        case '"': {
            char const *qi = p_input_beg;
            p_input_beg = parse_string(*p_state, get_input());
            p_qbeg = qi;
            p_qend = p_input_beg;
            p_ibeg = p_qbeg + 1;
            p_iend = p_qend - 1;
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
                        p_input_beg = parse_string(*p_state, get_input());
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
            p_ibeg = ibeg + 1;
            p_iend = p_input_beg - 1;
            p_qbeg = ibeg;
            p_qend = p_input_beg;
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            char const *e = parse_word(*p_state, get_input());
            p_ibeg = p_qbeg = p_input_beg;
            p_iend = p_qend = e;
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

LIBCUBESCRIPT_EXPORT std::size_t list_parser::count() {
    size_t ret = 0;
    while (parse()) {
        ++ret;
    }
    return ret;
}

LIBCUBESCRIPT_EXPORT string_ref list_parser::get_item() const {
    if ((p_qbeg != p_qend) && (*p_qbeg == '"')) {
        charbuf buf{*p_state};
        unescape_string(std::back_inserter(buf), get_raw_item());
        return string_ref{*p_state, buf.str()};
    }
    return string_ref{*p_state, get_raw_item()};
}

LIBCUBESCRIPT_EXPORT void list_parser::skip_until_item() {
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

} /* namespace cubescript */
