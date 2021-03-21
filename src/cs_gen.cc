#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_util.hh"

#include <ctype.h>

#include <limits>
#include <iterator>

namespace cscript {

std::string_view cs_gen_state::get_str() {
    size_t nl;
    char const *beg = source;
    source = util::parse_string(
        cs, std::string_view{source, std::size_t(send - source)}, nl
    );
    current_line += nl - 1;
    auto ret = std::string_view{beg, std::size_t(source - beg)};
    return ret.substr(1, ret.size() - 2);
}

cs_charbuf cs_gen_state::get_str_dup() {
    cs_charbuf buf{cs};
    util::unescape_string(std::back_inserter(buf), get_str());
    return buf;
}

std::string_view cs_gen_state::read_macro_name() {
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

char cs_gen_state::skip_until(std::string_view chars) {
    char c = current();
    while (c && (chars.find(c) == std::string_view::npos)) {
        next_char();
        c = current();
    }
    return c;
}

char cs_gen_state::skip_until(char cf) {
    char c = current();
    while (c && (c != cf)) {
        next_char();
        c = current();
    }
    return c;
}

static bool cs_is_hspace(char c) {
    return (c == ' ') || (c == '\t') || (c == '\r');
}

void cs_gen_state::skip_comments() {
    for (;;) {
        for (char c = current(); cs_is_hspace(c); c = current()) {
            next_char();
        }
        if (current() == '\\') {
            char c = current(1);
            if ((c != '\r') && (c != '\n')) {
                throw cs_error(cs, "invalid line break");
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

std::string_view cs_gen_state::get_word() {
    char const *beg = source;
    source = util::parse_word(
        cs, std::string_view{source, std::size_t(send - source)}
    );
    return std::string_view{beg, std::size_t(source - beg)};
}

static inline int cs_ret_code(int type, int def = 0) {
    if (type >= CS_VAL_ANY) {
        return (type == CS_VAL_STRING) ? CS_RET_STRING : def;
    }
    return type << CS_CODE_RET;
}

static void compilestatements(
    cs_gen_state &gs, int rettype, int brak = '\0', int prevargs = 0
);
static inline std::pair<std::string_view, size_t> compileblock(
    cs_gen_state &gs, std::string_view p, size_t line,
    int rettype = CS_RET_NULL, int brak = '\0'
);

void cs_gen_state::gen_int(std::string_view word) {
    gen_int(cs_parse_int(word));
}

void cs_gen_state::gen_float(std::string_view word) {
    gen_float(cs_parse_float(word));
}

void cs_gen_state::gen_value(int wordtype, std::string_view word, int line) {
    switch (wordtype) {
        case CS_VAL_ANY:
            if (!word.empty()) {
                gen_str(word);
            } else {
                gen_null();
            }
            break;
        case CS_VAL_STRING:
            gen_str(word);
            break;
        case CS_VAL_FLOAT:
            gen_float(word);
            break;
        case CS_VAL_INT:
            gen_int(word);
            break;
        case CS_VAL_COND:
            if (!word.empty()) {
                compileblock(*this, word, line);
            } else {
                gen_null();
            }
            break;
        case CS_VAL_CODE:
            compileblock(*this, word, line);
            break;
        case CS_VAL_IDENT:
            gen_ident(word);
            break;
        default:
            break;
    }
}

static inline void compileblock(cs_gen_state &gs) {
    gs.code.push_back(CS_CODE_EMPTY);
}

static inline std::pair<std::string_view, size_t> compileblock(
    cs_gen_state &gs, std::string_view p, size_t line, int rettype, int brak
) {
    size_t start = gs.code.size();
    gs.code.push_back(CS_CODE_BLOCK);
    gs.code.push_back(CS_CODE_OFFSET | ((start + 2) << 8));
    size_t retline = line;
    if (!p.empty()) {
        char const *op = gs.source, *oe = gs.send;
        size_t oldline = gs.current_line;
        gs.source = p.data();
        gs.send = p.data() + p.size();
        gs.current_line = line;
        compilestatements(gs, CS_VAL_ANY, brak);
        p = std::string_view{gs.source, std::size_t(gs.send - gs.source)};
        retline = gs.current_line;
        gs.source = op;
        gs.send = oe;
        gs.current_line = oldline;
    }
    if (gs.code.size() > start + 2) {
        gs.code.push_back(CS_CODE_EXIT | rettype);
        gs.code[start] |= uint32_t(gs.code.size() - (start + 1)) << 8;
    } else {
        gs.code.resize(start);
        gs.code.push_back(CS_CODE_EMPTY | rettype);
    }
    return std::make_pair(p, retline);
}

static inline void compileunescapestr(cs_gen_state &gs) {
    auto str = gs.get_str();
    gs.code.push_back(CS_CODE_VAL | CS_RET_STRING);
    gs.code.reserve(
        gs.code.size() + str.size() / sizeof(uint32_t) + 1
    );
    size_t bufs = (gs.code.capacity() - gs.code.size()) * sizeof(uint32_t);
    auto alloc = cs_allocator<char>{gs.cs};
    auto *buf = alloc.allocate(bufs + 1);
    char *wbuf = util::unescape_string(&buf[0], str);
    memset(
        &buf[wbuf - buf], 0,
        sizeof(uint32_t) - (wbuf - buf) % sizeof(uint32_t)
    );
    gs.code.back() |= (wbuf - buf) << 8;
    uint32_t *ubuf = reinterpret_cast<uint32_t *>(buf);
    gs.code.append(ubuf, ubuf + ((wbuf - buf) / sizeof(uint32_t) + 1));
    alloc.deallocate(buf, bufs + 1);
}

static bool compilearg(
    cs_gen_state &gs, int wordtype, int prevargs = MaxResults,
    cs_charbuf *word = nullptr
);

static void compilelookup(cs_gen_state &gs, int ltype, int prevargs = MaxResults) {
    cs_charbuf lookup{gs.cs};
    gs.next_char();
    switch (gs.current()) {
        case '(':
        case '[':
            if (!compilearg(gs, CS_VAL_STRING, prevargs)) {
                goto invalid;
            }
            break;
        case '$':
            compilelookup(gs, CS_VAL_STRING, prevargs);
            break;
        case '\"':
            lookup = gs.get_str_dup();
            lookup.push_back('\0');
            goto lookupid;
        default: {
            lookup.append(gs.get_word());
            if (lookup.empty()) goto invalid;
            lookup.push_back('\0');
lookupid:
            cs_ident *id = gs.cs.new_ident(lookup.str_term());
            if (id) {
                switch (id->get_type()) {
                    case cs_ident_type::IVAR:
                        gs.code.push_back(
                            CS_CODE_IVAR | cs_ret_code(ltype, CS_RET_INT) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CS_VAL_POP:
                                gs.code.pop_back();
                                break;
                            case CS_VAL_CODE:
                                gs.code.push_back(CS_CODE_COMPILE);
                                break;
                            case CS_VAL_IDENT:
                                gs.code.push_back(CS_CODE_IDENT_U);
                                break;
                        }
                        return;
                    case cs_ident_type::FVAR:
                        gs.code.push_back(
                            CS_CODE_FVAR | cs_ret_code(ltype, CS_RET_FLOAT) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CS_VAL_POP:
                                gs.code.pop_back();
                                break;
                            case CS_VAL_CODE:
                                gs.code.push_back(CS_CODE_COMPILE);
                                break;
                            case CS_VAL_IDENT:
                                gs.code.push_back(CS_CODE_IDENT_U);
                                break;
                        }
                        return;
                    case cs_ident_type::SVAR:
                        switch (ltype) {
                            case CS_VAL_POP:
                                return;
                            default:
                                gs.code.push_back(
                                    CS_CODE_SVAR | cs_ret_code(ltype, CS_RET_STRING) |
                                        (id->get_index() << 8)
                                );
                                break;
                        }
                        goto done;
                    case cs_ident_type::ALIAS:
                        switch (ltype) {
                            case CS_VAL_POP:
                                return;
                            case CS_VAL_COND:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CS_CODE_LOOKUP_MARG
                                        : CS_CODE_LOOKUP_M
                                    ) | (id->get_index() << 8)
                                );
                                break;
                            case CS_VAL_CODE:
                            case CS_VAL_IDENT:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CS_CODE_LOOKUP_MARG
                                        : CS_CODE_LOOKUP_M
                                    ) | CS_RET_STRING | (id->get_index() << 8)
                                );
                                break;
                            default:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CS_CODE_LOOKUP_ARG
                                        : CS_CODE_LOOKUP
                                    ) | cs_ret_code(ltype, CS_RET_STRING) |
                                        (id->get_index() << 8)
                                );
                                break;
                        }
                        goto done;
                    case cs_ident_type::COMMAND: {
                        int comtype = CS_CODE_COM, numargs = 0;
                        if (prevargs >= MaxResults) {
                            gs.code.push_back(CS_CODE_ENTER);
                        }
                        auto fmt = static_cast<cs_command_impl *>(id)->get_args();
                        for (char c: fmt) {
                            switch (c) {
                                case 'S':
                                    gs.gen_str();
                                    numargs++;
                                    break;
                                case 's':
                                    gs.gen_str(std::string_view{});
                                    numargs++;
                                    break;
                                case 'i':
                                    gs.gen_int();
                                    numargs++;
                                    break;
                                case 'b':
                                    gs.gen_int(std::numeric_limits<cs_int>::min());
                                    numargs++;
                                    break;
                                case 'f':
                                    gs.gen_float();
                                    numargs++;
                                    break;
                                case 'F':
                                    gs.code.push_back(CS_CODE_DUP | CS_RET_FLOAT);
                                    numargs++;
                                    break;
                                case 'E':
                                case 'T':
                                case 't':
                                    gs.gen_null();
                                    numargs++;
                                    break;
                                case 'e':
                                    compileblock(gs);
                                    numargs++;
                                    break;
                                case 'r':
                                    gs.gen_ident();
                                    numargs++;
                                    break;
                                case '$':
                                    gs.gen_ident(id);
                                    numargs++;
                                    break;
                                case 'N':
                                    gs.gen_int(-1);
                                    numargs++;
                                    break;
                                case 'C':
                                    comtype = CS_CODE_COM_C;
                                    goto compilecomv;
                                case 'V':
                                    comtype = CS_CODE_COM_V;
                                    goto compilecomv;
                                case '1':
                                case '2':
                                case '3':
                                case '4':
                                    break;
                            }
                        }
                        gs.code.push_back(
                            comtype | cs_ret_code(ltype) | (id->get_index() << 8)
                        );
                        gs.code.push_back(
                            (prevargs >= MaxResults
                                ? CS_CODE_EXIT
                                : CS_CODE_RESULT_ARG
                            ) | cs_ret_code(ltype)
                        );
                        goto done;
        compilecomv:
                        gs.code.push_back(
                            comtype | cs_ret_code(ltype) | (numargs << 8) |
                                (id->get_index() << 13)
                        );
                        gs.code.push_back(
                            (prevargs >= MaxResults
                                ? CS_CODE_EXIT
                                : CS_CODE_RESULT_ARG
                            ) | cs_ret_code(ltype)
                        );
                        goto done;
                    }
                    default:
                        goto invalid;
                }
            }
            gs.gen_str(lookup.str_term());
            break;
        }
    }
    switch (ltype) {
        case CS_VAL_COND:
            gs.code.push_back(CS_CODE_LOOKUP_MU);
            break;
        case CS_VAL_CODE:
        case CS_VAL_IDENT:
            gs.code.push_back(CS_CODE_LOOKUP_MU | CS_RET_STRING);
            break;
        default:
            gs.code.push_back(CS_CODE_LOOKUP_U | cs_ret_code(ltype));
            break;
    }
done:
    switch (ltype) {
        case CS_VAL_POP:
            gs.code.push_back(CS_CODE_POP);
            break;
        case CS_VAL_CODE:
            gs.code.push_back(CS_CODE_COMPILE);
            break;
        case CS_VAL_COND:
            gs.code.push_back(CS_CODE_COND);
            break;
        case CS_VAL_IDENT:
            gs.code.push_back(CS_CODE_IDENT_U);
            break;
    }
    return;
invalid:
    switch (ltype) {
        case CS_VAL_POP:
            break;
        case CS_VAL_NULL:
        case CS_VAL_ANY:
        case CS_VAL_WORD:
        case CS_VAL_COND:
            gs.gen_null();
            break;
        default:
            gs.gen_value(ltype);
            break;
    }
}

static bool compileblockstr(cs_gen_state &gs, char const *str, char const *send) {
    int startc = gs.code.size();
    gs.code.push_back(CS_CODE_VAL | CS_RET_STRING);
    gs.code.reserve(gs.code.size() + (send - str) / sizeof(uint32_t) + 1);
    auto alloc = cs_allocator<char>{gs.cs};
    auto asz = ((send - str) / sizeof(uint32_t) + 1) * sizeof(uint32_t);
    char *buf = alloc.allocate(asz);
    int len = 0;
    while (str < send) {
        std::string_view chrs{"\r/\"@]"};
        char const *orig = str;
        str = std::find_first_of(str, send, chrs.begin(), chrs.end());
        memcpy(&buf[len], orig, str - orig);
        len += (str - orig);
        if (str == send) {
            goto done;
        }
        switch (*str) {
            case '\r':
                ++str;
                break;
            case '\"': {
                char const *start = str;
                str = util::parse_string(
                    gs.cs, std::string_view{str, send}
                );
                memcpy(&buf[len], start, std::size_t(str - start));
                len += (str - start);
                break;
            }
            case '/':
                if (((str + 1) != send) && str[1] == '/') {
                    str = std::find(str, send, '\n');
                } else {
                    buf[len++] = *str++;
                }
                break;
            case '@':
            case ']':
                if (str < send) {
                    buf[len++] = *str++;
                } else {
                    goto done;
                }
                break;
        }
    }
done:
    memset(&buf[len], '\0', sizeof(uint32_t) - len % sizeof(uint32_t));
    uint32_t *ubuf = reinterpret_cast<uint32_t *>(buf);
    gs.code.append(ubuf, ubuf + (len / sizeof(uint32_t) + 1));
    gs.code[startc] |= len << 8;
    alloc.deallocate(buf, asz);
    return true;
}

static bool compileblocksub(cs_gen_state &gs, int prevargs) {
    cs_charbuf lookup{gs.cs};
    switch (gs.current()) {
        case '(':
            if (!compilearg(gs, CS_VAL_ANY, prevargs)) {
                return false;
            }
            break;
        case '[':
            if (!compilearg(gs, CS_VAL_STRING, prevargs)) {
                return false;
            }
            gs.code.push_back(CS_CODE_LOOKUP_MU);
            break;
        case '\"':
            lookup = gs.get_str_dup();
            lookup.push_back('\0');
            goto lookupid;
        default: {
            lookup.append(gs.read_macro_name());
            if (lookup.empty()) {
                return false;
            }
            lookup.push_back('\0');
lookupid:
            cs_ident *id = gs.cs.new_ident(lookup.str_term());
            if (id) {
                switch (id->get_type()) {
                    case cs_ident_type::IVAR:
                        gs.code.push_back(CS_CODE_IVAR | (id->get_index() << 8));
                        goto done;
                    case cs_ident_type::FVAR:
                        gs.code.push_back(CS_CODE_FVAR | (id->get_index() << 8));
                        goto done;
                    case cs_ident_type::SVAR:
                        gs.code.push_back(CS_CODE_SVAR | (id->get_index() << 8));
                        goto done;
                    case cs_ident_type::ALIAS:
                        gs.code.push_back(
                            (id->get_index() < MaxArguments
                                ? CS_CODE_LOOKUP_MARG
                                : CS_CODE_LOOKUP_M
                            ) | (id->get_index() << 8)
                        );
                        goto done;
                    default:
                        break;
                }
            }
            gs.gen_str(lookup.str_term());
            gs.code.push_back(CS_CODE_LOOKUP_MU);
done:
            break;
        }
    }
    return true;
}

static void compileblockmain(cs_gen_state &gs, int wordtype, int prevargs) {
    char const *start = gs.source;
    size_t curline = gs.current_line;
    int concs = 0;
    for (int brak = 1; brak;) {
        switch (gs.skip_until("@\"/[]")) {
            case '\0':
                throw cs_error(gs.cs, "missing \"]\"");
                return;
            case '\"':
                gs.get_str();
                break;
            case '/':
                gs.next_char();
                if (gs.current() == '/') {
                    gs.skip_until('\n');
                }
                break;
            case '[':
                gs.next_char();
                brak++;
                break;
            case ']':
                gs.next_char();
                brak--;
                break;
            case '@': {
                char const *esc = gs.source;
                int level = 0;
                while (gs.current() == '@') {
                    ++level;
                    gs.next_char();
                }
                if (brak > level) {
                    continue;
                } else if (brak < level) {
                    throw cs_error(gs.cs, "too many @s");
                    return;
                }
                if (!concs && prevargs >= MaxResults) {
                    gs.code.push_back(CS_CODE_ENTER);
                }
                if (concs + 2 > MaxArguments) {
                    gs.code.push_back(CS_CODE_CONC_W | CS_RET_STRING | (concs << 8));
                    concs = 1;
                }
                if (compileblockstr(gs, start, esc)) {
                    concs++;
                }
                if (compileblocksub(gs, prevargs + concs)) {
                    concs++;
                }
                if (concs) {
                    start = gs.source;
                    curline = gs.current_line;
                } else if (prevargs >= MaxResults) {
                    gs.code.pop_back();
                }
                break;
            }
            default:
                gs.next_char();
                break;
        }
    }
    if (gs.source - 1 > start) {
        if (!concs) {
            switch (wordtype) {
                case CS_VAL_POP:
                    return;
                case CS_VAL_CODE:
                case CS_VAL_COND: {
                    auto ret = compileblock(gs, std::string_view{
                        start, std::size_t(gs.send - start)
                    }, curline, CS_RET_NULL, ']');
                    gs.source = ret.first.data();
                    gs.send = ret.first.data() + ret.first.size();
                    gs.current_line = ret.second;
                    return;
                }
                case CS_VAL_IDENT:
                    gs.gen_ident(std::string_view{
                        start, std::size_t((gs.source - 1) - start)
                    });
                    return;
            }
        }
        compileblockstr(gs, start, gs.source - 1);
        if (concs > 1) {
            concs++;
        }
    }
    if (concs) {
        if (prevargs >= MaxResults) {
            gs.code.push_back(CS_CODE_CONC_M | cs_ret_code(wordtype) | (concs << 8));
            gs.code.push_back(CS_CODE_EXIT | cs_ret_code(wordtype));
        } else {
            gs.code.push_back(CS_CODE_CONC_W | cs_ret_code(wordtype) | (concs << 8));
        }
    }
    switch (wordtype) {
        case CS_VAL_POP:
            if (concs || gs.source - 1 > start) {
                gs.code.push_back(CS_CODE_POP);
            }
            break;
        case CS_VAL_COND:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_null();
            } else {
                gs.code.push_back(CS_CODE_COND);
            }
            break;
        case CS_VAL_CODE:
            if (!concs && gs.source - 1 <= start) {
                compileblock(gs);
            } else {
                gs.code.push_back(CS_CODE_COMPILE);
            }
            break;
        case CS_VAL_IDENT:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_ident();
            } else {
                gs.code.push_back(CS_CODE_IDENT_U);
            }
            break;
        case CS_VAL_STRING:
        case CS_VAL_NULL:
        case CS_VAL_ANY:
        case CS_VAL_WORD:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_str();
            }
            break;
        default:
            if (!concs) {
                if (gs.source - 1 <= start) {
                    gs.gen_value(wordtype);
                } else {
                    gs.code.push_back(CS_CODE_FORCE | (wordtype << CS_CODE_RET));
                }
            }
            break;
    }
}

static bool compilearg(
    cs_gen_state &gs, int wordtype, int prevargs, cs_charbuf *word
) {
    gs.skip_comments();
    switch (gs.current()) {
        case '\"':
            switch (wordtype) {
                case CS_VAL_POP:
                    gs.get_str();
                    break;
                case CS_VAL_COND: {
                    size_t line = gs.current_line;
                    auto s = gs.get_str_dup();
                    if (!s.empty()) {
                        s.push_back('\0');
                        compileblock(gs, s.str_term(), line);
                    } else {
                        gs.gen_null();
                    }
                    break;
                }
                case CS_VAL_CODE: {
                    auto s = gs.get_str_dup();
                    s.push_back('\0');
                    compileblock(gs, s.str_term(), gs.current_line);
                    break;
                }
                case CS_VAL_WORD:
                    if (word) {
                        *word = std::move(gs.get_str_dup());
                    }
                    break;
                case CS_VAL_ANY:
                case CS_VAL_STRING:
                    compileunescapestr(gs);
                    break;
                default: {
                    size_t line = gs.current_line;
                    auto s = gs.get_str_dup();
                    s.push_back('\0');
                    gs.gen_value(wordtype, s.str_term(), line);
                    break;
                }
            }
            return true;
        case '$':
            compilelookup(gs, wordtype, prevargs);
            return true;
        case '(':
            gs.next_char();
            if (prevargs >= MaxResults) {
                gs.code.push_back(CS_CODE_ENTER);
                compilestatements(gs, CS_VAL_ANY, ')');
                gs.code.push_back(CS_CODE_EXIT | cs_ret_code(wordtype));
            } else {
                size_t start = gs.code.size();
                compilestatements(gs, CS_VAL_ANY, ')', prevargs);
                if (gs.code.size() > start) {
                    gs.code.push_back(CS_CODE_RESULT_ARG | cs_ret_code(wordtype));
                } else {
                    gs.gen_value(wordtype);
                    return true;
                }
            }
            switch (wordtype) {
                case CS_VAL_POP:
                    gs.code.push_back(CS_CODE_POP);
                    break;
                case CS_VAL_COND:
                    gs.code.push_back(CS_CODE_COND);
                    break;
                case CS_VAL_CODE:
                    gs.code.push_back(CS_CODE_COMPILE);
                    break;
                case CS_VAL_IDENT:
                    gs.code.push_back(CS_CODE_IDENT_U);
                    break;
            }
            return true;
        case '[':
            gs.next_char();
            compileblockmain(gs, wordtype, prevargs);
            return true;
        default:
            switch (wordtype) {
                case CS_VAL_POP: {
                    return !gs.get_word().empty();
                }
                case CS_VAL_COND: {
                    size_t line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, s, line);
                    return true;
                }
                case CS_VAL_CODE: {
                    size_t line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, s, line);
                    return true;
                }
                case CS_VAL_WORD: {
                    auto w = gs.get_word();
                    if (word) {
                        word->clear();
                        word->append(w);
                    }
                    return !w.empty();
                }
                default: {
                    size_t line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    gs.gen_value(wordtype, s, line);
                    return true;
                }
            }
    }
}

static void compile_cmd(
    cs_gen_state &gs, cs_command_impl *id, bool &more, int rettype, int prevargs
) {
    int comtype = CS_CODE_COM, numargs = 0, fakeargs = 0;
    bool rep = false;
    auto fmt = id->get_args();
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case 's': /* string */
                if (more) {
                    more = compilearg(gs, CS_VAL_STRING, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_str(std::string_view{});
                    fakeargs++;
                } else if ((it + 1) == fmt.end()) {
                    int numconc = 1;
                    while ((numargs + numconc) < MaxArguments) {
                        more = compilearg(
                            gs, CS_VAL_STRING, prevargs + numargs + numconc
                        );
                        if (!more) {
                            break;
                        }
                        numconc++;
                    }
                    if (numconc > 1) {
                        gs.code.push_back(CS_CODE_CONC | CS_RET_STRING | (numconc << 8));
                    }
                }
                numargs++;
                break;
            case 'i': /* integer */
                if (more) {
                    more = compilearg(gs, CS_VAL_INT, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_int();
                    fakeargs++;
                }
                numargs++;
                break;
            case 'b': /* integer, INT_MIN default */
                if (more) {
                    more = compilearg(gs, CS_VAL_INT, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_int(std::numeric_limits<cs_int>::min());
                    fakeargs++;
                }
                numargs++;
                break;
            case 'f': /* float */
                if (more) {
                    more = compilearg(gs, CS_VAL_FLOAT, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_float();
                    fakeargs++;
                }
                numargs++;
                break;
            case 'F': /* float, prev-argument default */
                if (more) {
                    more = compilearg(gs, CS_VAL_FLOAT, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.code.push_back(CS_CODE_DUP | CS_RET_FLOAT);
                    fakeargs++;
                }
                numargs++;
                break;
            case 't': /* any arg */
                if (more) {
                    more = compilearg(
                        gs, CS_VAL_ANY,
                        prevargs + numargs
                    );
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_null();
                    fakeargs++;
                }
                numargs++;
                break;
            case 'E': /* condition */
                if (more) {
                    more = compilearg(gs, CS_VAL_COND, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_null();
                    fakeargs++;
                }
                numargs++;
                break;
            case 'e': /* code */
                if (more) {
                    more = compilearg(gs, CS_VAL_CODE, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    compileblock(gs);
                    fakeargs++;
                }
                numargs++;
                break;
            case 'r': /* ident */
                if (more) {
                    more = compilearg(gs, CS_VAL_IDENT, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_ident();
                    fakeargs++;
                }
                numargs++;
                break;
            case '$': /* self */
                gs.gen_ident(id);
                numargs++;
                break;
            case 'N': /* number of arguments */
                gs.gen_int(numargs - fakeargs);
                numargs++;
                break;
            case 'C': /* concatenated string */
                comtype = CS_CODE_COM_C;
                if (more) {
                    while (numargs < MaxArguments) {
                        more = compilearg(gs, CS_VAL_ANY, prevargs + numargs);
                        if (!more) {
                            break;
                        }
                        numargs++;
                    }
                }
                goto compilecomv;
            case 'V': /* varargs */
                comtype = CS_CODE_COM_V;
                if (more) {
                    while (numargs < MaxArguments) {
                        more = compilearg(gs, CS_VAL_ANY, prevargs + numargs);
                        if (!more) {
                            break;
                        }
                        numargs++;
                    }
                }
                goto compilecomv;
            case '1': /* vararg repetition */
            case '2':
            case '3':
            case '4':
                if (more && (numargs < MaxArguments)) {
                    int numrep = *it - '0' + 1;
                    it -= numrep;
                    rep = true;
                } else {
                    while (numargs > MaxArguments) {
                        gs.code.push_back(CS_CODE_POP);
                        --numargs;
                    }
                }
                break;
        }
    }
    gs.code.push_back(comtype | cs_ret_code(rettype) | (id->get_index() << 8));
    return;
compilecomv:
    gs.code.push_back(
        comtype | cs_ret_code(rettype) | (numargs << 8) | (id->get_index() << 13)
    );
}

static void compile_alias(cs_gen_state &gs, cs_alias *id, bool &more, int prevargs) {
    int numargs = 0;
    while (numargs < MaxArguments) {
        more = compilearg(gs, CS_VAL_ANY, prevargs + numargs);
        if (!more) {
            break;
        }
        ++numargs;
    }
    gs.code.push_back(
        (id->get_index() < MaxArguments ? CS_CODE_CALL_ARG : CS_CODE_CALL)
            | (numargs << 8) | (id->get_index() << 13)
    );
}

static void compile_local(cs_gen_state &gs, bool &more, int prevargs) {
    int numargs = 0;
    if (more) {
        while (numargs < MaxArguments) {
            more = compilearg(gs, CS_VAL_IDENT, prevargs + numargs);
            if (!more) {
                break;
            }
            numargs++;
        }
    }
    if (more) {
        while ((more = compilearg(gs, CS_VAL_POP)));
    }
    gs.code.push_back(CS_CODE_LOCAL | (numargs << 8));
}

static void compile_do(
    cs_gen_state &gs, bool &more, int prevargs, int rettype, int opcode
) {
    if (more) {
        more = compilearg(gs, CS_VAL_CODE, prevargs);
    }
    gs.code.push_back((more ? opcode : CS_CODE_NULL) | cs_ret_code(rettype));
}

static void compile_if(
    cs_gen_state &gs, cs_ident *id, bool &more, int prevargs, int rettype
) {
    if (more) {
        more = compilearg(gs, CS_VAL_ANY, prevargs);
    }
    if (!more) {
        gs.code.push_back(CS_CODE_NULL | cs_ret_code(rettype));
    } else {
        int start1 = gs.code.size();
        more = compilearg(gs, CS_VAL_CODE, prevargs + 1);
        if (!more) {
            gs.code.push_back(CS_CODE_POP);
            gs.code.push_back(CS_CODE_NULL | cs_ret_code(rettype));
        } else {
            int start2 = gs.code.size();
            more = compilearg(gs, CS_VAL_CODE, prevargs + 2);
            uint32_t inst1 = gs.code[start1];
            uint32_t op1 = inst1 & ~CS_CODE_RET_MASK;
            uint32_t len1 = start2 - (start1 + 1);
            if (!more) {
                if (op1 == (CS_CODE_BLOCK | (len1 << 8))) {
                    gs.code[start1] = (len1 << 8) | CS_CODE_JUMP_B | CS_CODE_FLAG_FALSE;
                    gs.code[start1 + 1] = CS_CODE_ENTER_RESULT;
                    gs.code[start1 + len1] = (
                        gs.code[start1 + len1] & ~CS_CODE_RET_MASK
                    ) | cs_ret_code(rettype);
                    return;
                }
                compileblock(gs);
            } else {
                uint32_t inst2 = gs.code[start2];
                uint32_t op2 = inst2 & ~CS_CODE_RET_MASK;
                uint32_t len2 = gs.code.size() - (start2 + 1);
                if (op2 == (CS_CODE_BLOCK | (len2 << 8))) {
                    if (op1 == (CS_CODE_BLOCK | (len1 << 8))) {
                        gs.code[start1] = ((start2 - start1) << 8)
                            | CS_CODE_JUMP_B | CS_CODE_FLAG_FALSE;
                        gs.code[start1 + 1] = CS_CODE_ENTER_RESULT;
                        gs.code[start1 + len1] = (
                            gs.code[start1 + len1] & ~CS_CODE_RET_MASK
                        ) | cs_ret_code(rettype);
                        gs.code[start2] = (len2 << 8) | CS_CODE_JUMP;
                        gs.code[start2 + 1] = CS_CODE_ENTER_RESULT;
                        gs.code[start2 + len2] = (
                            gs.code[start2 + len2] & ~CS_CODE_RET_MASK
                        ) | cs_ret_code(rettype);
                        return;
                    } else if (op1 == (CS_CODE_EMPTY | (len1 << 8))) {
                        gs.code[start1] = CS_CODE_NULL | (inst2 & CS_CODE_RET_MASK);
                        gs.code[start2] = (len2 << 8) | CS_CODE_JUMP_B | CS_CODE_FLAG_TRUE;
                        gs.code[start2 + 1] = CS_CODE_ENTER_RESULT;
                        gs.code[start2 + len2] = (
                            gs.code[start2 + len2] & ~CS_CODE_RET_MASK
                        ) | cs_ret_code(rettype);
                        return;
                    }
                }
            }
            gs.code.push_back(CS_CODE_COM | cs_ret_code(rettype) | (id->get_index() << 8));
        }
    }
}

static void compile_and_or(
    cs_gen_state &gs, cs_ident *id, bool &more, int prevargs, int rettype
) {
    int numargs = 0;
    if (more) {
        more = compilearg(gs, CS_VAL_COND, prevargs);
    }
    if (!more) {
        gs.code.push_back(
            ((id->get_raw_type() == CsIdAnd)
                ? CS_CODE_TRUE : CS_CODE_FALSE) | cs_ret_code(rettype)
        );
    } else {
        numargs++;
        int start = gs.code.size(), end = start;
        while (numargs < MaxArguments) {
            more = compilearg(gs, CS_VAL_COND, prevargs + numargs);
            if (!more) {
                break;
            }
            numargs++;
            if ((gs.code[end] & ~CS_CODE_RET_MASK) != (
                CS_CODE_BLOCK | (uint32_t(gs.code.size() - (end + 1)) << 8)
            )) {
                break;
            }
            end = gs.code.size();
        }
        if (more) {
            while (numargs < MaxArguments) {
                more = compilearg(gs, CS_VAL_COND, prevargs + numargs);
                if (!more) {
                    break;
                }
                numargs++;
            }
            gs.code.push_back(
                CS_CODE_COM_V | cs_ret_code(rettype) |
                    (numargs << 8) | (id->get_index() << 13)
            );
        } else {
            uint32_t op = (id->get_raw_type() == CsIdAnd)
                ? (CS_CODE_JUMP_RESULT | CS_CODE_FLAG_FALSE)
                : (CS_CODE_JUMP_RESULT | CS_CODE_FLAG_TRUE);
            gs.code.push_back(op);
            end = gs.code.size();
            while ((start + 1) < end) {
                uint32_t len = gs.code[start] >> 8;
                gs.code[start] = ((end - (start + 1)) << 8) | op;
                gs.code[start + 1] = CS_CODE_ENTER;
                gs.code[start + len] = (
                    gs.code[start + len] & ~CS_CODE_RET_MASK
                ) | cs_ret_code(rettype);
                start += len + 1;
            }
        }
    }
}

static void compilestatements(cs_gen_state &gs, int rettype, int brak, int prevargs) {
    cs_charbuf idname{gs.cs};
    for (;;) {
        gs.skip_comments();
        idname.clear();
        size_t curline = gs.current_line;
        bool more = compilearg(gs, CS_VAL_WORD, prevargs, &idname);
        if (!more) {
            goto endstatement;
        }
        gs.skip_comments();
        if (gs.current() == '=') {
            switch (gs.current(1)) {
                case '/':
                    if (gs.current(2) != '/') {
                        break;
                    }
                    [[fallthrough]];
                case ';':
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                case '\0':
                    gs.next_char();
                    if (!idname.empty()) {
                        idname.push_back('\0');
                        cs_ident *id = gs.cs.new_ident(idname.str_term());
                        if (id) {
                            switch (id->get_type()) {
                                case cs_ident_type::ALIAS:
                                    more = compilearg(gs, CS_VAL_ANY, prevargs);
                                    if (!more) {
                                        gs.gen_str();
                                    }
                                    gs.code.push_back(
                                        (id->get_index() < MaxArguments
                                            ? CS_CODE_ALIAS_ARG
                                            : CS_CODE_ALIAS
                                        ) | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case cs_ident_type::IVAR:
                                    more = compilearg(gs, CS_VAL_INT, prevargs);
                                    if (!more) {
                                        gs.gen_int();
                                    }
                                    gs.code.push_back(
                                        CS_CODE_IVAR1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case cs_ident_type::FVAR:
                                    more = compilearg(gs, CS_VAL_FLOAT, prevargs);
                                    if (!more) {
                                        gs.gen_float();
                                    }
                                    gs.code.push_back(
                                        CS_CODE_FVAR1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case cs_ident_type::SVAR:
                                    more = compilearg(gs, CS_VAL_STRING, prevargs);
                                    if (!more) {
                                        gs.gen_str();
                                    }
                                    gs.code.push_back(
                                        CS_CODE_SVAR1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                default:
                                    break;
                            }
                        }
                        gs.gen_str(idname.str_term());
                    }
                    more = compilearg(gs, CS_VAL_ANY);
                    if (!more) {
                        gs.gen_str();
                    }
                    gs.code.push_back(CS_CODE_ALIAS_U);
                    goto endstatement;
            }
        }
        if (idname.empty()) {
noid:
            int numargs = 0;
            while (numargs < MaxArguments) {
                more = compilearg(gs, CS_VAL_ANY, prevargs + numargs);
                if (!more) {
                    break;
                }
                ++numargs;
            }
            gs.code.push_back(CS_CODE_CALL_U | (numargs << 8));
        } else {
            idname.push_back('\0');
            cs_ident *id = gs.cs.get_ident(idname.str_term());
            if (!id) {
                if (!cs_check_num(idname.str_term())) {
                    gs.gen_str(idname.str_term());
                    goto noid;
                }
                switch (rettype) {
                    case CS_VAL_ANY: {
                        std::string_view end = idname.str_term();
                        cs_int val = cs_parse_int(end, &end);
                        if (!end.empty()) {
                            gs.gen_str(idname.str_term());
                        } else {
                            gs.gen_int(val);
                        }
                        break;
                    }
                    default:
                        gs.gen_value(rettype, idname.str_term(), curline);
                        break;
                }
                gs.code.push_back(CS_CODE_RESULT);
            } else {
                switch (id->get_raw_type()) {
                    case CsIdAlias:
                        compile_alias(
                            gs, static_cast<cs_alias *>(id), more, prevargs
                        );
                        break;
                    case CsIdCommand:
                        compile_cmd(
                            gs, static_cast<cs_command_impl *>(id), more,
                            rettype, prevargs
                        );
                        break;
                    case CsIdLocal:
                        compile_local(gs, more, prevargs);
                        break;
                    case CsIdDo:
                        compile_do(gs, more, prevargs, rettype, CS_CODE_DO);
                        break;
                    case CsIdDoArgs:
                        compile_do(gs, more, prevargs, rettype, CS_CODE_DO_ARGS);
                        break;
                    case CsIdIf:
                        compile_if(gs, id, more, prevargs, rettype);
                        break;
                    case CsIdBreak:
                        gs.code.push_back(CS_CODE_BREAK | CS_CODE_FLAG_FALSE);
                        break;
                    case CsIdContinue:
                        gs.code.push_back(CS_CODE_BREAK | CS_CODE_FLAG_TRUE);
                        break;
                    case CsIdResult:
                        if (more) {
                            more = compilearg(gs, CS_VAL_ANY, prevargs);
                        }
                        gs.code.push_back(
                            (more ? CS_CODE_RESULT : CS_CODE_NULL) |
                                cs_ret_code(rettype)
                        );
                        break;
                    case CsIdNot:
                        if (more) {
                            more = compilearg(gs, CS_VAL_ANY, prevargs);
                        }
                        gs.code.push_back(
                            (more ? CS_CODE_NOT : CS_CODE_TRUE) | cs_ret_code(rettype)
                        );
                        break;
                    case CsIdAnd:
                    case CsIdOr:
                        compile_and_or(gs, id, more, prevargs, rettype);
                        break;
                    case CsIdIvar:
                        if (!(more = compilearg(gs, CS_VAL_INT, prevargs))) {
                            gs.code.push_back(CS_CODE_PRINT | (id->get_index() << 8));
                        } else if (!(id->get_flags() & CS_IDF_HEX) || !(
                            more = compilearg(gs, CS_VAL_INT, prevargs + 1)
                        )) {
                            gs.code.push_back(CS_CODE_IVAR1 | (id->get_index() << 8));
                        } else if (!(
                            more = compilearg(gs, CS_VAL_INT, prevargs + 2)
                        )) {
                            gs.code.push_back(CS_CODE_IVAR2 | (id->get_index() << 8));
                        } else {
                            gs.code.push_back(CS_CODE_IVAR3 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdFvar:
                        if (!(more = compilearg(gs, CS_VAL_FLOAT, prevargs))) {
                            gs.code.push_back(CS_CODE_PRINT | (id->get_index() << 8));
                        } else {
                            gs.code.push_back(CS_CODE_FVAR1 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdSvar:
                        if (!(more = compilearg(gs, CS_VAL_STRING, prevargs))) {
                            gs.code.push_back(CS_CODE_PRINT | (id->get_index() << 8));
                        } else {
                            int numargs = 0;
                            do {
                                ++numargs;
                            } while (numargs < MaxArguments && (
                                more = compilearg(
                                    gs, CS_VAL_ANY, prevargs + numargs
                                )
                            ));
                            if (numargs > 1) {
                                gs.code.push_back(
                                    CS_CODE_CONC | CS_RET_STRING | (numargs << 8)
                                );
                            }
                            gs.code.push_back(CS_CODE_SVAR1 | (id->get_index() << 8));
                        }
                        break;
                }
            }
        }
endstatement:
        if (more) {
            while (compilearg(gs, CS_VAL_POP));
        }
        switch (gs.skip_until(")];/\n")) {
            case '\0':
                if (gs.current() != brak) {
                    throw cs_error(gs.cs, "missing \"%c\"", char(brak));
                    return;
                }
                return;
            case ')':
            case ']':
                if (gs.current() == brak) {
                    gs.next_char();
                    return;
                }
                throw cs_error(gs.cs, "unexpected \"%c\"", gs.current());
                return;
            case '/':
                gs.next_char();
                if (gs.current() == '/') {
                    gs.skip_until('\n');
                }
                goto endstatement;
            default:
                gs.next_char();
                break;
        }
    }
}

void cs_gen_state::gen_main(std::string_view s, int ret_type) {
    source = s.data();
    send = s.data() + s.size();
    code.push_back(CS_CODE_START);
    compilestatements(*this, CS_VAL_ANY);
    code.push_back(CS_CODE_EXIT | ((ret_type < CS_VAL_ANY) ? (ret_type << CS_CODE_RET) : 0));
}

} /* namespace cscript */