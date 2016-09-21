#include "cubescript/cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

#include <ctype.h>

#include <ostd/memory.hh>

namespace cscript {

static ostd::ConstCharRange cs_debug_line(
    ostd::ConstCharRange p, ostd::ConstCharRange srcf, ostd::ConstCharRange srcs,
    ostd::ConstCharRange fmt, ostd::CharRange buf
) {
    if (srcs.empty()) {
        return fmt;
    }
    ostd::Size num = 1;
    ostd::ConstCharRange line(srcs);
    for (;;) {
        ostd::ConstCharRange end = ostd::find(line, '\n');
        if (!end.empty()) {
            line = ostd::slice_until(line, end);
        }
        if (&p[0] >= &line[0] && &p[0] <= &line[line.size()]) {
            ostd::CharRange r(buf);
            if (!srcf.empty()) {
                ostd::format(r, "%s:%d: %s", srcf, num, fmt);
            } else {
                ostd::format(r, "%d: %s", num, fmt);
            }
            r.put('\0');
            return buf.data(); /* trigger strlen */
        }
        if (end.empty()) {
            break;
        }
        line = end;
        line.pop_front();
        ++num;
    }
    return fmt;
}

template<typename ...A>
static void cs_error_line(
    GenState &gs, ostd::ConstCharRange p, ostd::ConstCharRange fmt, A &&...args
) {
    ostd::Array<char, 256> buf;
    auto rfmt = cs_debug_line(
        p, gs.src_file, gs.src_str, fmt,
        ostd::CharRange(buf.data(), buf.size())
    );
    throw CsErrorException(gs.cs, rfmt, ostd::forward<A>(args)...);
}

static ostd::ConstCharRange cs_parse_str(ostd::ConstCharRange str) {
    while (!str.empty()) {
        switch (*str) {
            case '\r':
            case '\n':
            case '\"':
                return str;
            case '^':
                ++str;
                if (!str.empty()) {
                    break;
                }
                return str;
        }
        ++str;
    }
    return str;
}

ostd::ConstCharRange GenState::get_str() {
    char const *ln = source;
    char const *beg = source + 1;
    source = beg;
    for (; current(); next_char()) {
        switch (current()) {
            case '\r':
            case '\n':
            case '\"':
                goto done;
            case '^':
                next_char();
                if (current()) {
                    break;
                }
                goto done;
        }
    }
done:
    auto ret = ostd::ConstCharRange(beg, source);
    if (current() != '\"') {
        cs_error_line(*this, ln, "unfinished string '%s'", ln);
    }
    next_char();
    return ret;
}

CsString GenState::get_str_dup(bool unescape) {
    auto str = get_str();
    auto app = ostd::appender<CsString>();
    if (unescape) {
        util::unescape_string(app, str);
    } else {
        app.get() = str;
    }
    return ostd::move(app.get());
}

static inline void skipcomments(char const *&p) {
    for (;;) {
        p += strspn(p, " \t\r");
        if (p[0] != '/' || p[1] != '/') {
            break;
        }
        p += strcspn(p, "\n\0");
    }
}

static char const *parseword(char const *p) {
    for (;;) {
        p += strcspn(p, "\"/;()[] \t\r\n\0");
        switch (p[0]) {
            case '"':
            case ';':
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\0':
                return p;
            case '/':
                if (p[1] == '/') {
                    return p;
                }
                break;
            case '[':
                p = parseword(p + 1);
                if (*p != ']') {
                    return p;
                }
                break;
            case '(':
                p = parseword(p + 1);
                if (*p != ')') {
                    return p;
                }
                break;
            case ']':
            case ')':
                return p;
        }
        ++p;
    }
    return p;
}

ostd::ConstCharRange GenState::get_word() {
    char const *beg = source;
    source = parseword(source);
    if (source != beg) {
        return ostd::ConstCharRange(beg, source);
    }
    return nullptr;
}

namespace util {
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
                ++input;
                item = input;
                input = cs_parse_str(input);
                item = ostd::slice_until(item, input);
                if (!input.empty() && (*input == '"')) {
                    input.pop_front();
                }
                quote = ostd::slice_until(quote, input);
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
                            input = cs_parse_str(input);
                            if (!input.empty() && (*input == '"')) {
                                ++input;
                            }
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
                char const *e = parseword(input.data());
                item = input;
                input += e - input.data();
                item = ostd::slice_until(item, input);
                quote = item;
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

    ostd::Size list_length(ostd::ConstCharRange s) {
        ListParser p(s);
        ostd::Size ret = 0;
        while (p.parse()) {
            ++ret;
        }
        return ret;
    }

    ostd::Maybe<CsString> list_index(
        ostd::ConstCharRange s, ostd::Size idx
    ) {
        ListParser p(s);
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
        ostd::ConstCharRange s, ostd::Size limit
    ) {
        CsVector<CsString> ret;
        ListParser p(s);
        while ((ret.size() < limit) && p.parse()) {
            ret.push(ostd::move(p.element()));
        }
        return ret;
    }
} /* namespace util */

static inline int cs_ret_code(int type, int def = 0) {
    if (type >= CsValAny) {
        return (type == CsValCstring) ? CsRetString : def;
    }
    return type << CsCodeRet;
}

static void compilestatements(
    GenState &gs, int rettype, int brak = '\0', int prevargs = 0
);
static inline char const *compileblock(
    GenState &gs, char const *p, int rettype = CsRetNull, int brak = '\0'
);

void GenState::gen_int(ostd::ConstCharRange word) {
    gen_int(cs_parse_int(word));
}

void GenState::gen_float(ostd::ConstCharRange word) {
    gen_float(cs_parse_float(word));
}

void GenState::gen_value(int wordtype, ostd::ConstCharRange word) {
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
                compileblock(*this, word.data());
            } else {
                gen_null();
            }
            break;
        case CsValCode:
            compileblock(*this, word.data());
            break;
        case CsValIdent:
            gen_ident(word);
            break;
        default:
            break;
    }
}

static inline void compileblock(GenState &gs) {
    gs.code.push(CsCodeEmpty);
}

static inline char const *compileblock(
    GenState &gs, char const *p, int rettype, int brak
) {
    ostd::Size start = gs.code.size();
    gs.code.push(CsCodeBlock);
    gs.code.push(CsCodeOffset | ((start + 2) << 8));
    if (p) {
        char const *op = gs.source;
        gs.source = p;
        compilestatements(gs, CsValAny, brak);
        p = gs.source;
        gs.source = op;
    }
    if (gs.code.size() > start + 2) {
        gs.code.push(CsCodeExit | rettype);
        gs.code[start] |= ostd::Uint32(gs.code.size() - (start + 1)) << 8;
    } else {
        gs.code.resize(start);
        gs.code.push(CsCodeEmpty | rettype);
    }
    return p;
}

static inline void compileunescapestr(GenState &gs, bool macro = false) {
    auto str = gs.get_str();
    gs.code.push(macro ? CsCodeMacro : (CsCodeVal | CsRetString));
    gs.code.reserve(
        gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1
    );
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
    auto writer = ostd::CharRange(
        buf, (gs.code.capacity() - gs.code.size()) * sizeof(ostd::Uint32)
    );
    ostd::Size len = util::unescape_string(writer, str);
    memset(&buf[len], 0, sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    gs.code.back() |= len << 8;
    gs.code.advance(len / sizeof(ostd::Uint32) + 1);
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
                        gs.code.push(
                            CsCodeIvar | cs_ret_code(ltype, CsRetInt) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CsValPop:
                                gs.code.pop();
                                break;
                            case CsValCode:
                                gs.code.push(CsCodeCompile);
                                break;
                            case CsValIdent:
                                gs.code.push(CsCodeIdentU);
                                break;
                        }
                        return;
                    case CsIdentType::Fvar:
                        gs.code.push(
                            CsCodeFvar | cs_ret_code(ltype, CsRetFloat) |
                                (id->get_index() << 8)
                        );
                        switch (ltype) {
                            case CsValPop:
                                gs.code.pop();
                                break;
                            case CsValCode:
                                gs.code.push(CsCodeCompile);
                                break;
                            case CsValIdent:
                                gs.code.push(CsCodeIdentU);
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
                                gs.code.push(
                                    CsCodeSvarM | (id->get_index() << 8)
                                );
                                break;
                            default:
                                gs.code.push(
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
                                gs.code.push(
                                    (id->get_index() < MaxArguments
                                        ? CsCodeLookupMarg
                                        : CsCodeLookupM
                                    ) | (id->get_index() << 8)
                                );
                                break;
                            case CsValCstring:
                            case CsValCode:
                            case CsValIdent:
                                gs.code.push(
                                    (id->get_index() < MaxArguments
                                        ? CsCodeLookupMarg
                                        : CsCodeLookupM
                                    ) | CsRetString | (id->get_index() << 8)
                                );
                                break;
                            default:
                                gs.code.push(
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
                            gs.code.push(CsCodeEnter);
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
                                    gs.gen_int(CsIntMin);
                                    numargs++;
                                    break;
                                case 'f':
                                    gs.gen_float();
                                    numargs++;
                                    break;
                                case 'F':
                                    gs.code.push(CsCodeDup | CsRetFloat);
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
                        gs.code.push(
                            comtype | cs_ret_code(ltype) | (id->get_index() << 8)
                        );
                        gs.code.push(
                            (prevargs >= MaxResults
                                ? CsCodeExit
                                : CsCodeResultArg
                            ) | cs_ret_code(ltype)
                        );
                        goto done;
        compilecomv:
                        gs.code.push(
                            comtype | cs_ret_code(ltype) | (numargs << 8) |
                                (id->get_index() << 13)
                        );
                        gs.code.push(
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
            gs.code.push(CsCodeLookupMu);
            break;
        case CsValCstring:
        case CsValCode:
        case CsValIdent:
            gs.code.push(CsCodeLookupMu | CsRetString);
            break;
        default:
            gs.code.push(CsCodeLookupU | cs_ret_code(ltype));
            break;
    }
done:
    switch (ltype) {
        case CsValPop:
            gs.code.push(CsCodePop);
            break;
        case CsValCode:
            gs.code.push(CsCodeCompile);
            break;
        case CsValCond:
            gs.code.push(CsCodeCond);
            break;
        case CsValIdent:
            gs.code.push(CsCodeIdentU);
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
    gs.code.push(macro ? CsCodeMacro : CsCodeVal | CsRetString);
    gs.code.reserve(gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1);
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
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
                ++start;
                ostd::ConstCharRange end = cs_parse_str(start);
                if (!end.empty() && (*end == '\"')) {
                    ++end;
                }
                ostd::Size slen = str.distance_front(end);
                memcpy(&buf[len], str.data(), slen);
                len += slen;
                str = end;
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
    gs.code.advance(len / sizeof(ostd::Uint32) + 1);
    gs.code[startc] |= len << 8;
    return true;
}

static bool compileblocksub(GenState &gs, int prevargs) {
    CsString lookup;
    char const *op;
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
            gs.code.push(CsCodeLookupMu);
            break;
        case '\"':
            lookup = gs.get_str_dup();
            goto lookupid;
        default: {
            op = gs.source;
            while (isalnum(gs.current()) || gs.current() == '_') {
                gs.next_char();
            }
            lookup = ostd::ConstCharRange(op, gs.source - op);
            if (lookup.empty()) {
                return false;
            }
lookupid:
            CsIdent *id = gs.cs.new_ident(lookup);
            if (id) {
                switch (id->get_type()) {
                    case CsIdentType::Ivar:
                        gs.code.push(CsCodeIvar | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Fvar:
                        gs.code.push(CsCodeFvar | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Svar:
                        gs.code.push(CsCodeSvarM | (id->get_index() << 8));
                        goto done;
                    case CsIdentType::Alias:
                        gs.code.push(
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
            gs.code.push(CsCodeLookupMu);
done:
            break;
        }
    }
    return true;
}

static void compileblockmain(GenState &gs, int wordtype, int prevargs) {
    char const *line = gs.source, *start = gs.source;
    int concs = 0;
    for (int brak = 1; brak;) {
        gs.source += strcspn(gs.source, "@\"/[]\0");
        char c = gs.current();
        switch (c) {
            case '\0':
                cs_error_line(gs, line, "missing \"]\"");
                return;
            case '\"':
                gs.get_str();
                break;
            case '/':
                gs.next_char();
                if (gs.current() == '/') {
                    gs.source += strcspn(gs.source, "\n\0");
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
                gs.next_char();
                char const *esc = gs.source;
                while (gs.current() == '@') {
                    gs.next_char();
                }
                int level = gs.source - (esc - 1);
                if (brak > level) {
                    continue;
                } else if (brak < level) {
                    cs_error_line(gs, line, "too many @s");
                    return;
                }
                if (!concs && prevargs >= MaxResults) {
                    gs.code.push(CsCodeEnter);
                }
                if (concs + 2 > MaxArguments) {
                    gs.code.push(CsCodeConcW | CsRetString | (concs << 8));
                    concs = 1;
                }
                if (compileblockstr(
                    gs, ostd::ConstCharRange(start, esc - 1), true
                )) {
                    concs++;
                }
                if (compileblocksub(gs, prevargs + concs)) {
                    concs++;
                }
                if (concs) {
                    start = gs.source;
                } else if (prevargs >= MaxResults) {
                    gs.code.pop();
                }
                break;
            }
        }
    }
    if (gs.source - 1 > start) {
        if (!concs) {
            switch (wordtype) {
                case CsValPop:
                    return;
                case CsValCode:
                case CsValCond:
                    gs.source = compileblock(gs, start, CsRetNull, ']');
                    return;
                case CsValIdent:
                    gs.gen_ident(ostd::ConstCharRange(start, gs.source - 1));
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
                    gs, ostd::ConstCharRange(start, gs.source - 1), true
                );
                break;
            default:
                compileblockstr(
                    gs, ostd::ConstCharRange(start, gs.source - 1), concs > 0
                );
                break;
        }
        if (concs > 1) {
            concs++;
        }
    }
    if (concs) {
        if (prevargs >= MaxResults) {
            gs.code.push(CsCodeConcM | cs_ret_code(wordtype) | (concs << 8));
            gs.code.push(CsCodeExit | cs_ret_code(wordtype));
        } else {
            gs.code.push(CsCodeConcW | cs_ret_code(wordtype) | (concs << 8));
        }
    }
    switch (wordtype) {
        case CsValPop:
            if (concs || gs.source - 1 > start) {
                gs.code.push(CsCodePop);
            }
            break;
        case CsValCond:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_null();
            } else {
                gs.code.push(CsCodeCond);
            }
            break;
        case CsValCode:
            if (!concs && gs.source - 1 <= start) {
                compileblock(gs);
            } else {
                gs.code.push(CsCodeCompile);
            }
            break;
        case CsValIdent:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_ident();
            } else {
                gs.code.push(CsCodeIdentU);
            }
            break;
        case CsValCstring:
        case CsValCany:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_str(ostd::ConstCharRange(), true);
            }
            break;
        case CsValString:
        case CsValNull:
        case CsValAny:
        case CsValWord:
            if (!concs && gs.source - 1 <= start) {
                gs.gen_str();
            }
            break;
        default:
            if (!concs) {
                if (gs.source - 1 <= start) {
                    gs.gen_value(wordtype);
                } else {
                    gs.code.push(CsCodeForce | (wordtype << CsCodeRet));
                }
            }
            break;
    }
}

static bool compilearg(
    GenState &gs, int wordtype, int prevargs, CsString *word
) {
    skipcomments(gs.source);
    switch (gs.current()) {
        case '\"':
            switch (wordtype) {
                case CsValPop:
                    gs.get_str();
                    break;
                case CsValCond: {
                    auto s = gs.get_str_dup();
                    if (!s.empty()) {
                        compileblock(gs, s.data());
                    } else {
                        gs.gen_null();
                    }
                    break;
                }
                case CsValCode: {
                    auto s = gs.get_str_dup();
                    compileblock(gs, s.data());
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
                    auto s = gs.get_str_dup();
                    gs.gen_value(wordtype, s);
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
                gs.code.push(CsCodeEnter);
                compilestatements(
                    gs, wordtype > CsValAny ? CsValCany : CsValAny, ')'
                );
                gs.code.push(CsCodeExit | cs_ret_code(wordtype));
            } else {
                ostd::Size start = gs.code.size();
                compilestatements(
                    gs, wordtype > CsValAny ? CsValCany : CsValAny, ')', prevargs
                );
                if (gs.code.size() > start) {
                    gs.code.push(CsCodeResultArg | cs_ret_code(wordtype));
                } else {
                    gs.gen_value(wordtype);
                    return true;
                }
            }
            switch (wordtype) {
                case CsValPop:
                    gs.code.push(CsCodePop);
                    break;
                case CsValCond:
                    gs.code.push(CsCodeCond);
                    break;
                case CsValCode:
                    gs.code.push(CsCodeCompile);
                    break;
                case CsValIdent:
                    gs.code.push(CsCodeIdentU);
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
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, CsString(s).data());
                    return true;
                }
                case CsValCode: {
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    compileblock(gs, CsString(s).data());
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
                    auto s = gs.get_word();
                    if (s.empty()) {
                        return false;
                    }
                    gs.gen_value(wordtype, CsString(s));
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
                        gs.code.push(CsCodeConc | CsRetString | (numconc << 8));
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
                    gs.gen_int(CsIntMin);
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
                    gs.code.push(CsCodeDup | CsRetFloat);
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
                        gs.code.push(CsCodePop);
                        --numargs;
                    }
                }
                break;
        }
    }
    gs.code.push(comtype | cs_ret_code(rettype) | (id->get_index() << 8));
    return;
compilecomv:
    gs.code.push(
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
    gs.code.push(
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
    gs.code.push(CsCodeLocal | (numargs << 8));
}

static void compile_do(
    GenState &gs, bool &more, int prevargs, int rettype, int opcode
) {
    if (more) {
        more = compilearg(gs, CsValCode, prevargs);
    }
    gs.code.push((more ? opcode : CsCodeNull) | cs_ret_code(rettype));
}

static void compile_if(
    GenState &gs, CsIdent *id, bool &more, int prevargs, int rettype
) {
    if (more) {
        more = compilearg(gs, CsValCany, prevargs);
    }
    if (!more) {
        gs.code.push(CsCodeNull | cs_ret_code(rettype));
    } else {
        int start1 = gs.code.size();
        more = compilearg(gs, CsValCode, prevargs + 1);
        if (!more) {
            gs.code.push(CsCodePop);
            gs.code.push(CsCodeNull | cs_ret_code(rettype));
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
            gs.code.push(CsCodeCom | cs_ret_code(rettype) | (id->get_index() << 8));
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
        gs.code.push(
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
            gs.code.push(
                CsCodeComV | cs_ret_code(rettype) |
                    (numargs << 8) | (id->get_index() << 13)
            );
        } else {
            ostd::Uint32 op = (id->get_type_raw() == CsIdAnd)
                ? (CsCodeJumpResult | CsCodeFlagFalse)
                : (CsCodeJumpResult | CsCodeFlagTrue);
            gs.code.push(op);
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
    char const *line = gs.source;
    CsString idname;
    for (;;) {
        skipcomments(gs.source);
        idname.clear();
        bool more = compilearg(gs, CsValWord, prevargs, &idname);
        if (!more) {
            goto endstatement;
        }
        skipcomments(gs.source);
        if (gs.current() == '=') {
            switch (gs.source[1]) {
                case '/':
                    if (gs.source[2] != '/') {
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
                                    gs.code.push(
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
                                    gs.code.push(
                                        CsCodeIvar1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case CsIdentType::Fvar:
                                    more = compilearg(gs, CsValFloat, prevargs);
                                    if (!more) {
                                        gs.gen_float();
                                    }
                                    gs.code.push(
                                        CsCodeFvar1 | (id->get_index() << 8)
                                    );
                                    goto endstatement;
                                case CsIdentType::Svar:
                                    more = compilearg(gs, CsValCstring, prevargs);
                                    if (!more) {
                                        gs.gen_str();
                                    }
                                    gs.code.push(
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
                    gs.code.push(CsCodeAliasU);
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
            gs.code.push(CsCodeCallU | (numargs << 8));
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
                        gs.gen_value(rettype, idname);
                        break;
                }
                gs.code.push(CsCodeResult);
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
                        gs.code.push(CsCodeBreak | CsCodeFlagFalse);
                        break;
                    case CsIdContinue:
                        gs.code.push(CsCodeBreak | CsCodeFlagTrue);
                        break;
                    case CsIdResult:
                        if (more) {
                            more = compilearg(gs, CsValAny, prevargs);
                        }
                        gs.code.push(
                            (more ? CsCodeResult : CsCodeNull) |
                                cs_ret_code(rettype)
                        );
                        break;
                    case CsIdNot:
                        if (more) {
                            more = compilearg(gs, CsValCany, prevargs);
                        }
                        gs.code.push(
                            (more ? CsCodeNot : CsCodeTrue) | cs_ret_code(rettype)
                        );
                        break;
                    case CsIdAnd:
                    case CsIdOr:
                        compile_and_or(gs, id, more, prevargs, rettype);
                        break;
                    case CsIdIvar:
                        if (!(more = compilearg(gs, CsValInt, prevargs))) {
                            gs.code.push(CsCodePrint | (id->get_index() << 8));
                        } else if (!(id->get_flags() & CsIdfHex) || !(
                            more = compilearg(gs, CsValInt, prevargs + 1)
                        )) {
                            gs.code.push(CsCodeIvar1 | (id->get_index() << 8));
                        } else if (!(
                            more = compilearg(gs, CsValInt, prevargs + 2)
                        )) {
                            gs.code.push(CsCodeIvar2 | (id->get_index() << 8));
                        } else {
                            gs.code.push(CsCodeIvar3 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdFvar:
                        if (!(more = compilearg(gs, CsValFloat, prevargs))) {
                            gs.code.push(CsCodePrint | (id->get_index() << 8));
                        } else {
                            gs.code.push(CsCodeFvar1 | (id->get_index() << 8));
                        }
                        break;
                    case CsIdSvar:
                        if (!(more = compilearg(gs, CsValCstring, prevargs))) {
                            gs.code.push(CsCodePrint | (id->get_index() << 8));
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
                                gs.code.push(
                                    CsCodeConc | CsRetString | (numargs << 8)
                                );
                            }
                            gs.code.push(CsCodeSvar1 | (id->get_index() << 8));
                        }
                        break;
                }
            }
        }
endstatement:
        if (more) {
            while (compilearg(gs, CsValPop));
        }
        gs.source += strcspn(gs.source, ")];/\n\0");
        char c = gs.next_char();
        switch (c) {
            case '\0':
                if (c != brak) {
                    cs_error_line(
                        gs, line, "missing \"%c\"", char(brak)
                    );
                    return;
                }
                gs.source--;
                return;
            case ')':
            case ']':
                if (c == brak) {
                    return;
                }
                cs_error_line(gs, line, "unexpected \"%c\"", c);
                return;
            case '/':
                if (gs.current() == '/') {
                    gs.source += strcspn(gs.source, "\n\0");
                }
                goto endstatement;
        }
    }
}

void GenState::gen_main(ostd::ConstCharRange s, int ret_type) {
    source = s.data();
    code.push(CsCodeStart);
    compilestatements(*this, CsValAny);
    code.push(CsCodeExit | ((ret_type < CsValAny) ? (ret_type << CsCodeRet) : 0));
}

} /* namespace cscript */