#include "cubescript/cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

#include <ctype.h>

#include <ostd/memory.hh>
#include <ostd/limits.hh>

namespace cscript {

ostd::ConstCharRange GenState::get_str() {
    ostd::Size nl;
    ostd::ConstCharRange beg = source;
    source = util::parse_string(cs, source, nl);
    current_line += nl - 1;
    ostd::ConstCharRange ret = ostd::slice_until(beg, source);
    return ret.slice(1, ret.size() - 1);
}

CsString GenState::get_str_dup(bool unescape) {
    auto str = get_str();
    auto app = ostd::appender<CsString>();
    if (unescape) {
        util::unescape_string(app, str);
    } else {
        app.get() = str;
    }
    return std::move(app.get());
}

ostd::ConstCharRange GenState::read_macro_name() {
    auto op = source;
    char c = current();
    if (!isalpha(c) && (c != '_')) {
        return nullptr;
    }
    for (; isalnum(c) || (c == '_'); c = current()) {
        next_char();
    }
    return ostd::slice_until(op, source);
}

char GenState::skip_until(ostd::ConstCharRange chars) {
    char c = current();
    while (c && ostd::find(chars, c).empty()) {
        next_char();
        c = current();
    }
    return c;
}

char GenState::skip_until(char cf) {
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

void GenState::skip_comments() {
    for (;;) {
        for (char c = current(); cs_is_hspace(c); c = current()) {
            next_char();
        }
        if (current() == '\\') {
            char c = current(1);
            if ((c != '\r') && (c != '\n')) {
                throw CsErrorException(cs, "invalid line break");
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

ostd::ConstCharRange GenState::get_word() {
    auto beg = source;
    source = util::parse_word(cs, source);
    return ostd::slice_until(beg, source);
}

static inline int cs_ret_code(int type, int def = 0) {
    if (type >= CsValAny) {
        return (type == CsValCstring) ? CsRetString : def;
    }
    return type << CsCodeRet;
}

static void compilestatements(
    GenState &gs, int rettype, int brak = '\0', int prevargs = 0
);
static inline ostd::Pair<ostd::ConstCharRange, ostd::Size> compileblock(
    GenState &gs, ostd::ConstCharRange p, ostd::Size line,
    int rettype = CsRetNull, int brak = '\0'
);

void GenState::gen_int(ostd::ConstCharRange word) {
    gen_int(cs_parse_int(word));
}

void GenState::gen_float(ostd::ConstCharRange word) {
    gen_float(cs_parse_float(word));
}

void GenState::gen_value(int wordtype, ostd::ConstCharRange word, int line) {
    switch (wordtype) {
        case CsValCany:
            if (!word.empty()) {
                gen_str(word, true);
            } else {
                gen_null();
            }
            break;
        case CsValCstring:
            gen_str(word, true);
            break;
        case CsValAny:
            if (!word.empty()) {
                gen_str(word);
            } else {
                gen_null();
            }
            break;
        case CsValString:
            gen_str(word);
            break;
        case CsValFloat:
            gen_float(word);
            break;
        case CsValInt:
            gen_int(word);
            break;
        case CsValCond:
            if (!word.empty()) {
                compileblock(*this, word, line);
            } else {
                gen_null();
            }
            break;
        case CsValCode:
            compileblock(*this, word, line);
            break;
        case CsValIdent:
            gen_ident(word);
            break;
        default:
            break;
    }
}

static inline void compileblock(GenState &gs) {
    gs.code.push_back(CsCodeEmpty);
}

static inline ostd::Pair<ostd::ConstCharRange, ostd::Size> compileblock(
    GenState &gs, ostd::ConstCharRange p, ostd::Size line, int rettype, int brak
) {
    ostd::Size start = gs.code.size();
    gs.code.push_back(CsCodeBlock);
    gs.code.push_back(CsCodeOffset | ((start + 2) << 8));
    ostd::Size retline = line;
    if (p) {
        ostd::ConstCharRange op = gs.source;
        ostd::Size oldline = gs.current_line;
        gs.source = p;
        gs.current_line = line;
        compilestatements(gs, CsValAny, brak);
        p = gs.source;
        retline = gs.current_line;
        gs.source = op;
        gs.current_line = oldline;
    }
    if (gs.code.size() > start + 2) {
        gs.code.push_back(CsCodeExit | rettype);
        gs.code[start] |= ostd::Uint32(gs.code.size() - (start + 1)) << 8;
    } else {
        gs.code.resize(start);
        gs.code.push_back(CsCodeEmpty | rettype);
    }
    return ostd::make_pair(p, retline);
}

static inline void compileunescapestr(GenState &gs, bool macro = false) {
    auto str = gs.get_str();
    gs.code.push_back(macro ? CsCodeMacro : (CsCodeVal | CsRetString));
    gs.code.reserve(
        gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1
    );
    size_t bufs = (gs.code.capacity() - gs.code.size()) * sizeof(ostd::Uint32);
    char *buf = new char[bufs + 1];
    auto writer = ostd::CharRange(buf, bufs);
    ostd::Size len = util::unescape_string(writer, str);
    memset(&buf[len], 0, sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    gs.code.back() |= len << 8;
    uint32_t *ubuf = reinterpret_cast<uint32_t *>(buf);
    gs.code.insert(gs.code.end(), ubuf, ubuf + (len / sizeof(ostd::Uint32) + 1));
    delete[] buf;
}

static bool compilearg(
    GenState &gs, int wordtype, int prevargs = MaxResults,
    CsString *word = nullptr
);

static void compilelookup(GenState &gs, int ltype, int prevargs = MaxResults) {
    CsString lookup;
    gs.next_char();
    switch (gs.current()) {
        case '(':
        case '[':
            if (!compilearg(gs, CsValCstring, prevargs)) {
                goto invalid;
            }
            break;
        case '$':
            compilelookup(gs, CsValCstring, prevargs);
            break;
        case '\"':
            lookup = gs.get_str_dup();
            goto lookupid;
        default: {
            lookup = gs.get_word();
            if (lookup.empty()) goto invalid;
lookupid:
            CsIdent *id = gs.cs.new_ident(lookup);
            if (id) {
                switch (id->get_type()) {
                    case CsIdentType::Ivar:
                        gs.code.push_back(
                            CsCodeIvar | cs_ret_code(ltype, CsRetInt) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CsValPop:
                                gs.code.pop_back();
                                break;
                            case CsValCode:
                                gs.code.push_back(CsCodeCompile);
                                break;
                            case CsValIdent:
                                gs.code.push_back(CsCodeIdentU);
                                break;
                        }
                        return;
                    case CsIdentType::Fvar:
                        gs.code.push_back(
                            CsCodeFvar | cs_ret_code(ltype, CsRetFloat) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CsValPop:
                                gs.code.pop_back();
                                break;
                            case CsValCode:
                                gs.code.push_back(CsCodeCompile);
                                break;
                            case CsValIdent:
                                gs.code.push_back(CsCodeIdentU);
                                break;
                        }
                        return;
                    case CsIdentType::Svar:
                        switch (ltype) {
                            case CsValPop:
                                return;
                            case CsValCany:
                            case CsValCstring:
                            case CsValCode:
                            case CsValIdent:
                            case CsValCond:
                                gs.code.push_back(
                                    CsCodeSvarM | (id->get_index() << 8)
                                );
                                break;
                            default:
                                gs.code.push_back(
                                    CsCodeSvar | cs_ret_code(ltype, CsRetString) |
                                        (id->get_index() << 8)
                                );
                                break;
                        }
                        goto done;
                    case CsIdentType::Alias:
                        switch (ltype) {
                            case CsValPop:
                                return;
                            case CsValCany:
                            case CsValCond:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CsCodeLookupMarg
                                        : CsCodeLookupM
                                    ) | (id->get_index() << 8)
                                );
                                break;
                            case CsValCstring:
                            case CsValCode:
                            case CsValIdent:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CsCodeLookupMarg
                                        : CsCodeLookupM
                                    ) | CsRetString | (id->get_index() << 8)
                                );
                                break;
                            default:
                                gs.code.push_back(
                                    (id->get_index() < MaxArguments
                                        ? CsCodeLookupArg
                                        : CsCodeLookup
                                    ) | cs_ret_code(ltype, CsRetString) |
                                        (id->get_index() << 8)
                                );
                                break;
                        }
                        goto done;
                    case CsIdentType::Command: {
                        int comtype = CsCodeCom, numargs = 0;
                        if (prevargs >= MaxResults) {
                            gs.code.push_back(CsCodeEnter);
                        }
                        auto fmt = static_cast<CsCommand *>(id)->get_args();
                        for (char c: fmt) {
                            switch (c) {
                                case 'S':
                                    gs.gen_str();
                                    numargs++;
                                    break;
                                case 's':
                                    gs.gen_str(ostd::ConstCharRange(), true);
                                    numargs++;
                                    break;
                                case 'i':
                                    gs.gen_int();
                                    numargs++;
                                    break;
                                case 'b':
                                    gs.gen_int(ostd::NumericLimitMin<CsInt>);
                                    numargs++;
                                    break;
                                case 'f':
                                    gs.gen_float();
                                    numargs++;
                                    break;
                                case 'F':
                                    gs.code.push_back(CsCodeDup | CsRetFloat);
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
                                    comtype = CsCodeComC;
                                    goto compilecomv;
                                case 'V':
                                    comtype = CsCodeComV;
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
                                ? CsCodeExit
                                : CsCodeResultArg
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
                                ? CsCodeExit
                                : CsCodeResultArg
                            ) | cs_ret_code(ltype)
                        );
                        goto done;
                    }
                    default:
                        goto invalid;
                }
            }
            gs.gen_str(lookup, true);
            break;
        }
    }
    switch (ltype) {
        case CsValCany:
        case CsValCond:
            gs.code.push_back(CsCodeLookupMu);
            break;
        case CsValCstring:
        case CsValCode:
        case CsValIdent:
            gs.code.push_back(CsCodeLookupMu | CsRetString);
            break;
        default:
            gs.code.push_back(CsCodeLookupU | cs_ret_code(ltype));
            break;
    }
done:
    switch (ltype) {
        case CsValPop:
            gs.code.push_back(CsCodePop);
            break;
        case CsValCode:
            gs.code.push_back(CsCodeCompile);
            break;
        case CsValCond:
            gs.code.push_back(CsCodeCond);
            break;
        case CsValIdent:
            gs.code.push_back(CsCodeIdentU);
            break;
    }
    return;
invalid:
    switch (ltype) {
        case CsValPop:
            break;
        case CsValNull:
        case CsValAny:
        case CsValCany:
        case CsValWord:
        case CsValCond:
            gs.gen_null();
            break;
        default:
            gs.gen_value(ltype);
            break;
    }
}

static bool compileblockstr(GenState &gs, ostd::ConstCharRange str, bool macro) {
    int startc = gs.code.size();
    gs.code.push_back(macro ? CsCodeMacro : CsCodeVal | CsRetString);
    gs.code.reserve(gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1);
    char *buf = new char[(str.size() / sizeof(uint32_t) + 1) * sizeof(uint32_t)];
    int len = 0;
    while (!str.empty()) {
        char const *p = str.data();
        str = ostd::find_one_of(str, ostd::ConstCharRange("\r/\"@]"));
        memcpy(&buf[len], p, str.data() - p);
        len += str.data() - p;
        if (str.empty()) {
            goto done;
        }
        switch (str.front()) {
            case '\r':
                str.pop_front();
                break;
            case '\"': {
                ostd::ConstCharRange start = str;
                str = util::parse_string(gs.cs, str);
                ostd::ConstCharRange strr = ostd::slice_until(start, str);
                memcpy(&buf[len], strr.data(), strr.size());
                len += strr.size();
                break;
            }
            case '/':
                if (str[1] == '/') {
                    str = ostd::find(str, '\n');
                } else {
                    buf[len++] = str.front();
                    str.pop_front();
                }
                break;
            case '@':
            case ']':
                buf[len++] = str.front();
                str.pop_front();
                break;
        }
    }
done:
    memset(&buf[len], '\0', sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    uint32_t *ubuf = reinterpret_cast<uint32_t *>(buf);
    gs.code.insert(gs.code.end(), ubuf, ubuf + (len / sizeof(ostd::Uint32) + 1));
    gs.code[startc] |= len << 8;
    delete[] buf;
    return true;
}

static bool compileblocksub(GenState &gs, int prevargs) {
    CsString lookup;
    switch (gs.current()) {
        case '(':
            if (!compilearg(gs, CsValCany, prevargs)) {
                return false;
            }
            break;
        case '[':
            if (!compilearg(gs, CsValCstring, prevargs)) {
                return false;
            }
            gs.code.push_back(CsCodeLookupMu);
            break;
        case '\"':
            lookup = gs.get_str_dup();
            goto lookupid;
        default: {
            lookup = gs.read_macro_name();
            if (lookup.empty()) {
                return false;
            }
lookupid:
            CsIdent *id = gs.cs.new_ident(lookup);
            if (id) {
                switch (id->get_type()) {
                    case CsIdentType::Ivar:
                        gs.code.push_back(CsCodeIvar | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Fvar:
                        gs.code.push_back(CsCodeFvar | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Svar:
                        gs.code.push_back(CsCodeSvarM | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Alias:
                        gs.code.push_back(
                            (id->get_index() < MaxArguments
                                ? CsCodeLookupMarg
                                : CsCodeLookupM
                            ) | (id->get_index() << 8)
                        );
                        goto done;
                    default:
                        break;
                }
            }
            gs.gen_str(lookup, true);
            gs.code.push_back(CsCodeLookupMu);
done:
            break;
        }
    }
    return true;
}

static void compileblockmain(GenState &gs, int wordtype, int prevargs) {
    char const *start = gs.source.data();
    ostd::Size curline = gs.current_line;
    int concs = 0;
    for (int brak = 1; brak;) {
        switch (gs.skip_until("@\"/[]")) {
            case '\0':
                throw CsErrorException(gs.cs, "missing \"]\"");
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
                char const *esc = gs.source.data();
                int level = 0;
                while (gs.current() == '@') {
                    ++level;
                    gs.next_char();
                }
                if (brak > level) {
                    continue;
                } else if (brak < level) {
                    throw CsErrorException(gs.cs, "too many @s");
                    return;
                }
                if (!concs && prevargs >= MaxResults) {
                    gs.code.push_back(CsCodeEnter);
                }
                if (concs + 2 > MaxArguments) {
                    gs.code.push_back(CsCodeConcW | CsRetString | (concs << 8));
                    concs = 1;
                }
                if (compileblockstr(
                    gs, ostd::ConstCharRange(start, esc), true
                )) {
                    concs++;
                }
                if (compileblocksub(gs, prevargs + concs)) {
                    concs++;
                }
                if (concs) {
                    start = gs.source.data();
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
    if (gs.source.data() - 1 > start) {
        if (!concs) {
            switch (wordtype) {
                case CsValPop:
                    return;
                case CsValCode:
                case CsValCond: {
                    auto ret = compileblock(gs, ostd::ConstCharRange(
                        start, gs.source.data() + gs.source.size()
                    ), curline, CsRetNull, ']');
                    gs.source = ret.first;
                    gs.current_line = ret.second;
                    return;
                }
                case CsValIdent:
                    gs.gen_ident(ostd::ConstCharRange(start, gs.source.data() - 1));
                    return;
            }
        }
        switch (wordtype) {
            case CsValCstring:
            case CsValCode:
            case CsValIdent:
            case CsValCany:
            case CsValCond:
                compileblockstr(
                    gs, ostd::ConstCharRange(start, gs.source.data() - 1), true
                );
                break;
            default:
                compileblockstr(
                    gs, ostd::ConstCharRange(start, gs.source.data() - 1), concs > 0
                );
                break;
        }
        if (concs > 1) {
            concs++;
        }
    }
    if (concs) {
        if (prevargs >= MaxResults) {
            gs.code.push_back(CsCodeConcM | cs_ret_code(wordtype) | (concs << 8));
            gs.code.push_back(CsCodeExit | cs_ret_code(wordtype));
        } else {
            gs.code.push_back(CsCodeConcW | cs_ret_code(wordtype) | (concs << 8));
        }
    }
    switch (wordtype) {
        case CsValPop:
            if (concs || gs.source.data() - 1 > start) {
                gs.code.push_back(CsCodePop);
            }
            break;
        case CsValCond:
            if (!concs && gs.source.data() - 1 <= start) {
                gs.gen_null();
            } else {
                gs.code.push_back(CsCodeCond);
            }
            break;
        case CsValCode:
            if (!concs && gs.source.data() - 1 <= start) {
                compileblock(gs);
            } else {
                gs.code.push_back(CsCodeCompile);
            }
            break;
        case CsValIdent:
            if (!concs && gs.source.data() - 1 <= start) {
                gs.gen_ident();
            } else {
                gs.code.push_back(CsCodeIdentU);
            }
            break;
        case CsValCstring:
        case CsValCany:
            if (!concs && gs.source.data() - 1 <= start) {
                gs.gen_str(ostd::ConstCharRange(), true);
            }
            break;
        case CsValString:
        case CsValNull:
        case CsValAny:
        case CsValWord:
            if (!concs && gs.source.data() - 1 <= start) {
                gs.gen_str();
            }
            break;
        default:
            if (!concs) {
                if (gs.source.data() - 1 <= start) {
                    gs.gen_value(wordtype);
                } else {
                    gs.code.push_back(CsCodeForce | (wordtype << CsCodeRet));
                }
            }
            break;
    }
}

static bool compilearg(
    GenState &gs, int wordtype, int prevargs, CsString *word
) {
    gs.skip_comments();
    switch (gs.current()) {
        case '\"':
            switch (wordtype) {
                case CsValPop:
                    gs.get_str();
                    break;
                case CsValCond: {
                    ostd::Size line = gs.current_line;
                    auto s = gs.get_str_dup();
                    if (!s.empty()) {
                        compileblock(gs, s, line);
                    } else {
                        gs.gen_null();
                    }
                    break;
                }
                case CsValCode: {
                    auto s = gs.get_str_dup();
                    compileblock(gs);
                    break;
                }
                case CsValWord:
                    if (word) {
                        *word = gs.get_str_dup();
                    }
                    break;
                case CsValAny:
                case CsValString:
                    compileunescapestr(gs);
                    break;
                case CsValCany:
                case CsValCstring:
                    compileunescapestr(gs, true);
                    break;
                default: {
                    ostd::Size line = gs.current_line;
                    auto s = gs.get_str_dup();
                    gs.gen_value(wordtype, s, line);
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
                gs.code.push_back(CsCodeEnter);
                compilestatements(
                    gs, wordtype > CsValAny ? CsValCany : CsValAny, ')'
                );
                gs.code.push_back(CsCodeExit | cs_ret_code(wordtype));
            } else {
                ostd::Size start = gs.code.size();
                compilestatements(
                    gs, wordtype > CsValAny ? CsValCany : CsValAny, ')', prevargs
                );
                if (gs.code.size() > start) {
                    gs.code.push_back(CsCodeResultArg | cs_ret_code(wordtype));
                } else {
                    gs.gen_value(wordtype);
                    return true;
                }
            }
            switch (wordtype) {
                case CsValPop:
                    gs.code.push_back(CsCodePop);
                    break;
                case CsValCond:
                    gs.code.push_back(CsCodeCond);
                    break;
                case CsValCode:
                    gs.code.push_back(CsCodeCompile);
                    break;
                case CsValIdent:
                    gs.code.push_back(CsCodeIdentU);
                    break;
            }
            return true;
        case '[':
            gs.next_char();
            compileblockmain(gs, wordtype, prevargs);
            return true;
        default:
            switch (wordtype) {
                case CsValPop: {
                    return !gs.get_word().empty();
                }
                case CsValCond: {
                    ostd::Size line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, s, line);
                    return true;
                }
                case CsValCode: {
                    ostd::Size line = gs.current_line;
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, s, line);
                    return true;
                }
                case CsValWord: {
                    auto w = gs.get_word();
                    if (word) {
                        *word = w;
                    }
                    return !w.empty();
                }
                default: {
                    ostd::Size line = gs.current_line;
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
    GenState &gs, CsCommand *id, bool &more, int rettype, int prevargs
) {
    int comtype = CsCodeCom, numargs = 0, fakeargs = 0;
    bool rep = false;
    auto fmt = id->get_args();
    for (; !fmt.empty(); ++fmt) {
        switch (*fmt) {
            case 'S':
            case 's':
                if (more) {
                    more = compilearg(
                        gs, *fmt == 's' ? CsValCstring : CsValString,
                        prevargs + numargs
                    );
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_str(ostd::ConstCharRange(), *fmt == 's');
                    fakeargs++;
                } else if (fmt.size() == 1) {
                    int numconc = 1;
                    while ((numargs + numconc) < MaxArguments) {
                        more = compilearg(
                            gs, CsValCstring, prevargs + numargs + numconc
                        );
                        if (!more) {
                            break;
                        }
                        numconc++;
                    }
                    if (numconc > 1) {
                        gs.code.push_back(CsCodeConc | CsRetString | (numconc << 8));
                    }
                }
                numargs++;
                break;
            case 'i':
                if (more) {
                    more = compilearg(gs, CsValInt, prevargs + numargs);
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
            case 'b':
                if (more) {
                    more = compilearg(gs, CsValInt, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.gen_int(ostd::NumericLimitMin<CsInt>);
                    fakeargs++;
                }
                numargs++;
                break;
            case 'f':
                if (more) {
                    more = compilearg(gs, CsValFloat, prevargs + numargs);
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
            case 'F':
                if (more) {
                    more = compilearg(gs, CsValFloat, prevargs + numargs);
                }
                if (!more) {
                    if (rep) {
                        break;
                    }
                    gs.code.push_back(CsCodeDup | CsRetFloat);
                    fakeargs++;
                }
                numargs++;
                break;
            case 'T':
            case 't':
                if (more) {
                    more = compilearg(
                        gs, *fmt == 't' ? CsValCany : CsValAny,
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
            case 'E':
                if (more) {
                    more = compilearg(gs, CsValCond, prevargs + numargs);
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
            case 'e':
                if (more) {
                    more = compilearg(gs, CsValCode, prevargs + numargs);
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
            case 'r':
                if (more) {
                    more = compilearg(gs, CsValIdent, prevargs + numargs);
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
            case '$':
                gs.gen_ident(id);
                numargs++;
                break;
            case 'N':
                gs.gen_int(numargs - fakeargs);
                numargs++;
                break;
            case 'C':
                comtype = CsCodeComC;
                if (more) {
                    while (numargs < MaxArguments) {
                        more = compilearg(gs, CsValCany, prevargs + numargs);
                        if (!more) {
                            break;
                        }
                        numargs++;
                    }
                }
                goto compilecomv;
            case 'V':
                comtype = CsCodeComV;
                if (more) {
                    while (numargs < MaxArguments) {
                        more = compilearg(gs, CsValCany, prevargs + numargs);
                        if (!more) {
                            break;
                        }
                        numargs++;
                    }
                }
                goto compilecomv;
            case '1':
            case '2':
            case '3':
            case '4':
                if (more && (numargs < MaxArguments)) {
                    int numrep = *fmt - '0' + 1;
                    fmt -= numrep;
                    rep = true;
                } else {
                    while (numargs > MaxArguments) {
                        gs.code.push_back(CsCodePop);
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

static void compile_alias(GenState &gs, CsAlias *id, bool &more, int prevargs) {
    int numargs = 0;
    while (numargs < MaxArguments) {
        more = compilearg(gs, CsValAny, prevargs + numargs);
        if (!more) {
            break;
        }
        ++numargs;
    }
    gs.code.push_back(
        (id->get_index() < MaxArguments ? CsCodeCallArg : CsCodeCall)
            | (numargs << 8) | (id->get_index() << 13)
    );
}

static void compile_local(GenState &gs, bool &more, int prevargs) {
    int numargs = 0;
    if (more) {
        while (numargs < MaxArguments) {
            more = compilearg(gs, CsValIdent, prevargs + numargs);
            if (!more) {
                break;
            }
            numargs++;
        }
    }
    if (more) {
        while ((more = compilearg(gs, CsValPop)));
    }
    gs.code.push_back(CsCodeLocal | (numargs << 8));
}

static void compile_do(
    GenState &gs, bool &more, int prevargs, int rettype, int opcode
) {
    if (more) {
        more = compilearg(gs, CsValCode, prevargs);
    }
    gs.code.push_back((more ? opcode : CsCodeNull) | cs_ret_code(rettype));
}

static void compile_if(
    GenState &gs, CsIdent *id, bool &more, int prevargs, int rettype
) {
    if (more) {
        more = compilearg(gs, CsValCany, prevargs);
    }
    if (!more) {
        gs.code.push_back(CsCodeNull | cs_ret_code(rettype));
    } else {
        int start1 = gs.code.size();
        more = compilearg(gs, CsValCode, prevargs + 1);
        if (!more) {
            gs.code.push_back(CsCodePop);
            gs.code.push_back(CsCodeNull | cs_ret_code(rettype));
        } else {
            int start2 = gs.code.size();
            more = compilearg(gs, CsValCode, prevargs + 2);
            ostd::Uint32 inst1 = gs.code[start1];
            ostd::Uint32 op1 = inst1 & ~CsCodeRetMask;
            ostd::Uint32 len1 = start2 - (start1 + 1);
            if (!more) {
                if (op1 == (CsCodeBlock | (len1 << 8))) {
                    gs.code[start1] = (len1 << 8) | CsCodeJumpB | CsCodeFlagFalse;
                    gs.code[start1 + 1] = CsCodeEnterResult;
                    gs.code[start1 + len1] = (
                        gs.code[start1 + len1] & ~CsCodeRetMask
                    ) | cs_ret_code(rettype);
                    return;
                }
                compileblock(gs);
            } else {
                ostd::Uint32 inst2 = gs.code[start2];
                ostd::Uint32 op2 = inst2 & ~CsCodeRetMask;
                ostd::Uint32 len2 = gs.code.size() - (start2 + 1);
                if (op2 == (CsCodeBlock | (len2 << 8))) {
                    if (op1 == (CsCodeBlock | (len1 << 8))) {
                        gs.code[start1] = ((start2 - start1) << 8)
                            | CsCodeJumpB | CsCodeFlagFalse;
                        gs.code[start1 + 1] = CsCodeEnterResult;
                        gs.code[start1 + len1] = (
                            gs.code[start1 + len1] & ~CsCodeRetMask
                        ) | cs_ret_code(rettype);
                        gs.code[start2] = (len2 << 8) | CsCodeJump;
                        gs.code[start2 + 1] = CsCodeEnterResult;
                        gs.code[start2 + len2] = (
                            gs.code[start2 + len2] & ~CsCodeRetMask
                        ) | cs_ret_code(rettype);
                        return;
                    } else if (op1 == (CsCodeEmpty | (len1 << 8))) {
                        gs.code[start1] = CsCodeNull | (inst2 & CsCodeRetMask);
                        gs.code[start2] = (len2 << 8) | CsCodeJumpB | CsCodeFlagTrue;
                        gs.code[start2 + 1] = CsCodeEnterResult;
                        gs.code[start2 + len2] = (
                            gs.code[start2 + len2] & ~CsCodeRetMask
                        ) | cs_ret_code(rettype);
                        return;
                    }
                }
            }
            gs.code.push_back(CsCodeCom | cs_ret_code(rettype) | (id->get_index() << 8));
        }
    }
}

static void compile_and_or(
    GenState &gs, CsIdent *id, bool &more, int prevargs, int rettype
) {
    int numargs = 0;
    if (more) {
        more = compilearg(gs, CsValCond, prevargs);
    }
    if (!more) {
        gs.code.push_back(
            ((id->get_type_raw() == CsIdAnd) ? CsCodeTrue : CsCodeFalse)
                | cs_ret_code(rettype)
        );
    } else {
        numargs++;
        int start = gs.code.size(), end = start;
        while (numargs < MaxArguments) {
            more = compilearg(gs, CsValCond, prevargs + numargs);
            if (!more) {
                break;
            }
            numargs++;
            if ((gs.code[end] & ~CsCodeRetMask) != (
                CsCodeBlock | (ostd::Uint32(gs.code.size() - (end + 1)) << 8)
            )) {
                break;
            }
            end = gs.code.size();
        }
        if (more) {
            while (numargs < MaxArguments) {
                more = compilearg(gs, CsValCond, prevargs + numargs);
                if (!more) {
                    break;
                }
                numargs++;
            }
            gs.code.push_back(
                CsCodeComV | cs_ret_code(rettype) |
                    (numargs << 8) | (id->get_index() << 13)
            );
        } else {
            ostd::Uint32 op = (id->get_type_raw() == CsIdAnd)
                ? (CsCodeJumpResult | CsCodeFlagFalse)
                : (CsCodeJumpResult | CsCodeFlagTrue);
            gs.code.push_back(op);
            end = gs.code.size();
            while ((start + 1) < end) {
                ostd::Uint32 len = gs.code[start] >> 8;
                gs.code[start] = ((end - (start + 1)) << 8) | op;
                gs.code[start + 1] = CsCodeEnter;
                gs.code[start + len] = (
                    gs.code[start + len] & ~CsCodeRetMask
                ) | cs_ret_code(rettype);
                start += len + 1;
            }
        }
    }
}

static void compilestatements(GenState &gs, int rettype, int brak, int prevargs) {
    CsString idname;
    for (;;) {
        gs.skip_comments();
        idname.clear();
        ostd::Size curline = gs.current_line;
        bool more = compilearg(gs, CsValWord, prevargs, &idname);
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
                case ';':
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                case '\0':
                    gs.next_char();
                    if (!idname.empty()) {
                        CsIdent *id = gs.cs.new_ident(idname);
                        if (id) {
                            switch (id->get_type()) {
                                case CsIdentType::Alias:
                                    more = compilearg(gs, CsValAny, prevargs);
                                    if (!more) {
                                        gs.gen_str();
                                    }
                                    gs.code.push_back(
                                        (id->get_index() < MaxArguments
                                            ? CsCodeAliasArg
                                            : CsCodeAlias
                                        ) | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case CsIdentType::Ivar:
                                    more = compilearg(gs, CsValInt, prevargs);
                                    if (!more) {
                                        gs.gen_int();
                                    }
                                    gs.code.push_back(
                                        CsCodeIvar1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case CsIdentType::Fvar:
                                    more = compilearg(gs, CsValFloat, prevargs);
                                    if (!more) {
                                        gs.gen_float();
                                    }
                                    gs.code.push_back(
                                        CsCodeFvar1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case CsIdentType::Svar:
                                    more = compilearg(gs, CsValCstring, prevargs);
                                    if (!more) {
                                        gs.gen_str();
                                    }
                                    gs.code.push_back(
                                        CsCodeSvar1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                default:
                                    break;
                            }
                        }
                        gs.gen_str(idname, true);
                    }
                    more = compilearg(gs, CsValAny);
                    if (!more) {
                        gs.gen_str();
                    }
                    gs.code.push_back(CsCodeAliasU);
                    goto endstatement;
            }
        }
        if (idname.empty()) {
noid:
            int numargs = 0;
            while (numargs < MaxArguments) {
                more = compilearg(gs, CsValCany, prevargs + numargs);
                if (!more) {
                    break;
                }
                ++numargs;
            }
            gs.code.push_back(CsCodeCallU | (numargs << 8));
        } else {
            CsIdent *id = gs.cs.get_ident(idname);
            if (!id) {
                if (!cs_check_num(idname)) {
                    gs.gen_str(idname, true);
                    goto noid;
                }
                switch (rettype) {
                    case CsValAny:
                    case CsValCany: {
                        ostd::ConstCharRange end = idname;
                        CsInt val = cs_parse_int(end, &end);
                        if (!end.empty()) {
                            gs.gen_str(idname, rettype == CsValCany);
                        } else {
                            gs.gen_int(val);
                        }
                        break;
                    }
                    default:
                        gs.gen_value(rettype, idname, curline);
                        break;
                }
                gs.code.push_back(CsCodeResult);
            } else {
                switch (id->get_type_raw()) {
                    case CsIdAlias:
                        compile_alias(
                            gs, static_cast<CsAlias *>(id), more, prevargs
                        );
                        break;
                    case CsIdCommand:
                        compile_cmd(
                            gs, static_cast<CsCommand *>(id), more,
                            rettype, prevargs
                        );
                        break;
                    case CsIdLocal:
                        compile_local(gs, more, prevargs);
                        break;
                    case CsIdDo:
                        compile_do(gs, more, prevargs, rettype, CsCodeDo);
                        break;
                    case CsIdDoArgs:
                        compile_do(gs, more, prevargs, rettype, CsCodeDoArgs);
                        break;
                    case CsIdIf:
                        compile_if(gs, id, more, prevargs, rettype);
                        break;
                    case CsIdBreak:
                        gs.code.push_back(CsCodeBreak | CsCodeFlagFalse);
                        break;
                    case CsIdContinue:
                        gs.code.push_back(CsCodeBreak | CsCodeFlagTrue);
                        break;
                    case CsIdResult:
                        if (more) {
                            more = compilearg(gs, CsValAny, prevargs);
                        }
                        gs.code.push_back(
                            (more ? CsCodeResult : CsCodeNull) |
                                cs_ret_code(rettype)
                        );
                        break;
                    case CsIdNot:
                        if (more) {
                            more = compilearg(gs, CsValCany, prevargs);
                        }
                        gs.code.push_back(
                            (more ? CsCodeNot : CsCodeTrue) | cs_ret_code(rettype)
                        );
                        break;
                    case CsIdAnd:
                    case CsIdOr:
                        compile_and_or(gs, id, more, prevargs, rettype);
                        break;
                    case CsIdIvar:
                        if (!(more = compilearg(gs, CsValInt, prevargs))) {
                            gs.code.push_back(CsCodePrint | (id->get_index() << 8));
                        } else if (!(id->get_flags() & CsIdfHex) || !(
                            more = compilearg(gs, CsValInt, prevargs + 1)
                        )) {
                            gs.code.push_back(CsCodeIvar1 | (id->get_index() << 8));
                        } else if (!(
                            more = compilearg(gs, CsValInt, prevargs + 2)
                        )) {
                            gs.code.push_back(CsCodeIvar2 | (id->get_index() << 8));
                        } else {
                            gs.code.push_back(CsCodeIvar3 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdFvar:
                        if (!(more = compilearg(gs, CsValFloat, prevargs))) {
                            gs.code.push_back(CsCodePrint | (id->get_index() << 8));
                        } else {
                            gs.code.push_back(CsCodeFvar1 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdSvar:
                        if (!(more = compilearg(gs, CsValCstring, prevargs))) {
                            gs.code.push_back(CsCodePrint | (id->get_index() << 8));
                        } else {
                            int numargs = 0;
                            do {
                                ++numargs;
                            } while (numargs < MaxArguments && (
                                more = compilearg(
                                    gs, CsValCany, prevargs + numargs
                                )
                            ));
                            if (numargs > 1) {
                                gs.code.push_back(
                                    CsCodeConc | CsRetString | (numargs << 8)
                                );
                            }
                            gs.code.push_back(CsCodeSvar1 | (id->get_index() << 8));
                        }
                        break;
                }
            }
        }
endstatement:
        if (more) {
            while (compilearg(gs, CsValPop));
        }
        switch (gs.skip_until(")];/\n")) {
            case '\0':
                if (gs.current() != brak) {
                    throw CsErrorException(gs.cs, "missing \"%c\"", char(brak));
                    return;
                }
                return;
            case ')':
            case ']':
                if (gs.current() == brak) {
                    gs.next_char();
                    return;
                }
                throw CsErrorException(gs.cs, "unexpected \"%c\"", gs.current());
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

void GenState::gen_main(ostd::ConstCharRange s, int ret_type) {
    source = s;
    code.push_back(CsCodeStart);
    compilestatements(*this, CsValAny);
    code.push_back(CsCodeExit | ((ret_type < CsValAny) ? (ret_type << CsCodeRet) : 0));
}

} /* namespace cscript */