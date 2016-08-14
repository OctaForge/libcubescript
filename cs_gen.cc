#include "cubescript.hh"
#include "cs_vm.hh"

#include <limits.h>
#include <ctype.h>

#include <ostd/memory.hh>

namespace cscript {

char *cs_dup_ostr(ostd::ConstCharRange s);

int cs_parse_int(ostd::ConstCharRange s) {
    if (s.empty()) return 0;
    return parseint(s.data());
}

float cs_parse_float(ostd::ConstCharRange s) {
    if (s.empty()) return 0.0f;
    return parsefloat(s.data());
}

char const *parsestring(char const *p) {
    for (; *p; p++) switch (*p) {
        case '\r':
        case '\n':
        case '\"':
            return p;
        case '^':
            if (*++p) break;
            return p;
        }
    return p;
}

ostd::ConstCharRange cs_parse_str(ostd::ConstCharRange str) {
    for (; !str.empty(); str.pop_front())
        switch (str.front()) {
        case '\r':
        case '\n':
        case '\"':
            return str;
        case '^':
            str.pop_front();
            if (!str.empty()) break;
            return str;
        }
    return str;
}

static inline void skipcomments(char const *&p) {
    for (;;) {
        p += strspn(p, " \t\r");
        if (p[0] != '/' || p[1] != '/') break;
        p += strcspn(p, "\n\0");
    }
}

static inline char *cutstring(char const *&p) {
    p++;
    char const *end = parsestring(p);
    char *buf = new char[end - p + 1];
    auto writer = ostd::CharRange(buf, end - p + 1);
    util::unescape_string(writer, ostd::ConstCharRange(p, end));
    writer.put('\0');
    p = end;
    if (*p == '\"') p++;
    return buf;
}

char const *parseword(char const *p) {
    constexpr int maxbrak = 100;
    static char brakstack[maxbrak];
    int brakdepth = 0;
    for (;; p++) {
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
            if (p[1] == '/') return p;
            break;
        case '[':
        case '(':
            if (brakdepth >= maxbrak) return p;
            brakstack[brakdepth++] = p[0];
            break;
        case ']':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '[') return p;
            break;
        case ')':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '(') return p;
            break;
        }
    }
    return p;
}

static inline char *cutword(char const *&p) {
    char const *word = p;
    p = parseword(p);
    return p != word ? cs_dup_ostr(ostd::ConstCharRange(word, p - word)) : nullptr;
}

static inline int cs_ret_code(int type, int def = 0) {
    return (type >= VAL_ANY) ? ((type == VAL_CSTR) ? RET_STR : def)
                             : (type << CODE_RET);
}

static void compilestatements(GenState &gs, int rettype, int brak = '\0', int prevargs = 0);
static inline char const *compileblock(GenState &gs, char const *p, int rettype = RET_NULL, int brak = '\0');

void GenState::gen_int(ostd::ConstCharRange word) {
    gen_int(cs_parse_int(word));
}

void GenState::gen_float(ostd::ConstCharRange word) {
    gen_float(cs_parse_float(word));
}

void GenState::gen_value(int wordtype, ostd::ConstCharRange word) {
    switch (wordtype) {
    case VAL_CANY:
        if (!word.empty())
            gen_str(word, true);
        else
            gen_null();
        break;
    case VAL_CSTR:
        gen_str(word, true);
        break;
    case VAL_ANY:
        if (!word.empty())
            gen_str(word);
        else
            gen_null();
        break;
    case VAL_STR:
        gen_str(word);
        break;
    case VAL_FLOAT:
        gen_float(word);
        break;
    case VAL_INT:
        gen_int(word);
        break;
    case VAL_COND:
        if (!word.empty())
            compileblock(*this, word.data());
        else
            gen_null();
        break;
    case VAL_CODE:
        compileblock(*this, word.data());
        break;
    case VAL_IDENT:
        gen_ident(word);
        break;
    default:
        break;
    }
}

static inline void compileblock(GenState &gs) {
    gs.code.push(CODE_EMPTY);
}

static inline char const *compileblock(GenState &gs, char const *p, int rettype, int brak) {
    ostd::Size start = gs.code.size();
    gs.code.push(CODE_BLOCK);
    gs.code.push(CODE_OFFSET | ((start + 2) << 8));
    if (p) {
        char const *op = gs.source;
        gs.source = p;
        compilestatements(gs, VAL_ANY, brak);
        p = gs.source;
        gs.source = op;
    }
    if (gs.code.size() > start + 2) {
        gs.code.push(CODE_EXIT | rettype);
        gs.code[start] |= ostd::Uint32(gs.code.size() - (start + 1)) << 8;
    } else {
        gs.code.resize(start);
        gs.code.push(CODE_EMPTY | rettype);
    }
    return p;
}

static inline void compileunescapestr(GenState &gs, bool macro = false) {
    gs.next_char();
    char const *end = parsestring(gs.source);
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + (end - gs.source) / sizeof(ostd::Uint32) + 1);
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
    auto writer = ostd::CharRange(buf, (gs.code.capacity() - gs.code.size()) * sizeof(ostd::Uint32));
    ostd::Size len = util::unescape_string(writer, ostd::ConstCharRange(gs.source, end));
    writer.put('\0');
    memset(&buf[len], 0, sizeof(ostd::Uint32) - len % sizeof(ostd::Uint32));
    gs.code.back() |= len << 8;
    gs.code.advance(len / sizeof(ostd::Uint32) + 1);
    gs.source = end;
    if (*gs.source == '\"') gs.next_char();
}

static bool compilearg(GenState &gs, int wordtype, int prevargs = MaxResults, ostd::Box<char[]> *word = nullptr);

static void compilelookup(GenState &gs, int ltype, int prevargs = MaxResults) {
    ostd::Box<char[]> lookup;
    gs.next_char();
    switch (gs.current()) {
    case '(':
    case '[':
        if (!compilearg(gs, VAL_CSTR, prevargs)) goto invalid;
        break;
    case '$':
        compilelookup(gs, VAL_CSTR, prevargs);
        break;
    case '\"':
        lookup = ostd::Box<char[]>(cutstring(gs.source));
        goto lookupid;
    default: {
        lookup = ostd::Box<char[]>(cutword(gs.source));
        if (!lookup) goto invalid;
lookupid:
        Ident *id = gs.cs.new_ident(lookup.get());
        if (id) switch (id->type) {
            case ID_IVAR:
                gs.code.push(CODE_IVAR | cs_ret_code(ltype, RET_INT) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    gs.code.pop();
                    break;
                case VAL_CODE:
                    gs.code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    gs.code.push(CODE_IDENTU);
                    break;
                }
                return;
            case ID_FVAR:
                gs.code.push(CODE_FVAR | cs_ret_code(ltype, RET_FLOAT) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    gs.code.pop();
                    break;
                case VAL_CODE:
                    gs.code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    gs.code.push(CODE_IDENTU);
                    break;
                }
                return;
            case ID_SVAR:
                switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_CANY:
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                case VAL_COND:
                    gs.code.push(CODE_SVARM | (id->index << 8));
                    break;
                default:
                    gs.code.push(CODE_SVAR | cs_ret_code(ltype, RET_STR) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_ALIAS:
                switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_CANY:
                case VAL_COND:
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                    break;
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | RET_STR | (id->index << 8));
                    break;
                default:
                    gs.code.push((id->index < MaxArguments ? CODE_LOOKUPARG : CODE_LOOKUP) | cs_ret_code(ltype, RET_STR) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_COMMAND: {
                int comtype = CODE_COM, numargs = 0;
                if (prevargs >= MaxResults) gs.code.push(CODE_ENTER);
                for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
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
                        gs.gen_int(INT_MIN);
                        numargs++;
                        break;
                    case 'f':
                        gs.gen_float();
                        numargs++;
                        break;
                    case 'F':
                        gs.code.push(CODE_DUP | RET_FLOAT);
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
                        comtype = CODE_COMC;
                        goto compilecomv;
                    case 'V':
                        comtype = CODE_COMV;
                        goto compilecomv;
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        break;
                    }
                gs.code.push(comtype | cs_ret_code(ltype) | (id->index << 8));
                gs.code.push((prevargs >= MaxResults ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
compilecomv:
                gs.code.push(comtype | cs_ret_code(ltype) | (numargs << 8) | (id->index << 13));
                gs.code.push((prevargs >= MaxResults ? CODE_EXIT : CODE_RESULT_ARG) | cs_ret_code(ltype));
                goto done;
            }
            default:
                goto invalid;
            }
        gs.gen_str(lookup.get(), true);
        break;
    }
    }
    switch (ltype) {
    case VAL_CANY:
    case VAL_COND:
        gs.code.push(CODE_LOOKUPMU);
        break;
    case VAL_CSTR:
    case VAL_CODE:
    case VAL_IDENT:
        gs.code.push(CODE_LOOKUPMU | RET_STR);
        break;
    default:
        gs.code.push(CODE_LOOKUPU | cs_ret_code(ltype));
        break;
    }
done:
    switch (ltype) {
    case VAL_POP:
        gs.code.push(CODE_POP);
        break;
    case VAL_CODE:
        gs.code.push(CODE_COMPILE);
        break;
    case VAL_COND:
        gs.code.push(CODE_COND);
        break;
    case VAL_IDENT:
        gs.code.push(CODE_IDENTU);
        break;
    }
    return;
invalid:
    switch (ltype) {
    case VAL_POP:
        break;
    case VAL_NULL:
    case VAL_ANY:
    case VAL_CANY:
    case VAL_WORD:
    case VAL_COND:
        gs.gen_null();
        break;
    default:
        gs.gen_value(ltype);
        break;
    }
}

static bool compileblockstr(GenState &gs, ostd::ConstCharRange str, bool macro) {
    int startc = gs.code.size();
    gs.code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    gs.code.reserve(gs.code.size() + str.size() / sizeof(ostd::Uint32) + 1);
    char *buf = reinterpret_cast<char *>(&gs.code[gs.code.size()]);
    int len = 0;
    while (!str.empty()) {
        char const *p = str.data();
        str = ostd::find_one_of(str, ostd::ConstCharRange("\r/\"@]"));
        memcpy(&buf[len], p, str.data() - p);
        len += str.data() - p;
        if (str.empty())
            goto done;
        switch (str.front()) {
        case '\r':
            str.pop_front();
            break;
        case '\"': {
            ostd::ConstCharRange start = str;
            start.pop_front();
            ostd::ConstCharRange end = cs_parse_str(start);
            if (!end.empty() && (end.front() == '\"'))
                end.pop_front();
            ostd::Size slen = str.distance_front(end);
            memcpy(&buf[len], str.data(), slen);
            len += slen;
            str = end;
            break;
        }
        case '/':
            if (str[1] == '/')
                str = ostd::find(str, '\n');
            else {
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
    ostd::Box<char[]> lookup;
    ostd::ConstCharRange lkup;
    char const *op;
    switch (gs.current()) {
    case '(':
        if (!compilearg(gs, VAL_CANY, prevargs)) return false;
        break;
    case '[':
        if (!compilearg(gs, VAL_CSTR, prevargs)) return false;
        gs.code.push(CODE_LOOKUPMU);
        break;
    case '\"':
        lookup = ostd::Box<char[]>(cutstring(gs.source));
        goto lookupid;
    default: {
        op = gs.source;
        while (isalnum(gs.current()) || gs.current() == '_') gs.next_char();
        lkup = ostd::ConstCharRange(op, gs.source - op);
        if (lkup.empty()) return false;
        lookup = ostd::Box<char[]>(cs_dup_ostr(lkup));
lookupid:
        Ident *id = gs.cs.new_ident(lookup.get());
        if (id) switch (id->type) {
            case ID_IVAR:
                gs.code.push(CODE_IVAR | (id->index << 8));
                goto done;
            case ID_FVAR:
                gs.code.push(CODE_FVAR | (id->index << 8));
                goto done;
            case ID_SVAR:
                gs.code.push(CODE_SVARM | (id->index << 8));
                goto done;
            case ID_ALIAS:
                gs.code.push((id->index < MaxArguments ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                goto done;
            }
        gs.gen_str(lookup.get(), true);
        gs.code.push(CODE_LOOKUPMU);
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
        char c = gs.next_char();
        switch (c) {
        case '\0':
            cs_debug_code_line(gs.cs, line, "missing \"]\"");
            gs.source--;
            goto done;
        case '\"':
            gs.source = parsestring(gs.source);
            if (gs.current() == '\"') gs.next_char();
            break;
        case '/':
            if (gs.current() == '/') gs.source += strcspn(gs.source, "\n\0");
            break;
        case '[':
            brak++;
            break;
        case ']':
            brak--;
            break;
        case '@': {
            char const *esc = gs.source;
            while (gs.current() == '@') gs.next_char();
            int level = gs.source - (esc - 1);
            if (brak > level) continue;
            else if (brak < level) cs_debug_code_line(gs.cs, line, "too many @s");
            if (!concs && prevargs >= MaxResults) gs.code.push(CODE_ENTER);
            if (concs + 2 > MaxArguments) {
                gs.code.push(CODE_CONCW | RET_STR | (concs << 8));
                concs = 1;
            }
            if (compileblockstr(gs, ostd::ConstCharRange(start, esc - 1), true)) concs++;
            if (compileblocksub(gs, prevargs + concs)) concs++;
            if (concs) start = gs.source;
            else if (prevargs >= MaxResults) gs.code.pop();
            break;
        }
        }
    }
done:
    if (gs.source - 1 > start) {
        if (!concs) switch (wordtype) {
            case VAL_POP:
                return;
            case VAL_CODE:
            case VAL_COND:
                gs.source = compileblock(gs, start, RET_NULL, ']');
                return;
            case VAL_IDENT:
                gs.gen_ident(ostd::ConstCharRange(start, gs.source - 1));
                return;
            }
        switch (wordtype) {
        case VAL_CSTR:
        case VAL_CODE:
        case VAL_IDENT:
        case VAL_CANY:
        case VAL_COND:
            compileblockstr(gs, ostd::ConstCharRange(start, gs.source - 1), true);
            break;
        default:
            compileblockstr(gs, ostd::ConstCharRange(start, gs.source - 1), concs > 0);
            break;
        }
        if (concs > 1) concs++;
    }
    if (concs) {
        if (prevargs >= MaxResults) {
            gs.code.push(CODE_CONCM | cs_ret_code(wordtype) | (concs << 8));
            gs.code.push(CODE_EXIT | cs_ret_code(wordtype));
        } else gs.code.push(CODE_CONCW | cs_ret_code(wordtype) | (concs << 8));
    }
    switch (wordtype) {
    case VAL_POP:
        if (concs || gs.source - 1 > start) gs.code.push(CODE_POP);
        break;
    case VAL_COND:
        if (!concs && gs.source - 1 <= start) gs.gen_null();
        else gs.code.push(CODE_COND);
        break;
    case VAL_CODE:
        if (!concs && gs.source - 1 <= start) compileblock(gs);
        else gs.code.push(CODE_COMPILE);
        break;
    case VAL_IDENT:
        if (!concs && gs.source - 1 <= start) gs.gen_ident();
        else gs.code.push(CODE_IDENTU);
        break;
    case VAL_CSTR:
    case VAL_CANY:
        if (!concs && gs.source - 1 <= start)
            gs.gen_str(ostd::ConstCharRange(), true);
        break;
    case VAL_STR:
    case VAL_NULL:
    case VAL_ANY:
    case VAL_WORD:
        if (!concs && gs.source - 1 <= start) gs.gen_str();
        break;
    default:
        if (!concs) {
            if (gs.source - 1 <= start) gs.gen_value(wordtype);
            else gs.code.push(CODE_FORCE | (wordtype << CODE_RET));
        }
        break;
    }
}

static bool compilearg(GenState &gs, int wordtype, int prevargs, ostd::Box<char[]> *word) {
    ostd::Box<char[]> unused;
    if (!word) word = &unused;
    skipcomments(gs.source);
    switch (gs.current()) {
    case '\"':
        switch (wordtype) {
        case VAL_POP:
            gs.source = parsestring(gs.source + 1);
            if (gs.current() == '\"') gs.next_char();
            break;
        case VAL_COND: {
            char *s = cutstring(gs.source);
            if (s[0]) compileblock(gs, s);
            else gs.gen_null();
            delete[] s;
            break;
        }
        case VAL_CODE: {
            char *s = cutstring(gs.source);
            compileblock(gs, s);
            delete[] s;
            break;
        }
        case VAL_WORD:
            *word = ostd::Box<char[]>(cutstring(gs.source));
            break;
        case VAL_ANY:
        case VAL_STR:
            compileunescapestr(gs);
            break;
        case VAL_CANY:
        case VAL_CSTR:
            compileunescapestr(gs, true);
            break;
        default: {
            char *s = cutstring(gs.source);
            gs.gen_value(wordtype, s);
            delete[] s;
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
            gs.code.push(CODE_ENTER);
            compilestatements(gs, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')');
            gs.code.push(CODE_EXIT | cs_ret_code(wordtype));
        } else {
            ostd::Size start = gs.code.size();
            compilestatements(gs, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')', prevargs);
            if (gs.code.size() > start) gs.code.push(CODE_RESULT_ARG | cs_ret_code(wordtype));
            else {
                gs.gen_value(wordtype);
                return true;
            }
        }
        switch (wordtype) {
        case VAL_POP:
            gs.code.push(CODE_POP);
            break;
        case VAL_COND:
            gs.code.push(CODE_COND);
            break;
        case VAL_CODE:
            gs.code.push(CODE_COMPILE);
            break;
        case VAL_IDENT:
            gs.code.push(CODE_IDENTU);
            break;
        }
        return true;
    case '[':
        gs.next_char();
        compileblockmain(gs, wordtype, prevargs);
        return true;
    default:
        switch (wordtype) {
        case VAL_POP: {
            char const *s = gs.source;
            gs.source = parseword(gs.source);
            return gs.source != s;
        }
        case VAL_COND: {
            char *s = cutword(gs.source);
            if (!s) return false;
            compileblock(gs, s);
            delete[] s;
            return true;
        }
        case VAL_CODE: {
            char *s = cutword(gs.source);
            if (!s) return false;
            compileblock(gs, s);
            delete[] s;
            return true;
        }
        case VAL_WORD:
            *word = ostd::Box<char[]>(cutword(gs.source));
            return !!*word;
        default: {
            char *s = cutword(gs.source);
            if (!s) return false;
            gs.gen_value(wordtype, s);
            delete[] s;
            return true;
        }
        }
    }
}

static void compilestatements(GenState &gs, int rettype, int brak, int prevargs) {
    char const *line = gs.source;
    ostd::Box<char[]> idname;
    int numargs;
    for (;;) {
        skipcomments(gs.source);
        idname.reset();
        bool more = compilearg(gs, VAL_WORD, prevargs, &idname);
        if (!more) goto endstatement;
        skipcomments(gs.source);
        if (gs.current() == '=') switch (gs.source[1]) {
            case '/':
                if (gs.source[2] != '/') break;
            case ';':
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\0':
                gs.next_char();
                if (idname) {
                    Ident *id = gs.cs.new_ident(idname.get());
                    if (id) switch (id->type) {
                        case ID_ALIAS:
                            if (!(more = compilearg(gs, VAL_ANY, prevargs))) gs.gen_str();
                            gs.code.push((id->index < MaxArguments ? CODE_ALIASARG : CODE_ALIAS) | (id->index << 8));
                            goto endstatement;
                        case ID_IVAR:
                            if (!(more = compilearg(gs, VAL_INT, prevargs))) gs.gen_int();
                            gs.code.push(CODE_IVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_FVAR:
                            if (!(more = compilearg(gs, VAL_FLOAT, prevargs))) gs.gen_float();
                            gs.code.push(CODE_FVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_SVAR:
                            if (!(more = compilearg(gs, VAL_CSTR, prevargs))) gs.gen_str();
                            gs.code.push(CODE_SVAR1 | (id->index << 8));
                            goto endstatement;
                        }
                    gs.gen_str(idname.get(), true);
                }
                if (!(more = compilearg(gs, VAL_ANY))) gs.gen_str();
                gs.code.push(CODE_ALIASU);
                goto endstatement;
            }
        numargs = 0;
        if (!idname) {
noid:
            while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
            gs.code.push(CODE_CALLU | (numargs << 8));
        } else {
            Ident *id = gs.cs.idents.at(idname.get());
            if (!id) {
                if (!cs_check_num(idname.get())) {
                    gs.gen_str(idname.get(), true);
                    goto noid;
                }
                switch (rettype) {
                case VAL_ANY:
                case VAL_CANY: {
                    char *end = idname.get();
                    ostd::Size idlen = strlen(idname.get());
                    int val = int(strtoul(idname.get(), &end, 0));
                    if (end < &idname[idlen]) gs.gen_str(idname.get(), rettype == VAL_CANY);
                    else gs.gen_int(val);
                    break;
                }
                default:
                    gs.gen_value(rettype, idname.get());
                    break;
                }
                gs.code.push(CODE_RESULT);
            } else switch (id->type) {
                case ID_ALIAS:
                    while (numargs < MaxArguments && (more = compilearg(gs, VAL_ANY, prevargs + numargs))) numargs++;
                    gs.code.push((id->index < MaxArguments ? CODE_CALLARG : CODE_CALL) | (numargs << 8) | (id->index << 13));
                    break;
                case ID_COMMAND: {
                    int comtype = CODE_COM, fakeargs = 0;
                    bool rep = false;
                    for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
                        case 'S':
                        case 's':
                            if (more) more = compilearg(gs, *fmt == 's' ? VAL_CSTR : VAL_STR, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_str(ostd::ConstCharRange(), *fmt == 's');
                                fakeargs++;
                            } else if (!fmt[1]) {
                                int numconc = 1;
                                while (numargs + numconc < MaxArguments && (more = compilearg(gs, VAL_CSTR, prevargs + numargs + numconc))) numconc++;
                                if (numconc > 1) gs.code.push(CODE_CONC | RET_STR | (numconc << 8));
                            }
                            numargs++;
                            break;
                        case 'i':
                            if (more) more = compilearg(gs, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_int();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'b':
                            if (more) more = compilearg(gs, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_int(INT_MIN);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'f':
                            if (more) more = compilearg(gs, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_float();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'F':
                            if (more) more = compilearg(gs, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.code.push(CODE_DUP | RET_FLOAT);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'T':
                        case 't':
                            if (more) more = compilearg(gs, *fmt == 't' ? VAL_CANY : VAL_ANY, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_null();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'E':
                            if (more) more = compilearg(gs, VAL_COND, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                gs.gen_null();
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'e':
                            if (more) more = compilearg(gs, VAL_CODE, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileblock(gs);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'r':
                            if (more) more = compilearg(gs, VAL_IDENT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
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
                            comtype = CODE_COMC;
                            if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case 'V':
                            comtype = CODE_COMV;
                            if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                            if (more && numargs < MaxArguments) {
                                int numrep = *fmt - '0' + 1;
                                fmt -= numrep;
                                rep = true;
                            } else for (; numargs > MaxArguments; numargs--) gs.code.push(CODE_POP);
                            break;
                        }
                     gs.code.push(comtype | cs_ret_code(rettype) | (id->index << 8));
                    break;
compilecomv:
                    gs.code.push(comtype | cs_ret_code(rettype) | (numargs << 8) | (id->index << 13));
                    break;
                }
                case ID_LOCAL:
                    if (more) while (numargs < MaxArguments && (more = compilearg(gs, VAL_IDENT, prevargs + numargs))) numargs++;
                    if (more) while ((more = compilearg(gs, VAL_POP)));
                    gs.code.push(CODE_LOCAL | (numargs << 8));
                    break;
                case ID_DO:
                    if (more) more = compilearg(gs, VAL_CODE, prevargs);
                    gs.code.push((more ? CODE_DO : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_DOARGS:
                    if (more) more = compilearg(gs, VAL_CODE, prevargs);
                    gs.code.push((more ? CODE_DOARGS : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_IF:
                    if (more) more = compilearg(gs, VAL_CANY, prevargs);
                    if (!more) gs.code.push(CODE_NULL | cs_ret_code(rettype));
                    else {
                        int start1 = gs.code.size();
                        more = compilearg(gs, VAL_CODE, prevargs + 1);
                        if (!more) {
                            gs.code.push(CODE_POP);
                            gs.code.push(CODE_NULL | cs_ret_code(rettype));
                        } else {
                            int start2 = gs.code.size();
                            more = compilearg(gs, VAL_CODE, prevargs + 2);
                            ostd::Uint32 inst1 = gs.code[start1], op1 = inst1 & ~CODE_RET_MASK, len1 = start2 - (start1 + 1);
                            if (!more) {
                                if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                    gs.code[start1] = (len1 << 8) | CODE_JUMP_FALSE;
                                    gs.code[start1 + 1] = CODE_ENTER_RESULT;
                                    gs.code[start1 + len1] = (gs.code[start1 + len1] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                    break;
                                }
                                compileblock(gs);
                            } else {
                                ostd::Uint32 inst2 = gs.code[start2], op2 = inst2 & ~CODE_RET_MASK, len2 = gs.code.size() - (start2 + 1);
                                if (op2 == (CODE_BLOCK | (len2 << 8))) {
                                    if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                        gs.code[start1] = ((start2 - start1) << 8) | CODE_JUMP_FALSE;
                                        gs.code[start1 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start1 + len1] = (gs.code[start1 + len1] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        gs.code[start2] = (len2 << 8) | CODE_JUMP;
                                        gs.code[start2 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start2 + len2] = (gs.code[start2 + len2] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        break;
                                    } else if (op1 == (CODE_EMPTY | (len1 << 8))) {
                                        gs.code[start1] = CODE_NULL | (inst2 & CODE_RET_MASK);
                                        gs.code[start2] = (len2 << 8) | CODE_JUMP_TRUE;
                                        gs.code[start2 + 1] = CODE_ENTER_RESULT;
                                        gs.code[start2 + len2] = (gs.code[start2 + len2] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                        break;
                                    }
                                }
                            }
                            gs.code.push(CODE_COM | cs_ret_code(rettype) | (id->index << 8));
                        }
                    }
                    break;
                case ID_RESULT:
                    if (more) more = compilearg(gs, VAL_ANY, prevargs);
                    gs.code.push((more ? CODE_RESULT : CODE_NULL) | cs_ret_code(rettype));
                    break;
                case ID_NOT:
                    if (more) more = compilearg(gs, VAL_CANY, prevargs);
                    gs.code.push((more ? CODE_NOT : CODE_TRUE) | cs_ret_code(rettype));
                    break;
                case ID_AND:
                case ID_OR:
                    if (more) more = compilearg(gs, VAL_COND, prevargs);
                    if (!more) {
                        gs.code.push((id->type == ID_AND ? CODE_TRUE : CODE_FALSE) | cs_ret_code(rettype));
                    } else {
                        numargs++;
                        int start = gs.code.size(), end = start;
                        while (numargs < MaxArguments) {
                            more = compilearg(gs, VAL_COND, prevargs + numargs);
                            if (!more) break;
                            numargs++;
                            if ((gs.code[end] & ~CODE_RET_MASK) != (CODE_BLOCK | (ostd::Uint32(gs.code.size() - (end + 1)) << 8))) break;
                            end = gs.code.size();
                        }
                        if (more) {
                            while (numargs < MaxArguments && (more = compilearg(gs, VAL_COND, prevargs + numargs))) numargs++;
                            gs.code.push(CODE_COMV | cs_ret_code(rettype) | (numargs << 8) | (id->index << 13));
                        } else {
                            ostd::Uint32 op = id->type == ID_AND ? CODE_JUMP_RESULT_FALSE : CODE_JUMP_RESULT_TRUE;
                            gs.code.push(op);
                            end = gs.code.size();
                            while (start + 1 < end) {
                                ostd::Uint32 len = gs.code[start] >> 8;
                                gs.code[start] = ((end - (start + 1)) << 8) | op;
                                gs.code[start + 1] = CODE_ENTER;
                                gs.code[start + len] = (gs.code[start + len] & ~CODE_RET_MASK) | cs_ret_code(rettype);
                                start += len + 1;
                            }
                        }
                    }
                    break;
                case ID_IVAR:
                    if (!(more = compilearg(gs, VAL_INT, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else if (!(id->flags & IDF_HEX) || !(more = compilearg(gs, VAL_INT, prevargs + 1))) gs.code.push(CODE_IVAR1 | (id->index << 8));
                    else if (!(more = compilearg(gs, VAL_INT, prevargs + 2))) gs.code.push(CODE_IVAR2 | (id->index << 8));
                    else gs.code.push(CODE_IVAR3 | (id->index << 8));
                    break;
                case ID_FVAR:
                    if (!(more = compilearg(gs, VAL_FLOAT, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else gs.code.push(CODE_FVAR1 | (id->index << 8));
                    break;
                case ID_SVAR:
                    if (!(more = compilearg(gs, VAL_CSTR, prevargs))) gs.code.push(CODE_PRINT | (id->index << 8));
                    else {
                        do ++numargs;
                        while (numargs < MaxArguments && (more = compilearg(gs, VAL_CANY, prevargs + numargs)));
                        if (numargs > 1) gs.code.push(CODE_CONC | RET_STR | (numargs << 8));
                        gs.code.push(CODE_SVAR1 | (id->index << 8));
                    }
                    break;
                }
        }
endstatement:
        if (more) while (compilearg(gs, VAL_POP));
        gs.source += strcspn(gs.source, ")];/\n\0");
        char c = gs.next_char();
        switch (c) {
        case '\0':
            if (c != brak) cs_debug_code_line(gs.cs, line, "missing \"%c\"", brak);
            gs.source--;
            return;

        case ')':
        case ']':
            if (c == brak) return;
            cs_debug_code_line(gs.cs, line, "unexpected \"%c\"", c);
            break;

        case '/':
            if (gs.current() == '/') gs.source += strcspn(gs.source, "\n\0");
            goto endstatement;
        }
    }
}

void GenState::gen_main(ostd::ConstCharRange s, int ret_type) {
    source = s.data();
    code.push(CODE_START);
    compilestatements(*this, VAL_ANY);
    code.push(CODE_EXIT | ((ret_type < VAL_ANY) ? (ret_type << CODE_RET) : 0));
}

ostd::Uint32 *compilecode(CsState &cs, ostd::ConstCharRange str) {
    GenState gs(cs);
    gs.code.reserve(64);
    gs.gen_main(str);
    ostd::Uint32 *code = new ostd::Uint32[gs.code.size()];
    memcpy(code, gs.code.data(), gs.code.size() * sizeof(ostd::Uint32));
    bcode_incr(code);
    return code;
}

} /* namespace cscript */