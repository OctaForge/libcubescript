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
            cs, "unfinished string '%.*s'", int(beg - orig), orig
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
                it = parse_word(cs, make_str_view(it, end));
                if ((it == end) || (*it != ']')) {
                    throw error{cs, "missing \"]\""};
                }
                break;
            case '(':
                ++it;
                it = parse_word(cs, make_str_view(it, end));
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
    *end = make_str_view(nbeg, nend);
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

/* parse out a quoted string; return the raw string, without the quotes
 * current parser state will be after the final quote
 */
std::string_view parser_state::get_str() {
    size_t nl;
    char const *beg = source;
    source = parse_string(*ts.pstate, make_str_view(source, send), nl);
    current_line += nl - 1;
    auto ret = make_str_view(beg, source);
    return ret.substr(1, ret.size() - 2);
}

/* like the above, but unescapes the string and dups it as a buffer */
charbuf parser_state::get_str_dup() {
    charbuf buf{ts};
    unescape_string(std::back_inserter(buf), get_str());
    return buf;
}

/* a simple name, used for @foo in macro substitutions
 *
 * consists of an alpha character (or '_') followed
 * by alphanumeric characters (or more '_')
 */
std::string_view parser_state::read_macro_name() {
    char const *op = source;
    char c = current();
    if (!isalpha(c) && (c != '_')) {
        return std::string_view{};
    }
    for (; isalnum(c) || (c == '_'); c = current()) {
        next_char();
    }
    return make_str_view(op, source);
}

/* advance the parser until we reach any of the given chars, then stop at it */
char parser_state::skip_until(std::string_view chars) {
    char c = current();
    while (c && (chars.find(c) == std::string_view::npos)) {
        next_char();
        c = current();
    }
    return c;
}

/* advance the parser until we reach the given character, then stop at it */
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
        for (;;) {
            auto c = current();
            if (c && (c != '\n')) {
                next_char();
            } else {
                break;
            }
        }
    }
}

std::string_view parser_state::get_word() {
    char const *beg = source;
    source = parse_word(*ts.pstate, make_str_view(source, send));
    return make_str_view(beg, source);
}

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
            if (!parse_arg(VAL_STRING)) {
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
            return parse_arg(VAL_ANY);
        /* @[...]; like a variable lookup */
        case '[':
            if (!parse_arg(VAL_STRING)) {
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
                    gs.gen_val_block(make_str_view(start, end));
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
                    make_str_view(start, send), curline, VAL_NULL, ']'
                );
                source = ret.second.data();
                send = source + ret.second.size();
                current_line = ret.first;
                return;
            }
            case VAL_IDENT:
                gs.gen_val_ident(make_str_view(start, source - 1));
                return;
            default:
                gs.gen_val_block(make_str_view(start, source - 1));
                goto done;
        }
    }
    gs.gen_val_block(make_str_view(start, source - 1));
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

/* parses a single argument passed to anything
 * this also includes left and right sides in assignments
 * returns if we parsed something
 */
bool parser_state::parse_arg(int ltype, charbuf *word) {
    /* not a part of the grammar */
    skip_comments();
    /* guess what our argument is */
    switch (current()) {
        /* a plain string literal: "..." */
        case '\"':
            switch (ltype) {
                case VAL_POP:
                    get_str();
                    break;
                case VAL_COND: {
                    auto line = current_line;
                    auto s = get_str_dup();
                    if (!s.empty()) {
                        s.push_back('\0');
                        gs.gen_block(s.str_term(), line);
                    } else {
                        gs.gen_val_null();
                    }
                    break;
                }
                case VAL_CODE: {
                    auto line = current_line;
                    auto s = get_str_dup();
                    s.push_back('\0');
                    gs.gen_block(s.str_term(), line);
                    break;
                }
                /* used to begin a statement */
                case VAL_WORD: {
                    *word = get_str_dup();
                    break;
                }
                case VAL_ANY:
                case VAL_STRING:
                    gs.gen_val_string_unescape(get_str());
                    break;
                default: {
                    auto line = current_line;
                    auto s = get_str_dup();
                    s.push_back('\0');
                    gs.gen_val(ltype, s.str_term(), line);
                    break;
                }
            }
            return true;
        /* a lookup: $foo */
        case '$':
            parse_lookup(ltype);
            return true;
        /* an expression: (...) */
        case '(': {
            next_char();
            auto start = gs.count();
            parse_block(VAL_ANY, ')');
            if (gs.count() > start) {
                /* non-empty */
                gs.gen_push_result(ltype);
            } else {
                gs.gen_val(ltype);
                return true;
            }
            switch (ltype) {
                case VAL_POP:
                    gs.gen_pop();
                    break;
                case VAL_COND:
                case VAL_CODE:
                    gs.gen_compile(ltype == VAL_COND);
                    break;
                case VAL_IDENT:
                    gs.gen_ident_lookup();
                    break;
            }
            return true;
        }
        /* a block: [...] */
        case '[':
            next_char();
            parse_blockarg(ltype);
            return true;
        /* something else, presumably a word */
        default:
            break;
    }
    switch (ltype) {
        case VAL_POP:
            return !get_word().empty();
        case VAL_COND:
        case VAL_CODE: {
            auto line = current_line;
            auto s = get_word();
            if (s.empty()) {
                return false;
            }
            gs.gen_block(s, line);
            return true;
        }
        case VAL_WORD: {
            auto s = get_word();
            if (s.empty()) {
                return false;
            }
            word->clear();
            word->append(s);
            return true;
        }
        default: {
            auto line = current_line;
            auto s = get_word();
            if (s.empty()) {
                return false;
            }
            gs.gen_val(ltype, s, line);
            return true;
        }
    }
}

static bool parse_cmd_arg(parser_state &ps, char s, bool more, bool rep) {
    switch (s) {
        case 's': /* string */
            if (more) {
                more = ps.parse_arg(VAL_STRING);
            }
            if (!more && !rep) {
                ps.gs.gen_val_string();
            }
            return more;
        case 'i': /* integer */
            if (more) {
                more = ps.parse_arg(VAL_INT);
            }
            if (!more && !rep) {
                ps.gs.gen_val_integer();
            }
            return more;
        case 'b': /* integer, INT_MIN default */
            if (more) {
                more = ps.parse_arg(VAL_INT);
            }
            if (!more && !rep) {
                ps.gs.gen_val_integer(std::numeric_limits<integer_type>::min());
            }
            return more;
        case 'f': /* float */
            if (more) {
                more = ps.parse_arg(VAL_FLOAT);
            }
            if (!more && !rep) {
                ps.gs.gen_val_float();
            }
            return more;
        case 'F': /* float, prev-argument default */
            if (more) {
                more = ps.parse_arg(VAL_FLOAT);
            }
            if (!more && !rep) {
                ps.gs.gen_dup(VAL_FLOAT);
            }
            return more;
        case 't': /* any arg */
            if (more) {
                more = ps.parse_arg(VAL_ANY);
            }
            if (!more && !rep) {
                ps.gs.gen_val_null();
            }
            return more;
        case 'E': /* condition */
            if (more) {
                more = ps.parse_arg(VAL_COND);
            }
            if (!more && !rep) {
                ps.gs.gen_val_null();
            }
            return more;
        case 'e': /* code */
            if (more) {
                more = ps.parse_arg(VAL_CODE);
            }
            if (!more && !rep) {
                ps.gs.gen_block();
            }
            return more;
        case 'r': /* ident */
            if (more) {
                more = ps.parse_arg(VAL_IDENT);
            }
            if (!more && !rep) {
                ps.gs.gen_val_ident();
            }
            return more;
        default:
            return more;
    }
}

bool parser_state::parse_call_command(
    command_impl *id, ident &self, int rettype
) {
    std::uint32_t comtype = BC_INST_COM, numargs = 0, fakeargs = 0;
    auto fmt = id->get_args();
    bool more = true, rep = false;
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case 's': /* string */
                more = parse_cmd_arg(*this, 's', more, rep);
                if (more && ((it + 1) == fmt.end())) {
                    int numconc = 1;
                    for (;;) {
                        more = parse_arg(VAL_STRING);
                        if (!more) {
                            break;
                        }
                        numconc++;
                    }
                    if (numconc > 1) {
                        gs.gen_concat(numconc, true, VAL_STRING);
                    }
                } else if (!more) {
                    if (!rep) {
                        ++fakeargs;
                    } else {
                        break;
                    }
                }
                ++numargs;
                break;
            case '$': /* self */
                gs.gen_val_ident(self);
                ++numargs;
                break;
            case 'N': /* number of arguments */
                gs.gen_val_integer(numargs - fakeargs);
                ++numargs;
                break;
            case 'C': /* concatenated string */
            case 'V': /* varargs */
                comtype = (*it == 'C') ? BC_INST_COM_C : BC_INST_COM_V;
                if (more) {
                    for (;;) {
                        more = parse_arg(VAL_ANY);
                        if (!more) {
                            break;
                        }
                        ++numargs;
                    }
                }
                break;
            case '1': case '2': case '3': case '4': /* vararg repetition */
                if (more) {
                    int numrep = *it - '0' + 1;
                    it -= numrep;
                    rep = true;
                }
                break;
            default:
                more = parse_cmd_arg(*this, *it, more, rep);
                if (!more) {
                    if (!rep) {
                        ++fakeargs;
                    } else {
                        break;
                    }
                }
                ++numargs;
                break;
        }
    }
    gs.gen_command_call(*id, comtype, rettype, numargs);
    return more;
}

bool parser_state::parse_call_alias(alias &id) {
    bool more;
    std::uint32_t numargs = 0;
    for (;;) {
        more = parse_arg(VAL_ANY);
        if (!more) {
            break;
        }
        ++numargs;
    }
    gs.gen_alias_call(id, numargs);
    return more;
}

bool parser_state::parse_id_local() {
    bool more;
    std::uint32_t numargs = 0;
    for (;;) {
        more = parse_arg(VAL_IDENT);
        if (!more) {
            break;
        }
        numargs++;
    }
    gs.gen_local(numargs);
    return more;
}

bool parser_state::parse_id_do(bool args, int ltype) {
    bool more = parse_arg(VAL_CODE);
    if (!more) {
        gs.gen_result_null(ltype);
    } else {
        gs.gen_do(args, ltype);
    }
    return more;
}

bool parser_state::parse_id_if(ident &id, int ltype) {
    /* condition */
    bool more = parse_arg(VAL_ANY);
    if (!more) {
        /* no condition: expr is nothing */
        gs.gen_result_null(ltype);
    } else {
        auto tpos = gs.count();
        /* true block */
        more = parse_arg(VAL_CODE);
        if (!more) {
            /* we only had condition: expr is nothing */
            gs.gen_pop();
            gs.gen_result_null(ltype);
        } else {
            auto fpos = gs.count();
            /* false block */
            more = parse_arg(VAL_CODE);
            if (!gs.gen_if(tpos, more ? fpos : 0)) {
                /* can't fully compile: use a call */
                gs.gen_command_call(id, BC_INST_COM, ltype);
            }
        }
    }
    return more;
}

bool parser_state::parse_id_and_or(ident &id, int ltype) {
    std::uint32_t numargs = 0;
    /* first */
    bool more = parse_arg(VAL_COND);
    if (!more) {
        /* no first: generate true or false */
        if (ident_p{id}.impl().p_type == ID_AND) {
            gs.gen_result_true(ltype);
        } else {
            gs.gen_result_false(ltype);
        }
    } else {
        numargs++;
        std::size_t start = gs.count(), end = start;
        for (;;) {
            /* keep going as long as we only get blocks */
            more = parse_arg(VAL_COND);
            if (!more) {
                break;
            }
            numargs++;
            if (!gs.is_block(end)) {
                break;
            }
            end = gs.count();
        }
        if (more) {
            /* last parsed thing was not a block, fall back to call */
            for (;;) {
                /* but first, parse out the remainder of values */
                more = parse_arg(VAL_COND);
                if (!more) {
                    break;
                }
                numargs++;
            }
            gs.gen_command_call(id, BC_INST_COM_V, ltype, numargs);
        } else {
            /* all blocks and nothing left */
            gs.gen_and_or((ident_p{id}.impl().p_type != ID_AND), start);
        }
    }
    return more;
}

static bool finish_statement(parser_state &ps, bool more, int term) {
    /* skip through any remaining args in the statement */
    if (more) {
        while (ps.parse_arg(VAL_POP)) {}
    }
    /* handle special characters */
    switch (ps.skip_until(")];/\n")) {
        /* EOS */
        case '\0':
            if (ps.current() != term) {
                throw error{*ps.ts.pstate, "missing \"%c\"", char(term)};
            }
            return false;
        /* terminating parens/brackets */
        case ')':
        case ']':
            /* if the expected terminator, finish normally */
            if (ps.current() == term) {
                ps.next_char();
                return false;
            }
            throw error{*ps.ts.pstate, "unexpected \"%c\"", ps.current()};
        /* potential comment */
        case '/':
            ps.next_char();
            if (ps.current() == '/') {
                ps.skip_until('\n');
            }
            return finish_statement(ps, false, term);
        /* next statement */
        default:
            ps.next_char();
            break;
    }
    /* advance to next statement */
    return true;
}

bool parser_state::parse_call_id(ident &id, int ltype) {
    switch (ident_p{id}.impl().p_type) {
        case ID_ALIAS:
            return parse_call_alias(static_cast<alias &>(id));
        case ID_COMMAND:
            return parse_call_command(
                static_cast<command_impl *>(&id), id, ltype
            );
        case ID_LOCAL:
            return parse_id_local();
        case ID_DO:
            return parse_id_do(false, ltype);
        case ID_DOARGS:
            return parse_id_do(true, ltype);
        case ID_IF:
            return parse_id_if(id, ltype);
        case ID_BREAK:
            gs.gen_break();
            return true;
        case ID_CONTINUE:
            gs.gen_continue();
            return true;
        case ID_RESULT: {
            bool more = parse_arg(VAL_ANY);
            if (!more) {
                gs.gen_result_null(ltype);
            } else {
                gs.gen_result(ltype);
            }
            return more;
        }
        case ID_NOT: {
            bool more = parse_arg(VAL_ANY);
            if (!more) {
                gs.gen_result_true(ltype);
            } else {
                gs.gen_not(ltype);
            }
            return more;
        }
        case ID_AND:
        case ID_OR:
            return parse_id_and_or(id, ltype);
        case ID_IVAR: {
            auto *hid = ts.istate->cmd_ivar;
            return parse_call_command(
                static_cast<command_impl *>(hid), id, ltype
            );
        }
        case ID_FVAR: {
            auto *hid = ts.istate->cmd_fvar;
            return parse_call_command(
                static_cast<command_impl *>(hid), id, ltype
            );
        }
        case ID_SVAR: {
            auto *hid = ts.istate->cmd_svar;
            return parse_call_command(
                static_cast<command_impl *>(hid), id, ltype
            );
        }
        default:
            /* unreachable */
            break;
    }
    return true;
}

/* generates a call to an unknown entity on the stack */
static bool parse_no_id(parser_state &ps, int term) {
    std::uint32_t nargs = 0;
    /* the entity is already on the stack, parse out any arguments to it */
    while (ps.parse_arg(VAL_ANY)) {
        ++nargs;
    }
    ps.gs.gen_call(nargs);
    return finish_statement(ps, false, term);
}

static bool parse_assign_var(
    parser_state &ps, command_impl *id, ident &var, int ltype
) {
    auto fmt = id->get_args();
    std::uint32_t comtype = BC_INST_COM;
    std::uint32_t nargs = 0;
    bool more = true, got = false, rep = false;
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case '$':
                ps.gs.gen_val_ident(var);
                ++nargs;
                break;
            case 'N':
                ps.gs.gen_val_integer(nargs);
                ++nargs;
                break;
            case 'C':
            case 'V':
                comtype = (*it == 'C') ? BC_INST_COM_C : BC_INST_COM_V;
                if (more && !got) {
                    more = ps.parse_arg(VAL_ANY);
                    if (more) {
                        got = true;
                        ++nargs;
                    }
                }
                break;
            case '1': case '2': case '3': case '4':
                if (more && !got) {
                    int numrep = *it - '0' + 1;
                    it -= numrep;
                    rep = true;
                }
                break;
            default: {
                auto gotarg = parse_cmd_arg(ps, *it, got ? false : more, rep);
                if (!got) {
                    more = gotarg;
                }
                if (gotarg) {
                    ++nargs;
                    got = true;
                }
                break;
            }
        }
    }
    ps.gs.gen_command_call(*id, comtype, ltype, nargs);
    return more;
}

bool parser_state::parse_assign(
    charbuf &idname, int ltype, int term, bool &noass
) {
    /* lookahead */
    switch (current(1)) {
        /* the = can be followed by a bunch of stuff
         * some of these result in empty assignments
         */
        case '/': /* a comment maybe? */
            if (current(2) != '/') {
                /* not a comment */
                noass = true;
                return true;
            }
            [[fallthrough]];
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\0': {
            /* skip = */
            next_char();
            /* we had a name on the left hand side */
            if (!idname.empty()) {
                idname.push_back('\0');
                /* fetch an ident or make up a fresh one (unknown alias) */
                ident &id = ts.istate->new_ident(
                    *ts.pstate, idname.str_term(), IDENT_FLAG_UNKNOWN
                );
                /* check what we're assigning */
                switch (id.get_type()) {
                    case ident_type::ALIAS: {
                        /* alias assignment: parse out any one argument */
                        bool more = parse_arg(VAL_ANY);
                        if (!more) {
                            gs.gen_val_string();
                        }
                        gs.gen_assign_alias(id);
                        return finish_statement(*this, more, term);
                    }
                    case ident_type::IVAR: {
                        auto *hid = ts.istate->cmd_ivar;
                        bool more = parse_assign_var(
                            *this, static_cast<command_impl *>(hid), id, ltype
                        );
                        return finish_statement(*this, more, term);
                    }
                    case ident_type::FVAR: {
                        auto *hid = ts.istate->cmd_fvar;
                        bool more = parse_assign_var(
                            *this, static_cast<command_impl *>(hid), id, ltype
                        );
                        return finish_statement(*this, more, term);
                    }
                    case ident_type::SVAR: {
                        auto *hid = ts.istate->cmd_svar;
                        bool more = parse_assign_var(
                            *this, static_cast<command_impl *>(hid), id, ltype
                        );
                        return finish_statement(*this, more, term);
                    }
                    default:
                        break;
                }
                gs.gen_val_string(idname.str_term());
            }
            /* unknown thing, make it the VM's problem */
            bool more = parse_arg(VAL_ANY);
            if (!more) {
                gs.gen_val_string();
            }
            gs.gen_assign();
            return finish_statement(*this, more, term);
        }
        /* not followed by any of these: not an assignment */
        default:
            noass = true;
            return true;
    }
    return true;
}

void parser_state::parse_block(int ltype, int term) {
    charbuf idname{gs.ts};
    /* the main statement parse loop */
    for (;;) {
        /* first, skip any comments in the way and prepare the env */
        skip_comments();
        idname.clear();
        std::size_t curline = current_line;
        bool more = true;
        /* parse the left hand side of the statement */
        if (!parse_arg(VAL_WORD, &idname)) {
            if (!finish_statement(*this, more, term)) {
                return;
            }
            continue;
        }
        skip_comments();
        /* potentially an assignment */
        if (current() == '=') {
            bool noass = false;
            if (!parse_assign(idname, ltype, term, noass)) {
                /* terminated */
                return;
            }
            if (!noass) {
                /* was actually an assignment */
                continue;
            }
        }
        /* we didn't get a name to look up: treat as unknown */
        if (idname.empty()) {
            if (!parse_no_id(*this, term)) {
                return;
            }
            continue;
        }
        idname.push_back('\0');
        auto idstr = idname.str_term();
        ident *id = ts.pstate->get_ident(idstr);
        if (!id) {
            /* no such ident exists but the name is valid, which means
             * it's a syntactically ok call, make it the VM's problem
             */
            if (is_valid_name(idstr)) {
                /* VAL_WORD does not codegen, put the name on the stack */
                gs.gen_val_string(idstr);
                if (!parse_no_id(*this, term)) {
                    return;
                }
                continue;
            }
            /* not a valid command name: treat like an expression */
            switch (ltype) {
                case VAL_ANY: {
                    auto end = idstr;
                    auto val = parse_int(idstr, &end);
                    if (!end.empty()) {
                        gs.gen_val_string(idstr);
                    } else {
                        gs.gen_val_integer(val);
                    }
                    break;
                }
                default:
                    gs.gen_val(ltype, idname.str_term(), int(curline));
                    break;
            }
            gs.gen_result();
            continue;
        }
        /* the ident exists; treat like a call according to its type */
        more = parse_call_id(*id, ltype);
        if (!finish_statement(*this, more, term)) {
            return;
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
