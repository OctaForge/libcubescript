#include "command.hh"

#define fatal printf

CsState cstate;

static constexpr int MAX_ARGUMENTS = 25;
static constexpr int MAX_RESULTS = 7;
static constexpr int MAX_COMARGS = 12;

const struct NullValue: TaggedValue {
    NullValue() { set_null(); }
} null_value;

static TaggedValue no_ret = null_value;

CsState::CsState(): result(&no_ret) {
    for (int i = 0; i < MAX_ARGUMENTS; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident((const char *)buf, IDF_ARG);
    }
    dummy = new_ident("//dummy", IDF_UNKNOWN);
}

CsState::~CsState() {
    for (Ident &i: idents.iter()) {
        if (i.type == ID_ALIAS) {
            i.forcenull();
            delete[] i.code;
            i.code = nullptr;
        } else if (i.type == ID_COMMAND || i.type >= ID_LOCAL) {
            delete[] i.args;
        }
    }
}

void CsState::clear_override(Ident &id) {
    if (!(id.flags & IDF_OVERRIDDEN)) return;
    switch (id.type) {
    case ID_ALIAS:
        if (id.valtype == VAL_STR) {
            if (!id.val.s[0]) break;
            delete[] id.val.s;
        }
        id.clean_code();
        id.valtype = VAL_STR;
        id.val.s = dup_ostr("");
        break;
    case ID_VAR:
        *id.storage.i = id.overrideval.i;
        id.changed(*this);
        break;
    case ID_FVAR:
        *id.storage.f = id.overrideval.f;
        id.changed(*this);
        break;
    case ID_SVAR:
        delete[] *id.storage.s;
        *id.storage.s = id.overrideval.s;
        id.changed(*this);
        break;
    }
    id.flags &= ~IDF_OVERRIDDEN;
}

void CsState::clear_overrides() {
    for (Ident &id: idents.iter())
        clear_override(id);
}

int _numargs = variable("numargs", MAX_ARGUMENTS, 0, 0, &_numargs, nullptr, 0);

void TaggedValue::cleanup() {
    switch (type) {
    case VAL_STR:
        delete[] s;
        break;
    case VAL_CODE:
        if (code[-1] == CODE_START) delete[] (ostd::byte *)&code[-1];
        break;
    }
}

void TaggedValue::force_null() {
    if (type == VAL_NULL) return;
    cleanup();
    set_null();
}

float TaggedValue::force_float() {
    float rf = 0.0f;
    switch (type) {
    case VAL_INT:
        rf = i;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        rf = parsefloat(s);
        break;
    case VAL_FLOAT:
        return f;
    }
    cleanup();
    set_float(rf);
    return rf;
}

int TaggedValue::force_int() {
    int ri = 0;
    switch (type) {
    case VAL_FLOAT:
        ri = f;
        break;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        ri = parseint(s);
        break;
    case VAL_INT:
        return i;
    }
    cleanup();
    set_int(ri);
    return ri;
}

const char *TaggedValue::force_str() {
    const char *rs = "";
    switch (type) {
    case VAL_FLOAT:
        rs = floatstr(f);
        break;
    case VAL_INT:
        rs = intstr(i);
        break;
    case VAL_MACRO:
    case VAL_CSTR:
        rs = s;
        break;
    case VAL_STR:
        return s;
    }
    cleanup();
    set_str(dup_ostr(rs));
    return rs;
}

void TaggedValue::force(int type) {
    switch (type) {
    case RET_STR:
        if (type != VAL_STR) force_str();
        break;
    case RET_INT:
        if (type != VAL_INT) force_int();
        break;
    case RET_FLOAT:
        if (type != VAL_FLOAT) force_float();
        break;
    }
}

static inline void free_args(TaggedValue *args, int &oldnum, int newnum) {
    for (int i = newnum; i < oldnum; i++) args[i].cleanup();
    oldnum = newnum;
}

void Ident::clean_code() {
    if (code) {
        code[0] -= 0x100;
        if (int(code[0]) < 0x100) delete[] code;
        code = nullptr;
    }
}

static const char *sourcefile = nullptr, *sourcestr = nullptr;

static const char *debugline(const char *p, const char *fmt) {
    if (!sourcestr) return fmt;
    int num = 1;
    const char *line = sourcestr;
    for (;;) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        if (p >= line && p <= end) {
            static char buf[256];
            auto r = ostd::iter(buf);
            if (sourcefile) ostd::format(r, "%s:%d: %s", sourcefile, num, fmt);
            else ostd::format(r, "%d: %s", num, fmt);
            r.put('\0');
            return buf;
        }
        if (!*end) break;
        line = end + 1;
        num++;
    }
    return fmt;
}

static struct IdentLink {
    Ident *id;
    IdentLink *next;
    int usedargs;
    IdentStack *argstack;
} noalias = { nullptr, nullptr, (1 << MAX_ARGUMENTS) - 1, nullptr }, *aliasstack = &noalias;

int dbgalias = variable("dbgalias", 0, 4, 1000, &dbgalias, nullptr, 0);

static void debugalias() {
    if (!dbgalias) return;
    int total = 0, depth = 0;
    for (IdentLink *l = aliasstack; l != &noalias; l = l->next) total++;
    for (IdentLink *l = aliasstack; l != &noalias; l = l->next) {
        Ident *id = l->id;
        ++depth;
        if (depth < dbgalias) fprintf(stderr, "  %d) %s\n", total - depth + 1, id->name.data());
        else if (l->next == &noalias) fprintf(stderr, depth == dbgalias ? "  %d) %s\n" : "  ..%d) %s\n", total - depth + 1, id->name.data());
    }
}

static int nodebug = 0;

static void debugcode(const char *fmt, ...) {
    if (nodebug) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);

    debugalias();
}

static void debugcodeline(const char *p, const char *fmt, ...) {
    if (nodebug) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, debugline(p, fmt), args);
    fputc('\n', stderr);
    va_end(args);

    debugalias();
}

ICOMMAND(nodebug, "e", (CsState &cs, ostd::uint *body), { nodebug++; executeret(body, *cs.result); nodebug--; });

void pusharg(Ident &id, const TaggedValue &v, IdentStack &stack) {
    stack.val = id.val;
    stack.valtype = id.valtype;
    stack.next = id.stack;
    id.stack = &stack;
    id.setval(v);
    id.clean_code();
}

void poparg(Ident &id) {
    if (!id.stack) return;
    IdentStack *stack = id.stack;
    if (id.valtype == VAL_STR) delete[] id.val.s;
    id.setval(*stack);
    id.clean_code();
    id.stack = stack->next;
}

static inline void undoarg(Ident &id, IdentStack &stack) {
    IdentStack *prev = id.stack;
    stack.val = id.val;
    stack.valtype = id.valtype;
    stack.next = prev;
    id.stack = prev->next;
    id.setval(*prev);
    id.clean_code();
}

#define UNDOARGS \
    IdentStack argstack[MAX_ARGUMENTS]; \
    for(int argmask = aliasstack->usedargs, i = 0; argmask; argmask >>= 1, i++) if(argmask&1) \
        undoarg(*cstate.identmap[i], argstack[i]); \
    IdentLink *prevstack = aliasstack->next; \
    IdentLink aliaslink = { aliasstack->id, aliasstack, prevstack->usedargs, prevstack->argstack }; \
    aliasstack = &aliaslink;

static inline void redoarg(Ident &id, const IdentStack &stack) {
    IdentStack *prev = stack.next;
    prev->val = id.val;
    prev->valtype = id.valtype;
    id.stack = prev;
    id.setval(stack);
    id.clean_code();
}

#define REDOARGS \
    prevstack->usedargs = aliaslink.usedargs; \
    aliasstack = aliaslink.next; \
    for(int argmask = aliasstack->usedargs, i = 0; argmask; argmask >>= 1, i++) if(argmask&1) \
        redoarg(*cstate.identmap[i], argstack[i]);

ICOMMAND(push, "rTe", (CsState &cs, Ident *id, TaggedValue *v, ostd::uint *code), {
    if (id->type != ID_ALIAS || id->index < MAX_ARGUMENTS) return;
    IdentStack stack;
    pusharg(*id, *v, stack);
    v->type = VAL_NULL;
    id->flags &= ~IDF_UNKNOWN;
    executeret(code, *cs.result);
    poparg(*id);
});

static inline void pushalias(Ident &id, IdentStack &stack) {
    if (id.type == ID_ALIAS && id.index >= MAX_ARGUMENTS) {
        pusharg(id, null_value, stack);
        id.flags &= ~IDF_UNKNOWN;
    }
}

static inline void popalias(Ident &id) {
    if (id.type == ID_ALIAS && id.index >= MAX_ARGUMENTS) poparg(id);
}

KEYWORD(local, ID_LOCAL);

static inline bool checknumber(const char *s) {
    if (isdigit(s[0])) return true;
    else switch (s[0]) {
        case '+':
        case '-':
            return isdigit(s[1]) || (s[1] == '.' && isdigit(s[2]));
        case '.':
            return isdigit(s[1]) != 0;
        default:
            return false;
        }
}

Ident *CsState::new_ident(ostd::ConstCharRange name, int flags) {
    Ident *id = idents.at(name);
    if (!id) {
        if (checknumber(name.data())) {
            debugcode("number %.*s is not a valid identifier name", int(name.size()), name.data());
            return dummy;
        }
        id = add_ident(ID_ALIAS, name, flags);
    }
    return id;
}

Ident *CsState::force_ident(TaggedValue &v) {
    switch (v.type) {
    case VAL_IDENT:
        return v.id;
    case VAL_MACRO:
    case VAL_CSTR: {
        Ident *id = new_ident(v.s, IDF_UNKNOWN);
        v.set_ident(id);
        return id;
    }
    case VAL_STR: {
        Ident *id = new_ident(v.s, IDF_UNKNOWN);
        delete[] v.s;
        v.set_ident(id);
        return id;
    }
    }
    v.cleanup();
    v.set_ident(dummy);
    return dummy;
}

bool CsState::reset_var(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (!id) return false;
    if (id->flags & IDF_READONLY) {
        debugcode("variable %s is read only", id->name.data());
        return false;
    }
    clear_override(*id);
    return true;
}

ICOMMAND(resetvar, "s", (CsState &cs, char *name), cs.result->set_int(cs.reset_var(name)););

void CsState::touch_var(ostd::ConstCharRange name) {
    Ident *id = idents.at(name);
    if (id) switch (id->type) {
    case ID_VAR:
    case ID_FVAR:
    case ID_SVAR:
        id->changed(*this);
        break;
    }
}

static inline void setarg(Ident &id, TaggedValue &v) {
    if (aliasstack->usedargs & (1 << id.index)) {
        if (id.valtype == VAL_STR) delete[] id.val.s;
        id.setval(v);
        id.clean_code();
    } else {
        pusharg(id, v, aliasstack->argstack[id.index]);
        aliasstack->usedargs |= 1 << id.index;
    }
}

static inline void setalias(Ident &id, TaggedValue &v) {
    if (id.valtype == VAL_STR) delete[] id.val.s;
    id.setval(v);
    id.clean_code();
    id.flags = (id.flags & cstate.identflags) | cstate.identflags;
}

static void setalias(const char *name, TaggedValue &v) {
    Ident *id = cstate.idents.at(name);
    if (id) {
        switch (id->type) {
        case ID_ALIAS:
            if (id->index < MAX_ARGUMENTS) setarg(*id, v);
            else setalias(*id, v);
            return;
        case ID_VAR:
            setvarchecked(id, v.get_int());
            break;
        case ID_FVAR:
            setfvarchecked(id, v.get_float());
            break;
        case ID_SVAR:
            setsvarchecked(id, v.get_str());
            break;
        default:
            debugcode("cannot redefine builtin %s with an alias", id->name.data());
            break;
        }
        v.cleanup();
    } else if (checknumber(name)) {
        debugcode("cannot alias number %s", name);
        v.cleanup();
    } else {
        cstate.add_ident(ID_ALIAS, name, v, cstate.identflags);
    }
}

void alias(const char *name, const char *str) {
    TaggedValue v;
    v.set_str(dup_ostr(str));
    setalias(name, v);
}

void alias(const char *name, TaggedValue &v) {
    setalias(name, v);
}

ICOMMAND(alias, "sT", (CsState &, const char *name, TaggedValue *v), {
    setalias(name, *v);
    v->type = VAL_NULL;
});

int variable(const char *name, int min, int cur, int max, int *storage, IdentFunc fun, int flags) {
    cstate.add_ident(ID_VAR, name, min, max, storage, fun, flags);
    return cur;
}

float fvariable(const char *name, float min, float cur, float max, float *storage, IdentFunc fun, int flags) {
    cstate.add_ident(ID_FVAR, name, min, max, storage, fun, flags);
    return cur;
}

char *svariable(const char *name, const char *cur, char **storage, IdentFunc fun, int flags) {
    cstate.add_ident(ID_SVAR, name, storage, fun, flags);
    return dup_ostr(cur);
}

#define _GETVAR(id, vartype, name, retval) \
    Ident *id = cstate.idents.at(name); \
    if(!id || id->type!=vartype) return retval;
#define GETVAR(id, name, retval) _GETVAR(id, ID_VAR, name, retval)
#define OVERRIDEVAR(errorval, saveval, resetval, clearval) \
    if(cstate.identflags&IDF_OVERRIDDEN || id->flags&IDF_OVERRIDE) \
    { \
        if(id->flags&IDF_PERSIST) \
        { \
            debugcode("cannot override persistent variable %s", id->name.data()); \
            errorval; \
        } \
        if(!(id->flags&IDF_OVERRIDDEN)) { saveval; id->flags |= IDF_OVERRIDDEN; } \
        else { clearval; } \
    } \
    else \
    { \
        if(id->flags&IDF_OVERRIDDEN) { resetval; id->flags &= ~IDF_OVERRIDDEN; } \
        clearval; \
    }

void setvar(const char *name, int i, bool dofunc, bool doclamp) {
    GETVAR(id, name, );
    OVERRIDEVAR(return, id->overrideval.i = *id->storage.i, , )
    if (doclamp) * id->storage.i = ostd::clamp(i, id->minval, id->maxval);
    else *id->storage.i = i;
    if (dofunc) id->changed(cstate);
}
void setfvar(const char *name, float f, bool dofunc, bool doclamp) {
    _GETVAR(id, ID_FVAR, name, );
    OVERRIDEVAR(return, id->overrideval.f = *id->storage.f, , );
    if (doclamp) *id->storage.f = ostd::clamp(f, id->minvalf, id->maxvalf);
    else *id->storage.f = f;
    if (dofunc) id->changed(cstate);
}
void setsvar(const char *name, const char *str, bool dofunc) {
    _GETVAR(id, ID_SVAR, name, );
    OVERRIDEVAR(return, id->overrideval.s = *id->storage.s, delete[] id->overrideval.s, delete[] * id->storage.s);
    *id->storage.s = dup_ostr(str);
    if (dofunc) id->changed(cstate);
}
int getvar(const char *name) {
    GETVAR(id, name, 0);
    return *id->storage.i;
}
int getvarmin(const char *name) {
    GETVAR(id, name, 0);
    return id->minval;
}
int getvarmax(const char *name) {
    GETVAR(id, name, 0);
    return id->maxval;
}
float getfvarmin(const char *name) {
    _GETVAR(id, ID_FVAR, name, 0);
    return id->minvalf;
}
float getfvarmax(const char *name) {
    _GETVAR(id, ID_FVAR, name, 0);
    return id->maxvalf;
}

ICOMMAND(getvarmin, "s", (CsState &cs, char *s), cs.result->set_int(getvarmin(s)));
ICOMMAND(getvarmax, "s", (CsState &cs, char *s), cs.result->set_int(getvarmax(s)));
ICOMMAND(getfvarmin, "s", (CsState &cs, char *s), cs.result->set_float(getfvarmin(s)));
ICOMMAND(getfvarmax, "s", (CsState &cs, char *s), cs.result->set_float(getfvarmax(s)));

ICOMMAND(identexists, "s", (CsState &cs, char *s), cs.result->set_int(cstate.have_ident(s) ? 1 : 0));

const char *getalias(const char *name) {
    Ident *i = cstate.idents.at(name);
    return i && i->type == ID_ALIAS && (i->index >= MAX_ARGUMENTS || aliasstack->usedargs & (1 << i->index)) ? i->get_str() : "";
}

ICOMMAND(getalias, "s", (CsState &, char *s), result(getalias(s)));

int clampvar(Ident *id, int val, int minval, int maxval) {
    if (val < minval) val = minval;
    else if (val > maxval) val = maxval;
    else return val;
    debugcode(id->flags & IDF_HEX ?
              (minval <= 255 ? "valid range for %s is %d..0x%X" : "valid range for %s is 0x%X..0x%X") :
              "valid range for %s is %d..%d",
              id->name.data(), minval, maxval);
    return val;
}

void setvarchecked(Ident *id, int val) {
    if (id->flags & IDF_READONLY) debugcode("variable %s is read-only", id->name.data());
    else {
        OVERRIDEVAR(return, id->overrideval.i = *id->storage.i, , )
        if (val < id->minval || val > id->maxval) val = clampvar(id, val, id->minval, id->maxval);
        *id->storage.i = val;
        id->changed(cstate);
    }
}

static inline void setvarchecked(Ident *id, TaggedValue *args, int numargs) {
    int val = args[0].force_int();
    if (id->flags & IDF_HEX && numargs > 1) {
        val = (val << 16) | (args[1].force_int() << 8);
        if (numargs > 2) val |= args[2].force_int();
    }
    setvarchecked(id, val);
}

float clampfvar(Ident *id, float val, float minval, float maxval) {
    if (val < minval) val = minval;
    else if (val > maxval) val = maxval;
    else return val;
    debugcode("valid range for %s is %s..%s", id->name.data(), floatstr(minval), floatstr(maxval));
    return val;
}

void setfvarchecked(Ident *id, float val) {
    if (id->flags & IDF_READONLY) debugcode("variable %s is read-only", id->name.data());
    else {
        OVERRIDEVAR(return, id->overrideval.f = *id->storage.f, , );
        if (val < id->minvalf || val > id->maxvalf) val = clampfvar(id, val, id->minvalf, id->maxvalf);
        *id->storage.f = val;
        id->changed(cstate);
    }
}

void setsvarchecked(Ident *id, const char *val) {
    if (id->flags & IDF_READONLY) debugcode("variable %s is read-only", id->name.data());
    else {
        OVERRIDEVAR(return, id->overrideval.s = *id->storage.s, delete[] id->overrideval.s, delete[] * id->storage.s);
        *id->storage.s = dup_ostr(val);
        id->changed(cstate);
    }
}

bool CsState::add_command(ostd::ConstCharRange name, ostd::ConstCharRange args,
                          IdentFunc func, int type) {
    ostd::uint argmask = 0;
    int nargs = 0;
    bool limit = true;
    ostd::ConstCharRange fmt(args);
    for (; !fmt.empty(); fmt.pop_front()) {
        switch (fmt.front()) {
        case 'i':
        case 'b':
        case 'f':
        case 'F':
        case 't':
        case 'T':
        case 'E':
        case 'N':
        case 'D':
            if (nargs < MAX_ARGUMENTS) nargs++;
            break;
        case 'S':
        case 's':
        case 'e':
        case 'r':
        case '$':
            if (nargs < MAX_ARGUMENTS) {
                argmask |= 1 << nargs;
                nargs++;
            }
            break;
        case '1':
        case '2':
        case '3':
        case '4':
            if (nargs < MAX_ARGUMENTS)
                fmt.push_front_n(fmt.front() - '0' + 1);
            break;
        case 'C':
        case 'V':
            limit = false;
            break;
        default:
            ostd::err.writefln("builtin %s declared with illegal type: %c",
                               name, fmt.front());
            return false;
        }
    }
    if (limit && nargs > MAX_COMARGS) {
        ostd::err.writefln("builtin %s declared with too many arguments: %d",
                           name, nargs);
        return false;
    }
    add_ident(type, name, args.data(), argmask, nargs, func);
    return true;
}

bool addcommand(const char *name, IdentFunc fun, const char *args, int type) {
    ostd::uint argmask = 0;
    int numargs = 0;
    bool limit = true;
    if (args) for (const char *fmt = args; *fmt; fmt++) switch (*fmt) {
            case 'i':
            case 'b':
            case 'f':
            case 'F':
            case 't':
            case 'T':
            case 'E':
            case 'N':
            case 'D':
                if (numargs < MAX_ARGUMENTS) numargs++;
                break;
            case 'S':
            case 's':
            case 'e':
            case 'r':
            case '$':
                if (numargs < MAX_ARGUMENTS) {
                    argmask |= 1 << numargs;
                    numargs++;
                }
                break;
            case '1':
            case '2':
            case '3':
            case '4':
                if (numargs < MAX_ARGUMENTS) fmt -= *fmt - '0' + 1;
                break;
            case 'C':
            case 'V':
                limit = false;
                break;
            default:
                break; fatal("builtin %s declared with illegal type: %s", name, args); break;
            }
    if(limit && numargs > MAX_COMARGS) fatal("builtin %s declared with too many args: %d", name, numargs);
    cstate.add_ident(type, name, args, argmask, numargs, fun);
    return false;
}

const char *parsestring(const char *p) {
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

int unescapestring(char *dst, const char *src, const char *end) {
    char *start = dst;
    while (src < end) {
        int c = *src++;
        if (c == '^') {
            if (src >= end) break;
            int e = *src++;
            switch (e) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'f':
                *dst++ = '\f';
                break;
            default:
                *dst++ = e;
                break;
            }
        } else *dst++ = c;
    }
    *dst = '\0';
    return dst - start;
}

static char *conc(ostd::Vector<char> &buf, TaggedValue *v, int n, bool space, const char *prefix = nullptr, int prefixlen = 0) {
    if (prefix) {
        buf.push_n(prefix, prefixlen);
        if (space && n) buf.push(' ');
    }
    for (int i = 0; i < n; ++i) {
        const char *s = "";
        int len = 0;
        switch (v[i].type) {
        case VAL_INT:
            s = intstr(v[i].i);
            break;
        case VAL_FLOAT:
            s = floatstr(v[i].f);
            break;
        case VAL_STR:
        case VAL_CSTR:
            s = v[i].s;
            break;
        case VAL_MACRO:
            s = v[i].s;
            len = v[i].code[-1] >> 8;
            goto haslen;
        }
        len = int(strlen(s));
haslen:
        buf.push_n(s, len);
        if (i == n - 1) break;
        if (space) buf.push(' ');
    }
    buf.push('\0');
    return buf.data();
}

static char *conc(TaggedValue *v, int n, bool space, const char *prefix, int prefixlen) {
    static int vlen[MAX_ARGUMENTS];
    static char numbuf[3 * 256];
    int len = prefixlen, numlen = 0, i = 0;
    for (; i < n; i++) switch (v[i].type) {
        case VAL_MACRO:
            len += (vlen[i] = v[i].code[-1] >> 8);
            break;
        case VAL_STR:
        case VAL_CSTR:
            len += (vlen[i] = int(strlen(v[i].s)));
            break;
        case VAL_INT:
            if (numlen + 256 > int(sizeof(numbuf))) goto overflow;
            intformat(&numbuf[numlen], v[i].i);
            numlen += (vlen[i] = strlen(&numbuf[numlen]));
            break;
        case VAL_FLOAT:
            if (numlen + 256 > int(sizeof(numbuf))) goto overflow;
            floatformat(&numbuf[numlen], v[i].f);
            numlen += (vlen[i] = strlen(&numbuf[numlen]));
            break;
        default:
            vlen[i] = 0;
            break;
        }
overflow:
    if (space) len += ostd::max(prefix ? i : i - 1, 0);
    char *buf = new char[len + numlen + 1];
    int offset = 0, numoffset = 0;
    if (prefix) {
        memcpy(buf, prefix, prefixlen);
        offset += prefixlen;
        if (space && i) buf[offset++] = ' ';
    }
    for (ostd::Size j = 0; j < ostd::Size(i); ++j) {
        if (v[j].type == VAL_INT || v[j].type == VAL_FLOAT) {
            memcpy(&buf[offset], &numbuf[numoffset], vlen[j]);
            numoffset += vlen[j];
        } else if (vlen[j]) memcpy(&buf[offset], v[j].s, vlen[j]);
        offset += vlen[j];
        if (j == ostd::Size(i) - 1) break;
        if (space) buf[offset++] = ' ';
    }
    buf[offset] = '\0';
    if (i < n) {
        char *morebuf = conc(&v[i], n - i, space, buf, offset);
        delete[] buf;
        return morebuf;
    }
    return buf;
}

static inline char *conc(TaggedValue *v, int n, bool space) {
    return conc(v, n, space, nullptr, 0);
}

static inline void skipcomments(const char *&p) {
    for (;;) {
        p += strspn(p, " \t\r");
        if (p[0] != '/' || p[1] != '/') break;
        p += strcspn(p, "\n\0");
    }
}

static ostd::Vector<char> strbuf[4];
static int stridx = 0;

static inline void cutstring(const char *&p, ostd::ConstCharRange &s) {
    p++;
    const char *end = parsestring(p);
    int maxlen = int(end - p) + 1;

    stridx = (stridx + 1) % 4;
    ostd::Vector<char> &buf = strbuf[stridx];
    buf.reserve(maxlen);

    s = ostd::ConstCharRange(buf.data(), unescapestring(buf.data(), p, end));
    p = end;
    if (*p == '\"') p++;
}

static inline char *cutstring(const char *&p) {
    p++;
    const char *end = parsestring(p);
    char *buf = new char[end - p + 1];
    unescapestring(buf, p, end);
    p = end;
    if (*p == '\"') p++;
    return buf;
}

static inline const char *parseword(const char *p) {
    const int maxbrak = 100;
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

static inline void cutword(const char *&p, ostd::ConstCharRange &s) {
    const char *op = p;
    p = parseword(p);
    s = ostd::ConstCharRange(op, p - op);
}

static inline char *cutword(const char *&p) {
    const char *word = p;
    p = parseword(p);
    return p != word ? dup_ostr(ostd::ConstCharRange(word, p - word)) : nullptr;
}

#define retcode(type, defaultret) ((type) >= VAL_ANY ? ((type) == VAL_CSTR ? RET_STR : (defaultret)) : (type) << CODE_RET)
#define retcodeint(type) retcode(type, RET_INT)
#define retcodefloat(type) retcode(type, RET_FLOAT)
#define retcodeany(type) retcode(type, 0)
#define retcodestr(type) ((type) >= VAL_ANY ? RET_STR : (type) << CODE_RET)

static inline void compilestr(ostd::Vector<ostd::uint> &code, const char *word, int len, bool macro = false) {
    if (len <= 3 && !macro) {
        ostd::uint op = CODE_VALI | RET_STR;
        for (int i = 0; i < len; ++i) op |= ostd::uint(ostd::byte(word[i])) << ((i + 1) * 8);
        code.push(op);
        return;
    }
    code.push((macro ? CODE_MACRO : CODE_VAL | RET_STR) | (len << 8));
    code.push_n((const ostd::uint *)word, len / sizeof(ostd::uint));
    ostd::Size endlen = len % sizeof(ostd::uint);
    union {
        char c[sizeof(ostd::uint)];
        ostd::uint u;
    } end;
    end.u = 0;
    memcpy(end.c, word + len - endlen, endlen);
    code.push(end.u);
}

static inline void compilestr(ostd::Vector<ostd::uint> &code) {
    code.push(CODE_VALI | RET_STR);
}
static inline void compilestr(ostd::Vector<ostd::uint> &code, ostd::ConstCharRange word, bool macro = false) {
    compilestr(code, word.data(), word.size(), macro);
}

static inline void compileunescapestring(ostd::Vector<ostd::uint> &code, const char *&p, bool macro = false) {
    p++;
    const char *end = parsestring(p);
    code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    code.reserve(code.size() + (end - p) / sizeof(ostd::uint) + 1);
    char *buf = (char *)&code[code.size()];
    int len = unescapestring(buf, p, end);
    memset(&buf[len], 0, sizeof(ostd::uint) - len % sizeof(ostd::uint));
    code.back() |= len << 8;
    code.advance(len / sizeof(ostd::uint) + 1);
    p = end;
    if (*p == '\"') p++;
}

static inline void compileint(ostd::Vector<ostd::uint> &code, int i = 0) {
    if (i >= -0x800000 && i <= 0x7FFFFF)
        code.push(CODE_VALI | RET_INT | (i << 8));
    else {
        code.push(CODE_VAL | RET_INT);
        code.push(i);
    }
}

static inline void compilenull(ostd::Vector<ostd::uint> &code) {
    code.push(CODE_VALI | RET_NULL);
}

static ostd::uint emptyblock[VAL_ANY][2] = {
    { CODE_START + 0x100, CODE_EXIT | RET_NULL },
    { CODE_START + 0x100, CODE_EXIT | RET_INT },
    { CODE_START + 0x100, CODE_EXIT | RET_FLOAT },
    { CODE_START + 0x100, CODE_EXIT | RET_STR }
};

static inline void compileblock(ostd::Vector<ostd::uint> &code) {
    code.push(CODE_EMPTY);
}

static void compilestatements(ostd::Vector<ostd::uint> &code, const char *&p, int rettype, int brak = '\0', int prevargs = 0);

static inline const char *compileblock(ostd::Vector<ostd::uint> &code, const char *p, int rettype = RET_NULL, int brak = '\0') {
    ostd::Size start = code.size();
    code.push(CODE_BLOCK);
    code.push(CODE_OFFSET | ((start + 2) << 8));
    if (p) compilestatements(code, p, VAL_ANY, brak);
    if (code.size() > start + 2) {
        code.push(CODE_EXIT | rettype);
        code[start] |= ostd::uint(code.size() - (start + 1)) << 8;
    } else {
        code.resize(start);
        code.push(CODE_EMPTY | rettype);
    }
    return p;
}

static inline void compileident(ostd::Vector<ostd::uint> &code, Ident *id = cstate.dummy) {
    code.push((id->index < MAX_ARGUMENTS ? CODE_IDENTARG : CODE_IDENT) | (id->index << 8));
}

static inline void compileident(ostd::Vector<ostd::uint> &code, ostd::ConstCharRange word) {
    compileident(code, cstate.new_ident(word, IDF_UNKNOWN));
}

static inline void compileint(ostd::Vector<ostd::uint> &code, ostd::ConstCharRange word) {
    compileint(code, word.size() ? parseint(word.data()) : 0);
}

static inline void compilefloat(ostd::Vector<ostd::uint> &code, float f = 0.0f) {
    if (int(f) == f && f >= -0x800000 && f <= 0x7FFFFF)
        code.push(CODE_VALI | RET_FLOAT | (int(f) << 8));
    else {
        union {
            float f;
            ostd::uint u;
        } conv;
        conv.f = f;
        code.push(CODE_VAL | RET_FLOAT);
        code.push(conv.u);
    }
}

static inline void compilefloat(ostd::Vector<ostd::uint> &code, ostd::ConstCharRange &word) {
    compilefloat(code, word.size() ? parsefloat(word.data()) : 0.0f);
}

static inline bool getbool(const char *s) {
    switch (s[0]) {
    case '+':
    case '-':
        switch (s[1]) {
        case '0':
            break;
        case '.':
            return !isdigit(s[2]) || parsefloat(s) != 0;
        default:
            return true;
        }
    /* fallthrough */
    case '0': {
        char *end;
        int val = int(strtoul((char *)s, &end, 0));
        if (val) return true;
        switch (*end) {
        case 'e':
        case '.':
            return parsefloat(s) != 0;
        default:
            return false;
        }
    }
    case '.':
        return !isdigit(s[1]) || parsefloat(s) != 0;
    case '\0':
        return false;
    default:
        return true;
    }
}

static inline bool getbool(const TaggedValue &v) {
    switch (v.type) {
    case VAL_FLOAT:
        return v.f != 0;
    case VAL_INT:
        return v.i != 0;
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        return getbool(v.s);
    default:
        return false;
    }
}

static inline void compileval(ostd::Vector<ostd::uint> &code, int wordtype, ostd::ConstCharRange word = ostd::ConstCharRange(nullptr, nullptr)) {
    switch (wordtype) {
    case VAL_CANY:
        if (word.size()) compilestr(code, word, true);
        else compilenull(code);
        break;
    case VAL_CSTR:
        compilestr(code, word, true);
        break;
    case VAL_ANY:
        if (word.size()) compilestr(code, word);
        else compilenull(code);
        break;
    case VAL_STR:
        compilestr(code, word);
        break;
    case VAL_FLOAT:
        compilefloat(code, word);
        break;
    case VAL_INT:
        compileint(code, word);
        break;
    case VAL_COND:
        if (word.size()) compileblock(code, word.data());
        else compilenull(code);
        break;
    case VAL_CODE:
        compileblock(code, word.data());
        break;
    case VAL_IDENT:
        compileident(code, word);
        break;
    default:
        break;
    }
}

static ostd::ConstCharRange unusedword(nullptr, nullptr);
static bool compilearg(ostd::Vector<ostd::uint> &code, const char *&p, int wordtype, int prevargs = MAX_RESULTS, ostd::ConstCharRange &word = unusedword);

static void compilelookup(ostd::Vector<ostd::uint> &code, const char *&p, int ltype, int prevargs = MAX_RESULTS) {
    ostd::ConstCharRange lookup;
    switch (*++p) {
    case '(':
    case '[':
        if (!compilearg(code, p, VAL_CSTR, prevargs)) goto invalid;
        break;
    case '$':
        compilelookup(code, p, VAL_CSTR, prevargs);
        break;
    case '\"':
        cutstring(p, lookup);
        goto lookupid;
    default: {
        cutword(p, lookup);
        if (!lookup.size()) goto invalid;
lookupid:
        Ident *id = cstate.new_ident(lookup, IDF_UNKNOWN);
        if (id) switch (id->type) {
            case ID_VAR:
                code.push(CODE_IVAR | retcodeint(ltype) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    code.pop();
                    break;
                case VAL_CODE:
                    code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    code.push(CODE_IDENTU);
                    break;
                }
                return;
            case ID_FVAR:
                code.push(CODE_FVAR | retcodefloat(ltype) | (id->index << 8));
                switch (ltype) {
                case VAL_POP:
                    code.pop();
                    break;
                case VAL_CODE:
                    code.push(CODE_COMPILE);
                    break;
                case VAL_IDENT:
                    code.push(CODE_IDENTU);
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
                    code.push(CODE_SVARM | (id->index << 8));
                    break;
                default:
                    code.push(CODE_SVAR | retcodestr(ltype) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_ALIAS:
                switch (ltype) {
                case VAL_POP:
                    return;
                case VAL_CANY:
                case VAL_COND:
                    code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                    break;
                case VAL_CSTR:
                case VAL_CODE:
                case VAL_IDENT:
                    code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | RET_STR | (id->index << 8));
                    break;
                default:
                    code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPARG : CODE_LOOKUP) | retcodestr(ltype) | (id->index << 8));
                    break;
                }
                goto done;
            case ID_COMMAND: {
                int comtype = CODE_COM, numargs = 0;
                if (prevargs >= MAX_RESULTS) code.push(CODE_ENTER);
                for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
                    case 'S':
                        compilestr(code);
                        numargs++;
                        break;
                    case 's':
                        compilestr(code, nullptr, 0, true);
                        numargs++;
                        break;
                    case 'i':
                        compileint(code);
                        numargs++;
                        break;
                    case 'b':
                        compileint(code, INT_MIN);
                        numargs++;
                        break;
                    case 'f':
                        compilefloat(code);
                        numargs++;
                        break;
                    case 'F':
                        code.push(CODE_DUP | RET_FLOAT);
                        numargs++;
                        break;
                    case 'E':
                    case 'T':
                    case 't':
                        compilenull(code);
                        numargs++;
                        break;
                    case 'e':
                        compileblock(code);
                        numargs++;
                        break;
                    case 'r':
                        compileident(code);
                        numargs++;
                        break;
                    case '$':
                        compileident(code, id);
                        numargs++;
                        break;
                    case 'N':
                        compileint(code, -1);
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
                code.push(comtype | retcodeany(ltype) | (id->index << 8));
                code.push((prevargs >= MAX_RESULTS ? CODE_EXIT : CODE_RESULT_ARG) | retcodeany(ltype));
                goto done;
compilecomv:
                code.push(comtype | retcodeany(ltype) | (numargs << 8) | (id->index << 13));
                code.push((prevargs >= MAX_RESULTS ? CODE_EXIT : CODE_RESULT_ARG) | retcodeany(ltype));
                goto done;
            }
            default:
                goto invalid;
            }
        compilestr(code, lookup, true);
        break;
    }
    }
    switch (ltype) {
    case VAL_CANY:
    case VAL_COND:
        code.push(CODE_LOOKUPMU);
        break;
    case VAL_CSTR:
    case VAL_CODE:
    case VAL_IDENT:
        code.push(CODE_LOOKUPMU | RET_STR);
        break;
    default:
        code.push(CODE_LOOKUPU | retcodeany(ltype));
        break;
    }
done:
    switch (ltype) {
    case VAL_POP:
        code.push(CODE_POP);
        break;
    case VAL_CODE:
        code.push(CODE_COMPILE);
        break;
    case VAL_COND:
        code.push(CODE_COND);
        break;
    case VAL_IDENT:
        code.push(CODE_IDENTU);
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
        compilenull(code);
        break;
    default:
        compileval(code, ltype);
        break;
    }
}

static bool compileblockstr(ostd::Vector<ostd::uint> &code, const char *str, const char *end, bool macro) {
    int start = code.size();
    code.push(macro ? CODE_MACRO : CODE_VAL | RET_STR);
    code.reserve(code.size() + (end - str) / sizeof(ostd::uint) + 1);
    char *buf = (char *)&code[code.size()];
    int len = 0;
    while (str < end) {
        int n = strcspn(str, "\r/\"@]\0");
        memcpy(&buf[len], str, n);
        len += n;
        str += n;
        switch (*str) {
        case '\r':
            str++;
            break;
        case '\"': {
            const char *start = str;
            str = parsestring(str + 1);
            if (*str == '\"') str++;
            memcpy(&buf[len], start, str - start);
            len += str - start;
            break;
        }
        case '/':
            if (str[1] == '/') str += strcspn(str, "\n\0");
            else buf[len++] = *str++;
            break;
        case '@':
        case ']':
            if (str < end) {
                buf[len++] = *str++;
                break;
            }
        case '\0':
            goto done;
        }
    }
done:
    memset(&buf[len], '\0', sizeof(ostd::uint) - len % sizeof(ostd::uint));
    code.advance(len / sizeof(ostd::uint) + 1);
    code[start] |= len << 8;
    return true;
}

static bool compileblocksub(ostd::Vector<ostd::uint> &code, const char *&p, int prevargs) {
    ostd::ConstCharRange lookup;
    const char *op;
    switch (*p) {
    case '(':
        if (!compilearg(code, p, VAL_CANY, prevargs)) return false;
        break;
    case '[':
        if (!compilearg(code, p, VAL_CSTR, prevargs)) return false;
        code.push(CODE_LOOKUPMU);
        break;
    case '\"':
        cutstring(p, lookup);
        goto lookupid;
    default: {
        op = p;
        while (isalnum(*p) || *p == '_') p++;
        lookup = ostd::ConstCharRange(op, p - op);
        if (lookup.empty()) return false;
lookupid:
        Ident *id = cstate.new_ident(lookup, IDF_UNKNOWN);
        if (id) switch (id->type) {
            case ID_VAR:
                code.push(CODE_IVAR | (id->index << 8));
                goto done;
            case ID_FVAR:
                code.push(CODE_FVAR | (id->index << 8));
                goto done;
            case ID_SVAR:
                code.push(CODE_SVARM | (id->index << 8));
                goto done;
            case ID_ALIAS:
                code.push((id->index < MAX_ARGUMENTS ? CODE_LOOKUPMARG : CODE_LOOKUPM) | (id->index << 8));
                goto done;
            }
        compilestr(code, lookup, true);
        code.push(CODE_LOOKUPMU);
done:
        break;
    }
    }
    return true;
}

static void compileblockmain(ostd::Vector<ostd::uint> &code, const char *&p, int wordtype, int prevargs) {
    const char *line = p, *start = p;
    int concs = 0;
    for (int brak = 1; brak;) {
        p += strcspn(p, "@\"/[]\0");
        int c = *p++;
        switch (c) {
        case '\0':
            debugcodeline(line, "missing \"]\"");
            p--;
            goto done;
        case '\"':
            p = parsestring(p);
            if (*p == '\"') p++;
            break;
        case '/':
            if (*p == '/') p += strcspn(p, "\n\0");
            break;
        case '[':
            brak++;
            break;
        case ']':
            brak--;
            break;
        case '@': {
            const char *esc = p;
            while (*p == '@') p++;
            int level = p - (esc - 1);
            if (brak > level) continue;
            else if (brak < level) debugcodeline(line, "too many @s");
            if (!concs && prevargs >= MAX_RESULTS) code.push(CODE_ENTER);
            if (concs + 2 > MAX_ARGUMENTS) {
                code.push(CODE_CONCW | RET_STR | (concs << 8));
                concs = 1;
            }
            if (compileblockstr(code, start, esc - 1, true)) concs++;
            if (compileblocksub(code, p, prevargs + concs)) concs++;
            if (concs) start = p;
            else if (prevargs >= MAX_RESULTS) code.pop();
            break;
        }
        }
    }
done:
    if (p - 1 > start) {
        if (!concs) switch (wordtype) {
            case VAL_POP:
                return;
            case VAL_CODE:
            case VAL_COND:
                p = compileblock(code, start, RET_NULL, ']');
                return;
            case VAL_IDENT:
                compileident(code, ostd::ConstCharRange(start, p - 1));
                return;
            }
        switch (wordtype) {
        case VAL_CSTR:
        case VAL_CODE:
        case VAL_IDENT:
        case VAL_CANY:
        case VAL_COND:
            compileblockstr(code, start, p - 1, true);
            break;
        default:
            compileblockstr(code, start, p - 1, concs > 0);
            break;
        }
        if (concs > 1) concs++;
    }
    if (concs) {
        if (prevargs >= MAX_RESULTS) {
            code.push(CODE_CONCM | retcodeany(wordtype) | (concs << 8));
            code.push(CODE_EXIT | retcodeany(wordtype));
        } else code.push(CODE_CONCW | retcodeany(wordtype) | (concs << 8));
    }
    switch (wordtype) {
    case VAL_POP:
        if (concs || p - 1 > start) code.push(CODE_POP);
        break;
    case VAL_COND:
        if (!concs && p - 1 <= start) compilenull(code);
        else code.push(CODE_COND);
        break;
    case VAL_CODE:
        if (!concs && p - 1 <= start) compileblock(code);
        else code.push(CODE_COMPILE);
        break;
    case VAL_IDENT:
        if (!concs && p - 1 <= start) compileident(code);
        else code.push(CODE_IDENTU);
        break;
    case VAL_CSTR:
    case VAL_CANY:
        if (!concs && p - 1 <= start) compilestr(code, nullptr, 0, true);
        break;
    case VAL_STR:
    case VAL_NULL:
    case VAL_ANY:
    case VAL_WORD:
        if (!concs && p - 1 <= start) compilestr(code);
        break;
    default:
        if (!concs) {
            if (p - 1 <= start) compileval(code, wordtype);
            else code.push(CODE_FORCE | (wordtype << CODE_RET));
        }
        break;
    }
}

static bool compilearg(ostd::Vector<ostd::uint> &code, const char *&p, int wordtype, int prevargs, ostd::ConstCharRange &word) {
    skipcomments(p);
    switch (*p) {
    case '\"':
        switch (wordtype) {
        case VAL_POP:
            p = parsestring(p + 1);
            if (*p == '\"') p++;
            break;
        case VAL_COND: {
            char *s = cutstring(p);
            if (s[0]) compileblock(code, s);
            else compilenull(code);
            delete[] s;
            break;
        }
        case VAL_CODE: {
            char *s = cutstring(p);
            compileblock(code, s);
            delete[] s;
            break;
        }
        case VAL_WORD:
            cutstring(p, word);
            break;
        case VAL_ANY:
        case VAL_STR:
            compileunescapestring(code, p);
            break;
        case VAL_CANY:
        case VAL_CSTR:
            compileunescapestring(code, p, true);
            break;
        default: {
            ostd::ConstCharRange s;
            cutstring(p, s);
            compileval(code, wordtype, s);
            break;
        }
        }
        return true;
    case '$':
        compilelookup(code, p, wordtype, prevargs);
        return true;
    case '(':
        p++;
        if (prevargs >= MAX_RESULTS) {
            code.push(CODE_ENTER);
            compilestatements(code, p, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')');
            code.push(CODE_EXIT | retcodeany(wordtype));
        } else {
            ostd::Size start = code.size();
            compilestatements(code, p, wordtype > VAL_ANY ? VAL_CANY : VAL_ANY, ')', prevargs);
            if (code.size() > start) code.push(CODE_RESULT_ARG | retcodeany(wordtype));
            else {
                compileval(code, wordtype);
                return true;
            }
        }
        switch (wordtype) {
        case VAL_POP:
            code.push(CODE_POP);
            break;
        case VAL_COND:
            code.push(CODE_COND);
            break;
        case VAL_CODE:
            code.push(CODE_COMPILE);
            break;
        case VAL_IDENT:
            code.push(CODE_IDENTU);
            break;
        }
        return true;
    case '[':
        p++;
        compileblockmain(code, p, wordtype, prevargs);
        return true;
    default:
        switch (wordtype) {
        case VAL_POP: {
            const char *s = p;
            p = parseword(p);
            return p != s;
        }
        case VAL_COND: {
            char *s = cutword(p);
            if (!s) return false;
            compileblock(code, s);
            delete[] s;
            return true;
        }
        case VAL_CODE: {
            char *s = cutword(p);
            if (!s) return false;
            compileblock(code, s);
            delete[] s;
            return true;
        }
        case VAL_WORD:
            cutword(p, word);
            return !word.empty();
        default: {
            ostd::ConstCharRange s;
            cutword(p, s);
            if (s.empty()) return false;
            compileval(code, wordtype, s);
            return true;
        }
        }
    }
}

static void compilestatements(ostd::Vector<ostd::uint> &code, const char *&p, int rettype, int brak, int prevargs) {
    const char *line = p;
    ostd::ConstCharRange idname;
    int numargs;
    for (;;) {
        skipcomments(p);
        idname = ostd::ConstCharRange(nullptr, nullptr);
        bool more = compilearg(code, p, VAL_WORD, prevargs, idname);
        if (!more) goto endstatement;
        skipcomments(p);
        if (p[0] == '=') switch (p[1]) {
            case '/':
                if (p[2] != '/') break;
            case ';':
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\0':
                p++;
                if (idname.data()) {
                    Ident *id = cstate.new_ident(idname, IDF_UNKNOWN);
                    if (id) switch (id->type) {
                        case ID_ALIAS:
                            if (!(more = compilearg(code, p, VAL_ANY, prevargs))) compilestr(code);
                            code.push((id->index < MAX_ARGUMENTS ? CODE_ALIASARG : CODE_ALIAS) | (id->index << 8));
                            goto endstatement;
                        case ID_VAR:
                            if (!(more = compilearg(code, p, VAL_INT, prevargs))) compileint(code);
                            code.push(CODE_IVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_FVAR:
                            if (!(more = compilearg(code, p, VAL_FLOAT, prevargs))) compilefloat(code);
                            code.push(CODE_FVAR1 | (id->index << 8));
                            goto endstatement;
                        case ID_SVAR:
                            if (!(more = compilearg(code, p, VAL_CSTR, prevargs))) compilestr(code);
                            code.push(CODE_SVAR1 | (id->index << 8));
                            goto endstatement;
                        }
                    compilestr(code, idname, true);
                }
                if (!(more = compilearg(code, p, VAL_ANY))) compilestr(code);
                code.push(CODE_ALIASU);
                goto endstatement;
            }
        numargs = 0;
        if (!idname.data()) {
noid:
            while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_CANY, prevargs + numargs))) numargs++;
            code.push(CODE_CALLU | (numargs << 8));
        } else {
            Ident *id = cstate.idents.at(idname);
            if (!id) {
                if (!checknumber(idname.data())) {
                    compilestr(code, idname, true);
                    goto noid;
                }
                switch (rettype) {
                case VAL_ANY:
                case VAL_CANY: {
                    char *end = (char *)idname.data();
                    int val = int(strtoul(idname.data(), &end, 0));
                    if (end < &idname[idname.size()]) compilestr(code, idname, rettype == VAL_CANY);
                    else compileint(code, val);
                    break;
                }
                default:
                    compileval(code, rettype, idname);
                    break;
                }
                code.push(CODE_RESULT);
            } else switch (id->type) {
                case ID_ALIAS:
                    while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_ANY, prevargs + numargs))) numargs++;
                    code.push((id->index < MAX_ARGUMENTS ? CODE_CALLARG : CODE_CALL) | (numargs << 8) | (id->index << 13));
                    break;
                case ID_COMMAND: {
                    int comtype = CODE_COM, fakeargs = 0;
                    bool rep = false;
                    for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
                        case 'S':
                        case 's':
                            if (more) more = compilearg(code, p, *fmt == 's' ? VAL_CSTR : VAL_STR, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compilestr(code, nullptr, 0, *fmt == 's');
                                fakeargs++;
                            } else if (!fmt[1]) {
                                int numconc = 1;
                                while (numargs + numconc < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_CSTR, prevargs + numargs + numconc))) numconc++;
                                if (numconc > 1) code.push(CODE_CONC | RET_STR | (numconc << 8));
                            }
                            numargs++;
                            break;
                        case 'i':
                            if (more) more = compilearg(code, p, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileint(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'b':
                            if (more) more = compilearg(code, p, VAL_INT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileint(code, INT_MIN);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'f':
                            if (more) more = compilearg(code, p, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compilefloat(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'F':
                            if (more) more = compilearg(code, p, VAL_FLOAT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                code.push(CODE_DUP | RET_FLOAT);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'T':
                        case 't':
                            if (more) more = compilearg(code, p, *fmt == 't' ? VAL_CANY : VAL_ANY, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compilenull(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'E':
                            if (more) more = compilearg(code, p, VAL_COND, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compilenull(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'e':
                            if (more) more = compilearg(code, p, VAL_CODE, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileblock(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case 'r':
                            if (more) more = compilearg(code, p, VAL_IDENT, prevargs + numargs);
                            if (!more) {
                                if (rep) break;
                                compileident(code);
                                fakeargs++;
                            }
                            numargs++;
                            break;
                        case '$':
                            compileident(code, id);
                            numargs++;
                            break;
                        case 'N':
                            compileint(code, numargs - fakeargs);
                            numargs++;
                            break;
                        case 'C':
                            comtype = CODE_COMC;
                            if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case 'V':
                            comtype = CODE_COMV;
                            if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_CANY, prevargs + numargs))) numargs++;
                            goto compilecomv;
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                            if (more && numargs < MAX_ARGUMENTS) {
                                int numrep = *fmt - '0' + 1;
                                fmt -= numrep;
                                rep = true;
                            } else for (; numargs > MAX_ARGUMENTS; numargs--) code.push(CODE_POP);
                            break;
                        }
                    code.push(comtype | retcodeany(rettype) | (id->index << 8));
                    break;
compilecomv:
                    code.push(comtype | retcodeany(rettype) | (numargs << 8) | (id->index << 13));
                    break;
                }
                case ID_LOCAL:
                    if (more) while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_IDENT, prevargs + numargs))) numargs++;
                    if (more) while ((more = compilearg(code, p, VAL_POP)));
                    code.push(CODE_LOCAL | (numargs << 8));
                    break;
                case ID_DO:
                    if (more) more = compilearg(code, p, VAL_CODE, prevargs);
                    code.push((more ? CODE_DO : CODE_NULL) | retcodeany(rettype));
                    break;
                case ID_DOARGS:
                    if (more) more = compilearg(code, p, VAL_CODE, prevargs);
                    code.push((more ? CODE_DOARGS : CODE_NULL) | retcodeany(rettype));
                    break;
                case ID_IF:
                    if (more) more = compilearg(code, p, VAL_CANY, prevargs);
                    if (!more) code.push(CODE_NULL | retcodeany(rettype));
                    else {
                        int start1 = code.size();
                        more = compilearg(code, p, VAL_CODE, prevargs + 1);
                        if (!more) {
                            code.push(CODE_POP);
                            code.push(CODE_NULL | retcodeany(rettype));
                        } else {
                            int start2 = code.size();
                            more = compilearg(code, p, VAL_CODE, prevargs + 2);
                            ostd::uint inst1 = code[start1], op1 = inst1 & ~CODE_RET_MASK, len1 = start2 - (start1 + 1);
                            if (!more) {
                                if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                    code[start1] = (len1 << 8) | CODE_JUMP_FALSE;
                                    code[start1 + 1] = CODE_ENTER_RESULT;
                                    code[start1 + len1] = (code[start1 + len1] & ~CODE_RET_MASK) | retcodeany(rettype);
                                    break;
                                }
                                compileblock(code);
                            } else {
                                ostd::uint inst2 = code[start2], op2 = inst2 & ~CODE_RET_MASK, len2 = code.size() - (start2 + 1);
                                if (op2 == (CODE_BLOCK | (len2 << 8))) {
                                    if (op1 == (CODE_BLOCK | (len1 << 8))) {
                                        code[start1] = ((start2 - start1) << 8) | CODE_JUMP_FALSE;
                                        code[start1 + 1] = CODE_ENTER_RESULT;
                                        code[start1 + len1] = (code[start1 + len1] & ~CODE_RET_MASK) | retcodeany(rettype);
                                        code[start2] = (len2 << 8) | CODE_JUMP;
                                        code[start2 + 1] = CODE_ENTER_RESULT;
                                        code[start2 + len2] = (code[start2 + len2] & ~CODE_RET_MASK) | retcodeany(rettype);
                                        break;
                                    } else if (op1 == (CODE_EMPTY | (len1 << 8))) {
                                        code[start1] = CODE_NULL | (inst2 & CODE_RET_MASK);
                                        code[start2] = (len2 << 8) | CODE_JUMP_TRUE;
                                        code[start2 + 1] = CODE_ENTER_RESULT;
                                        code[start2 + len2] = (code[start2 + len2] & ~CODE_RET_MASK) | retcodeany(rettype);
                                        break;
                                    }
                                }
                            }
                            code.push(CODE_COM | retcodeany(rettype) | (id->index << 8));
                        }
                    }
                    break;
                case ID_RESULT:
                    if (more) more = compilearg(code, p, VAL_ANY, prevargs);
                    code.push((more ? CODE_RESULT : CODE_NULL) | retcodeany(rettype));
                    break;
                case ID_NOT:
                    if (more) more = compilearg(code, p, VAL_CANY, prevargs);
                    code.push((more ? CODE_NOT : CODE_TRUE) | retcodeany(rettype));
                    break;
                case ID_AND:
                case ID_OR:
                    if (more) more = compilearg(code, p, VAL_COND, prevargs);
                    if (!more) {
                        code.push((id->type == ID_AND ? CODE_TRUE : CODE_FALSE) | retcodeany(rettype));
                    } else {
                        numargs++;
                        int start = code.size(), end = start;
                        while (numargs < MAX_ARGUMENTS) {
                            more = compilearg(code, p, VAL_COND, prevargs + numargs);
                            if (!more) break;
                            numargs++;
                            if ((code[end] & ~CODE_RET_MASK) != (CODE_BLOCK | (ostd::uint(code.size() - (end + 1)) << 8))) break;
                            end = code.size();
                        }
                        if (more) {
                            while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_COND, prevargs + numargs))) numargs++;
                            code.push(CODE_COMV | retcodeany(rettype) | (numargs << 8) | (id->index << 13));
                        } else {
                            ostd::uint op = id->type == ID_AND ? CODE_JUMP_RESULT_FALSE : CODE_JUMP_RESULT_TRUE;
                            code.push(op);
                            end = code.size();
                            while (start + 1 < end) {
                                ostd::uint len = code[start] >> 8;
                                code[start] = ((end - (start + 1)) << 8) | op;
                                code[start + 1] = CODE_ENTER;
                                code[start + len] = (code[start + len] & ~CODE_RET_MASK) | retcodeany(rettype);
                                start += len + 1;
                            }
                        }
                    }
                    break;
                case ID_VAR:
                    if (!(more = compilearg(code, p, VAL_INT, prevargs))) code.push(CODE_PRINT | (id->index << 8));
                    else if (!(id->flags & IDF_HEX) || !(more = compilearg(code, p, VAL_INT, prevargs + 1))) code.push(CODE_IVAR1 | (id->index << 8));
                    else if (!(more = compilearg(code, p, VAL_INT, prevargs + 2))) code.push(CODE_IVAR2 | (id->index << 8));
                    else code.push(CODE_IVAR3 | (id->index << 8));
                    break;
                case ID_FVAR:
                    if (!(more = compilearg(code, p, VAL_FLOAT, prevargs))) code.push(CODE_PRINT | (id->index << 8));
                    else code.push(CODE_FVAR1 | (id->index << 8));
                    break;
                case ID_SVAR:
                    if (!(more = compilearg(code, p, VAL_CSTR, prevargs))) code.push(CODE_PRINT | (id->index << 8));
                    else {
                        do ++numargs;
                        while (numargs < MAX_ARGUMENTS && (more = compilearg(code, p, VAL_CANY, prevargs + numargs)));
                        if (numargs > 1) code.push(CODE_CONC | RET_STR | (numargs << 8));
                        code.push(CODE_SVAR1 | (id->index << 8));
                    }
                    break;
                }
        }
endstatement:
        if (more) while (compilearg(code, p, VAL_POP));
        p += strcspn(p, ")];/\n\0");
        int c = *p++;
        switch (c) {
        case '\0':
            if (c != brak) debugcodeline(line, "missing \"%c\"", brak);
            p--;
            return;

        case ')':
        case ']':
            if (c == brak) return;
            debugcodeline(line, "unexpected \"%c\"", c);
            break;

        case '/':
            if (*p == '/') p += strcspn(p, "\n\0");
            goto endstatement;
        }
    }
}

static void compilemain(ostd::Vector<ostd::uint> &code, const char *p, int rettype = VAL_ANY) {
    code.push(CODE_START);
    compilestatements(code, p, VAL_ANY);
    code.push(CODE_EXIT | (rettype < VAL_ANY ? rettype << CODE_RET : 0));
}

ostd::uint *compilecode(const char *p) {
    ostd::Vector<ostd::uint> buf;
    buf.reserve(64);
    compilemain(buf, p);
    ostd::uint *code = new ostd::uint[buf.size()];
    memcpy(code, buf.data(), buf.size()*sizeof(ostd::uint));
    code[0] += 0x100;
    return code;
}

static inline const ostd::uint *forcecode(TaggedValue &v) {
    if (v.type != VAL_CODE) {
        ostd::Vector<ostd::uint> buf;
        buf.reserve(64);
        compilemain(buf, v.get_str());
        v.cleanup();
        v.set_code(buf.disown() + 1);
    }
    return v.code;
}

static inline void forcecond(TaggedValue &v) {
    switch (v.type) {
    case VAL_STR:
    case VAL_MACRO:
    case VAL_CSTR:
        if (v.s[0]) forcecode(v);
        else v.set_int(0);
        break;
    }
}

void keepcode(ostd::uint *code) {
    if (!code) return;
    switch (*code & CODE_OP_MASK) {
    case CODE_START:
        *code += 0x100;
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
    case CODE_START:
        code[-1] += 0x100;
        break;
    case CODE_OFFSET:
        code -= int(code[-1] >> 8);
        *code += 0x100;
        break;
    }
}

void freecode(ostd::uint *code) {
    if (!code) return;
    switch (*code & CODE_OP_MASK) {
    case CODE_START:
        *code -= 0x100;
        if (int(*code) < 0x100) delete[] code;
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
    case CODE_START:
        code[-1] -= 0x100;
        if (int(code[-1]) < 0x100) delete[] &code[-1];
        break;
    case CODE_OFFSET:
        code -= int(code[-1] >> 8);
        *code -= 0x100;
        if (int(*code) < 0x100) delete[] code;
        break;
    }
}

void printvar(Ident *id, int i) {
    if (i < 0) printf("%s = %d\n", id->name.data(), i);
    else if (id->flags & IDF_HEX && id->maxval == 0xFFFFFF)
        printf("%s = 0x%.6X (%d, %d, %d)\n", id->name.data(), i, (i >> 16) & 0xFF, (i >> 8) & 0xFF, i & 0xFF);
    else
        printf(id->flags & IDF_HEX ? "%s = 0x%X\n" : "%s = %d\n", id->name.data(), i);
}

void printfvar(Ident *id, float f) {
    printf("%s = %s\n", id->name.data(), floatstr(f));
}

void printsvar(Ident *id, const char *s) {
    printf(strchr(s, '"') ? "%s = [%s]\n" : "%s = \"%s\"\n", id->name.data(), s);
}

void printvar(Ident *id) {
    switch (id->type) {
    case ID_VAR:
        printvar(id, *id->storage.i);
        break;
    case ID_FVAR:
        printfvar(id, *id->storage.f);
        break;
    case ID_SVAR:
        printsvar(id, *id->storage.s);
        break;
    }
}

using CommandFunc = void (__cdecl *)(CsState &);
using CommandFunc1 = void (__cdecl *)(CsState &, void *);
using CommandFunc2 = void (__cdecl *)(CsState &, void *, void *);
using CommandFunc3 = void (__cdecl *)(CsState &, void *, void *, void *);
using CommandFunc4 = void (__cdecl *)(CsState &, void *, void *, void *, void *);
using CommandFunc5 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *);
using CommandFunc6 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *);
using CommandFunc7 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc8 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc9 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc10 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc11 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFunc12 = void (__cdecl *)(CsState &, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using CommandFuncTv = void (__cdecl *)(CsState &, TaggedValue *, int);

static const ostd::uint *skipcode(const ostd::uint *code, TaggedValue &result = no_ret) {
    int depth = 0;
    for (;;) {
        ostd::uint op = *code++;
        switch (op & 0xFF) {
        case CODE_MACRO:
        case CODE_VAL|RET_STR: {
            ostd::uint len = op >> 8;
            code += len / sizeof(ostd::uint) + 1;
            continue;
        }
        case CODE_BLOCK:
        case CODE_JUMP:
        case CODE_JUMP_TRUE:
        case CODE_JUMP_FALSE:
        case CODE_JUMP_RESULT_TRUE:
        case CODE_JUMP_RESULT_FALSE: {
            ostd::uint len = op >> 8;
            code += len;
            continue;
        }
        case CODE_ENTER:
        case CODE_ENTER_RESULT:
            ++depth;
            continue;
        case CODE_EXIT|RET_NULL:
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            if (depth <= 0) {
                if (&result != &no_ret) result.force(op & CODE_RET_MASK);
                return code;
            }
            --depth;
            continue;
        }
    }
}

static inline void callcommand(Ident *id, TaggedValue *args, int numargs, bool lookup = false) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (const char *fmt = id->args; *fmt; fmt++) switch (*fmt) {
        case 'i':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(0);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'b':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(INT_MIN);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'f':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(0.0f);
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'F':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(args[i - 1].get_float());
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'S':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_str(dup_ostr(""));
                fakeargs++;
            } else args[i].force_str();
            break;
        case 's':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_cstr("");
                fakeargs++;
            } else args[i].force_str();
            break;
        case 'T':
        case 't':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            }
            break;
        case 'E':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            } else forcecond(args[i]);
            break;
        case 'e':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_code(emptyblock[VAL_NULL] + 1);
                fakeargs++;
            } else forcecode(args[i]);
            break;
        case 'r':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_ident(cstate.dummy);
                fakeargs++;
            } else cstate.force_ident(args[i]);
            break;
        case '$':
            if (++i < numargs) args[i].cleanup();
            args[i].set_ident(id);
            break;
        case 'N':
            if (++i < numargs) args[i].cleanup();
            args[i].set_int(lookup ? -1 : i - fakeargs);
            break;
        case 'C': {
            i = ostd::max(i + 1, numargs);
            ostd::Vector<char> buf;
            ((CommandFunc1)id->fun)(cstate, conc(buf, args, i, true));
            goto cleanup;
        }
        case 'V':
            i = ostd::max(i + 1, numargs);
            ((CommandFuncTv)id->fun)(cstate, args, i);
            goto cleanup;
        case '1':
        case '2':
        case '3':
        case '4':
            if (i + 1 < numargs) {
                fmt -= *fmt - '0' + 1;
                rep = true;
            }
            break;
        }
    ++i;
#define OFFSETARG(n) n
#define ARG(n) (id->argmask&(1<<(n)) ? (void *)args[OFFSETARG(n)].s : (void *)&args[OFFSETARG(n)].i)
#define CALLCOM(n) \
        switch(n) \
        { \
            case 0: ((CommandFunc)id->fun)(cstate); break; \
            case 1: ((CommandFunc1)id->fun)(cstate, ARG(0)); break; \
            case 2: ((CommandFunc2)id->fun)(cstate, ARG(0), ARG(1)); break; \
            case 3: ((CommandFunc3)id->fun)(cstate, ARG(0), ARG(1), ARG(2)); break; \
            case 4: ((CommandFunc4)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3)); break; \
            case 5: ((CommandFunc5)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)); break; \
            case 6: ((CommandFunc6)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5)); break; \
            case 7: ((CommandFunc7)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)); break; \
            case 8: ((CommandFunc8)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7)); break; \
            case 9: ((CommandFunc9)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)); break; \
            case 10: ((CommandFunc10)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9)); break; \
            case 11: ((CommandFunc11)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), ARG(10)); break; \
            case 12: ((CommandFunc12)id->fun)(cstate, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), ARG(10), ARG(11)); break; \
        }
    CALLCOM(i)
#undef OFFSETARG
cleanup:
    for (ostd::Size k = 0; k < ostd::Size(i); ++k) args[k].cleanup();
    for (; i < numargs; i++) args[i].cleanup();
}

#define MAXRUNDEPTH 255
static int rundepth = 0;

static const ostd::uint *runcode(const ostd::uint *code, TaggedValue &result) {
    result.set_null();
    if (rundepth >= MAXRUNDEPTH) {
        debugcode("exceeded recursion limit");
        return skipcode(code, result);
    }
    ++rundepth;
    int numargs = 0;
    TaggedValue args[MAX_ARGUMENTS + MAX_RESULTS], *prevret = cstate.result;
    cstate.result = &result;
    for (;;) {
        ostd::uint op = *code++;
        switch (op & 0xFF) {
        case CODE_START:
        case CODE_OFFSET:
            continue;

#define RETOP(op, val) \
                case op: \
                    result.cleanup(); \
                    val; \
                    continue;

            RETOP(CODE_NULL | RET_NULL, result.set_null())
            RETOP(CODE_NULL | RET_STR, result.set_str(dup_ostr("")))
            RETOP(CODE_NULL | RET_INT, result.set_int(0))
            RETOP(CODE_NULL | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_FALSE | RET_STR, result.set_str(dup_ostr("0")))
        case CODE_FALSE|RET_NULL:
            RETOP(CODE_FALSE | RET_INT, result.set_int(0))
            RETOP(CODE_FALSE | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_TRUE | RET_STR, result.set_str(dup_ostr("1")))
        case CODE_TRUE|RET_NULL:
            RETOP(CODE_TRUE | RET_INT, result.set_int(1))
            RETOP(CODE_TRUE | RET_FLOAT, result.set_float(1.0f))

#define RETPOP(op, val) \
                RETOP(op, { --numargs; val; args[numargs].cleanup(); })

            RETPOP(CODE_NOT | RET_STR, result.set_str(dup_ostr(getbool(args[numargs]) ? "0" : "1")))
        case CODE_NOT|RET_NULL:
                RETPOP(CODE_NOT | RET_INT, result.set_int(getbool(args[numargs]) ? 0 : 1))
                RETPOP(CODE_NOT | RET_FLOAT, result.set_float(getbool(args[numargs]) ? 0.0f : 1.0f))

            case CODE_POP:
                    args[--numargs].cleanup();
            continue;
        case CODE_ENTER:
            code = runcode(code, args[numargs++]);
            continue;
        case CODE_ENTER_RESULT:
            result.cleanup();
            code = runcode(code, result);
            continue;
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            result.force(op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_EXIT|RET_NULL:
            goto exit;
        case CODE_RESULT_ARG|RET_STR:
        case CODE_RESULT_ARG|RET_INT:
        case CODE_RESULT_ARG|RET_FLOAT:
            result.force(op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_RESULT_ARG|RET_NULL:
            args[numargs++] = result;
            result.set_null();
            continue;
        case CODE_PRINT:
            printvar(cstate.identmap[op >> 8]);
            continue;

        case CODE_LOCAL: {
            result.cleanup();
            int numlocals = op >> 8, offset = numargs - numlocals;
            IdentStack locals[MAX_ARGUMENTS];
            for (int i = 0; i < numlocals; ++i) pushalias(*args[offset + i].id, locals[i]);
            code = runcode(code, result);
            for (int i = offset; i < numargs; i++) popalias(*args[i].id);
            goto exit;
        }

        case CODE_DOARGS|RET_NULL:
        case CODE_DOARGS|RET_STR:
        case CODE_DOARGS|RET_INT:
        case CODE_DOARGS|RET_FLOAT:
            if (aliasstack != &noalias) {
                UNDOARGS
                result.cleanup();
                runcode(args[--numargs].code, result);
                args[numargs].cleanup();
                result.force(op & CODE_RET_MASK);
                REDOARGS
                continue;
            }
        /* fallthrough */
        case CODE_DO|RET_NULL:
        case CODE_DO|RET_STR:
        case CODE_DO|RET_INT:
        case CODE_DO|RET_FLOAT:
            result.cleanup();
            runcode(args[--numargs].code, result);
            args[numargs].cleanup();
            result.force(op & CODE_RET_MASK);
            continue;

        case CODE_JUMP: {
            ostd::uint len = op >> 8;
            code += len;
            continue;
        }
        case CODE_JUMP_TRUE: {
            ostd::uint len = op >> 8;
            if (getbool(args[--numargs])) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_FALSE: {
            ostd::uint len = op >> 8;
            if (!getbool(args[--numargs])) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_RESULT_TRUE: {
            ostd::uint len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].type == VAL_CODE) {
                runcode(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (getbool(result)) code += len;
            continue;
        }
        case CODE_JUMP_RESULT_FALSE: {
            ostd::uint len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].type == VAL_CODE) {
                runcode(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (!getbool(result)) code += len;
            continue;
        }

        case CODE_MACRO: {
            ostd::uint len = op >> 8;
            args[numargs++].set_macro(code);
            code += len / sizeof(ostd::uint) + 1;
            continue;
        }

        case CODE_VAL|RET_STR: {
            ostd::uint len = op >> 8;
            args[numargs++].set_str(dup_ostr(ostd::ConstCharRange((const char *)code, len)));
            code += len / sizeof(ostd::uint) + 1;
            continue;
        }
        case CODE_VALI|RET_STR: {
            char s[4] = { char((op >> 8) & 0xFF), char((op >> 16) & 0xFF), char((op >> 24) & 0xFF), '\0' };
            args[numargs++].set_str(dup_ostr(s));
            continue;
        }
        case CODE_VAL|RET_NULL:
        case CODE_VALI|RET_NULL:
            args[numargs++].set_null();
            continue;
        case CODE_VAL|RET_INT:
            args[numargs++].set_int(int(*code++));
            continue;
        case CODE_VALI|RET_INT:
            args[numargs++].set_int(int(op) >> 8);
            continue;
        case CODE_VAL|RET_FLOAT:
            args[numargs++].set_float(*(const float *)code++);
            continue;
        case CODE_VALI|RET_FLOAT:
            args[numargs++].set_float(float(int(op) >> 8));
            continue;

        case CODE_DUP|RET_NULL:
            args[numargs - 1].get_val(args[numargs]);
            numargs++;
            continue;
        case CODE_DUP|RET_INT:
            args[numargs].set_int(args[numargs - 1].get_int());
            numargs++;
            continue;
        case CODE_DUP|RET_FLOAT:
            args[numargs].set_float(args[numargs - 1].get_float());
            numargs++;
            continue;
        case CODE_DUP|RET_STR:
            args[numargs].set_str(dup_ostr(args[numargs - 1].get_str()));
            numargs++;
            continue;

        case CODE_FORCE|RET_STR:
            args[numargs - 1].force_str();
            continue;
        case CODE_FORCE|RET_INT:
            args[numargs - 1].force_int();
            continue;
        case CODE_FORCE|RET_FLOAT:
            args[numargs - 1].force_float();
            continue;

        case CODE_RESULT|RET_NULL:
            result.cleanup();
            result = args[--numargs];
            continue;
        case CODE_RESULT|RET_STR:
        case CODE_RESULT|RET_INT:
        case CODE_RESULT|RET_FLOAT:
            result.cleanup();
            result = args[--numargs];
            result.force(op & CODE_RET_MASK);
            continue;

        case CODE_EMPTY|RET_NULL:
            args[numargs++].set_code(emptyblock[VAL_NULL] + 1);
            break;
        case CODE_EMPTY|RET_STR:
            args[numargs++].set_code(emptyblock[VAL_STR] + 1);
            break;
        case CODE_EMPTY|RET_INT:
            args[numargs++].set_code(emptyblock[VAL_INT] + 1);
            break;
        case CODE_EMPTY|RET_FLOAT:
            args[numargs++].set_code(emptyblock[VAL_FLOAT] + 1);
            break;
        case CODE_BLOCK: {
            ostd::uint len = op >> 8;
            args[numargs++].set_code(code + 1);
            code += len;
            continue;
        }
        case CODE_COMPILE: {
            TaggedValue &arg = args[numargs - 1];
            ostd::Vector<ostd::uint> buf;
            switch (arg.type) {
            case VAL_INT:
                buf.reserve(8);
                buf.push(CODE_START);
                compileint(buf, arg.i);
                buf.push(CODE_RESULT);
                buf.push(CODE_EXIT);
                break;
            case VAL_FLOAT:
                buf.reserve(8);
                buf.push(CODE_START);
                compilefloat(buf, arg.f);
                buf.push(CODE_RESULT);
                buf.push(CODE_EXIT);
                break;
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                buf.reserve(64);
                compilemain(buf, arg.s);
                arg.cleanup();
                break;
            default:
                buf.reserve(8);
                buf.push(CODE_START);
                compilenull(buf);
                buf.push(CODE_RESULT);
                buf.push(CODE_EXIT);
                break;
            }
            arg.set_code(buf.disown() + 1);
            continue;
        }
        case CODE_COND: {
            TaggedValue &arg = args[numargs - 1];
            switch (arg.type) {
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                if (arg.s[0]) {
                    ostd::Vector<ostd::uint> buf;
                    buf.reserve(64);
                    compilemain(buf, arg.s);
                    arg.cleanup();
                    arg.set_code(buf.disown() + 1);
                } else arg.force_null();
                break;
            }
            continue;
        }

        case CODE_IDENT:
            args[numargs++].set_ident(cstate.identmap[op >> 8]);
            continue;
        case CODE_IDENTARG: {
            Ident *id = cstate.identmap[op >> 8];
            if (!(aliasstack->usedargs & (1 << id->index))) {
                pusharg(*id, null_value, aliasstack->argstack[id->index]);
                aliasstack->usedargs |= 1 << id->index;
            }
            args[numargs++].set_ident(id);
            continue;
        }
        case CODE_IDENTU: {
            TaggedValue &arg = args[numargs - 1];
            Ident *id = arg.type == VAL_STR || arg.type == VAL_MACRO || arg.type == VAL_CSTR ? cstate.new_ident(arg.cstr, IDF_UNKNOWN) : cstate.dummy;
            if (id->index < MAX_ARGUMENTS && !(aliasstack->usedargs & (1 << id->index))) {
                pusharg(*id, null_value, aliasstack->argstack[id->index]);
                aliasstack->usedargs |= 1 << id->index;
            }
            arg.cleanup();
            arg.set_ident(id);
            continue;
        }

        case CODE_LOOKUPU|RET_STR:
#define LOOKUPU(aval, sval, ival, fval, nval) { \
                    TaggedValue &arg = args[numargs-1]; \
                    if(arg.type != VAL_STR && arg.type != VAL_MACRO && arg.type != VAL_CSTR) continue; \
                    Ident *id = cstate.idents.at(arg.s); \
                    if(id) switch(id->type) \
                    { \
                        case ID_ALIAS: \
                            if(id->flags&IDF_UNKNOWN) break; \
                            arg.cleanup(); \
                            if(id->index < MAX_ARGUMENTS && !(aliasstack->usedargs&(1<<id->index))) { nval; continue; } \
                            aval; \
                            continue; \
                        case ID_SVAR: arg.cleanup(); sval; continue; \
                        case ID_VAR: arg.cleanup(); ival; continue; \
                        case ID_FVAR: arg.cleanup(); fval; continue; \
                        case ID_COMMAND: \
                        { \
                            arg.cleanup(); \
                            arg.set_null(); \
                            cstate.result = &arg; \
                            TaggedValue buf[MAX_ARGUMENTS]; \
                            callcommand(id, buf, 0, true); \
                            arg.force(op&CODE_RET_MASK); \
                            cstate.result = &result; \
                            continue; \
                        } \
                        default: arg.cleanup(); nval; continue; \
                    } \
                    debugcode("unknown alias lookup: %s", arg.s); \
                    arg.cleanup(); \
                    nval; \
                    continue; \
                }
            LOOKUPU(arg.set_str(dup_ostr(id->get_str())),
                    arg.set_str(dup_ostr(*id->storage.s)),
                    arg.set_str(dup_ostr(intstr(*id->storage.i))),
                    arg.set_str(dup_ostr(floatstr(*id->storage.f))),
                    arg.set_str(dup_ostr("")));
        case CODE_LOOKUP|RET_STR:
#define LOOKUP(aval) { \
                    Ident *id = cstate.identmap[op>>8]; \
                    if(id->flags&IDF_UNKNOWN) debugcode("unknown alias lookup: %s", id->name.data()); \
                    aval; \
                    continue; \
                }
            LOOKUP(args[numargs++].set_str(dup_ostr(id->get_str())));
        case CODE_LOOKUPARG|RET_STR:
#define LOOKUPARG(aval, nval) { \
                    Ident *id = cstate.identmap[op>>8]; \
                    if(!(aliasstack->usedargs&(1<<id->index))) { nval; continue; } \
                    aval; \
                    continue; \
                }
            LOOKUPARG(args[numargs++].set_str(dup_ostr(id->get_str())), args[numargs++].set_str(dup_ostr("")));
        case CODE_LOOKUPU|RET_INT:
            LOOKUPU(arg.set_int(id->get_int()),
                    arg.set_int(parseint(*id->storage.s)),
                    arg.set_int(*id->storage.i),
                    arg.set_int(int(*id->storage.f)),
                    arg.set_int(0));
        case CODE_LOOKUP|RET_INT:
            LOOKUP(args[numargs++].set_int(id->get_int()));
        case CODE_LOOKUPARG|RET_INT:
            LOOKUPARG(args[numargs++].set_int(id->get_int()), args[numargs++].set_int(0));
        case CODE_LOOKUPU|RET_FLOAT:
            LOOKUPU(arg.set_float(id->get_float()),
                    arg.set_float(parsefloat(*id->storage.s)),
                    arg.set_float(float(*id->storage.i)),
                    arg.set_float(*id->storage.f),
                    arg.set_float(0.0f));
        case CODE_LOOKUP|RET_FLOAT:
            LOOKUP(args[numargs++].set_float(id->get_float()));
        case CODE_LOOKUPARG|RET_FLOAT:
            LOOKUPARG(args[numargs++].set_float(id->get_float()), args[numargs++].set_float(0.0f));
        case CODE_LOOKUPU|RET_NULL:
            LOOKUPU(id->get_val(arg),
                    arg.set_str(dup_ostr(*id->storage.s)),
                    arg.set_int(*id->storage.i),
                    arg.set_float(*id->storage.f),
                    arg.set_null());
        case CODE_LOOKUP|RET_NULL:
            LOOKUP(id->get_val(args[numargs++]));
        case CODE_LOOKUPARG|RET_NULL:
            LOOKUPARG(id->get_val(args[numargs++]), args[numargs++].set_null());

        case CODE_LOOKUPMU|RET_STR:
            LOOKUPU(id->getcstr(arg),
                    arg.set_cstr(*id->storage.s),
                    arg.set_str(dup_ostr(intstr(*id->storage.i))),
                    arg.set_str(dup_ostr(floatstr(*id->storage.f))),
                    arg.set_cstr(""));
        case CODE_LOOKUPM|RET_STR:
            LOOKUP(id->getcstr(args[numargs++]));
        case CODE_LOOKUPMARG|RET_STR:
            LOOKUPARG(id->getcstr(args[numargs++]), args[numargs++].set_cstr(""));
        case CODE_LOOKUPMU|RET_NULL:
            LOOKUPU(id->getcval(arg),
                    arg.set_cstr(*id->storage.s),
                    arg.set_int(*id->storage.i),
                    arg.set_float(*id->storage.f),
                    arg.set_null());
        case CODE_LOOKUPM|RET_NULL:
            LOOKUP(id->getcval(args[numargs++]));
        case CODE_LOOKUPMARG|RET_NULL:
            LOOKUPARG(id->getcval(args[numargs++]), args[numargs++].set_null());

        case CODE_SVAR|RET_STR:
        case CODE_SVAR|RET_NULL:
            args[numargs++].set_str(dup_ostr(*cstate.identmap[op >> 8]->storage.s));
            continue;
        case CODE_SVAR|RET_INT:
            args[numargs++].set_int(parseint(*cstate.identmap[op >> 8]->storage.s));
            continue;
        case CODE_SVAR|RET_FLOAT:
            args[numargs++].set_float(parsefloat(*cstate.identmap[op >> 8]->storage.s));
            continue;
        case CODE_SVARM:
            args[numargs++].set_cstr(*cstate.identmap[op >> 8]->storage.s);
            continue;
        case CODE_SVAR1:
            setsvarchecked(cstate.identmap[op >> 8], args[--numargs].s);
            args[numargs].cleanup();
            continue;

        case CODE_IVAR|RET_INT:
        case CODE_IVAR|RET_NULL:
            args[numargs++].set_int(*cstate.identmap[op >> 8]->storage.i);
            continue;
        case CODE_IVAR|RET_STR:
            args[numargs++].set_str(dup_ostr(intstr(*cstate.identmap[op >> 8]->storage.i)));
            continue;
        case CODE_IVAR|RET_FLOAT:
            args[numargs++].set_float(float(*cstate.identmap[op >> 8]->storage.i));
            continue;
        case CODE_IVAR1:
            setvarchecked(cstate.identmap[op >> 8], args[--numargs].i);
            continue;
        case CODE_IVAR2:
            numargs -= 2;
            setvarchecked(cstate.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8));
            continue;
        case CODE_IVAR3:
            numargs -= 3;
            setvarchecked(cstate.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8) | args[numargs + 2].i);
            continue;

        case CODE_FVAR|RET_FLOAT:
        case CODE_FVAR|RET_NULL:
            args[numargs++].set_float(*cstate.identmap[op >> 8]->storage.f);
            continue;
        case CODE_FVAR|RET_STR:
            args[numargs++].set_str(dup_ostr(floatstr(*cstate.identmap[op >> 8]->storage.f)));
            continue;
        case CODE_FVAR|RET_INT:
            args[numargs++].set_int(int(*cstate.identmap[op >> 8]->storage.f));
            continue;
        case CODE_FVAR1:
            setfvarchecked(cstate.identmap[op >> 8], args[--numargs].f);
            continue;

#define OFFSETARG(n) offset+n
        case CODE_COM|RET_NULL:
        case CODE_COM|RET_STR:
        case CODE_COM|RET_FLOAT:
        case CODE_COM|RET_INT: {
            Ident *id = cstate.identmap[op >> 8];
            int offset = numargs - id->numargs;
            result.force_null();
            CALLCOM(id->numargs)
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
            }
#undef OFFSETARG

        case CODE_COMV|RET_NULL:
        case CODE_COMV|RET_STR:
        case CODE_COMV|RET_FLOAT:
        case CODE_COMV|RET_INT: {
            Ident *id = cstate.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            ((CommandFuncTv)id->fun)(cstate, &args[offset], callargs);
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }
        case CODE_COMC|RET_NULL:
        case CODE_COMC|RET_STR:
        case CODE_COMC|RET_FLOAT:
        case CODE_COMC|RET_INT: {
            Ident *id = cstate.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            {
                ostd::Vector<char> buf;
                buf.reserve(256);
                ((CommandFunc1)id->fun)(cstate, conc(buf, &args[offset], callargs, true));
            }
            result.force(op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }

        case CODE_CONC|RET_NULL:
        case CODE_CONC|RET_STR:
        case CODE_CONC|RET_FLOAT:
        case CODE_CONC|RET_INT:
        case CODE_CONCW|RET_NULL:
        case CODE_CONCW|RET_STR:
        case CODE_CONCW|RET_FLOAT:
        case CODE_CONCW|RET_INT: {
            int numconc = op >> 8;
            char *s = conc(&args[numargs - numconc], numconc, (op & CODE_OP_MASK) == CODE_CONC);
            free_args(args, numargs, numargs - numconc);
            args[numargs].set_str(s);
            args[numargs].force(op & CODE_RET_MASK);
            numargs++;
            continue;
        }

        case CODE_CONCM|RET_NULL:
        case CODE_CONCM|RET_STR:
        case CODE_CONCM|RET_FLOAT:
        case CODE_CONCM|RET_INT: {
            int numconc = op >> 8;
            char *s = conc(&args[numargs - numconc], numconc, false);
            free_args(args, numargs, numargs - numconc);
            result.set_str(s);
            result.force(op & CODE_RET_MASK);
            continue;
        }

        case CODE_ALIAS:
            setalias(*cstate.identmap[op >> 8], args[--numargs]);
            continue;
        case CODE_ALIASARG:
            setarg(*cstate.identmap[op >> 8], args[--numargs]);
            continue;
        case CODE_ALIASU:
            numargs -= 2;
            setalias(args[numargs].get_str(), args[numargs + 1]);
            args[numargs].cleanup();
            continue;

#define SKIPARGS(offset) offset
        case CODE_CALL|RET_NULL:
        case CODE_CALL|RET_STR:
        case CODE_CALL|RET_FLOAT:
        case CODE_CALL|RET_INT: {
#define FORCERESULT { \
                free_args(args, numargs, SKIPARGS(offset)); \
                result.force(op&CODE_RET_MASK); \
                continue; \
            }
#define CALLALIAS { \
                IdentStack argstack[MAX_ARGUMENTS]; \
                for(int i = 0; i < callargs; i++) \
                    pusharg(*cstate.identmap[i], args[offset + i], argstack[i]); \
                int oldargs = _numargs; \
                _numargs = callargs; \
                int oldflags = cstate.identflags; \
                cstate.identflags |= id->flags&IDF_OVERRIDDEN; \
                IdentLink aliaslink = { id, aliasstack, (1<<callargs)-1, argstack }; \
                aliasstack = &aliaslink; \
                if(!id->code) id->code = compilecode(id->get_str()); \
                ostd::uint *code = id->code; \
                code[0] += 0x100; \
                runcode(code+1, result); \
                code[0] -= 0x100; \
                if(int(code[0]) < 0x100) delete[] code; \
                aliasstack = aliaslink.next; \
                cstate.identflags = oldflags; \
                for(int i = 0; i < callargs; i++) \
                    poparg(*cstate.identmap[i]); \
                for(int argmask = aliaslink.usedargs&(~0<<callargs), i = callargs; argmask; i++) \
                    if(argmask&(1<<i)) { poparg(*cstate.identmap[i]); argmask &= ~(1<<i); } \
                result.force(op&CODE_RET_MASK); \
                _numargs = oldargs; \
                numargs = SKIPARGS(offset); \
            }
            result.force_null();
            Ident *id = cstate.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (id->flags & IDF_UNKNOWN) {
                debugcode("unknown command: %s", id->name.data());
                FORCERESULT;
            }
            CALLALIAS;
            continue;
        }
        case CODE_CALLARG|RET_NULL:
        case CODE_CALLARG|RET_STR:
        case CODE_CALLARG|RET_FLOAT:
        case CODE_CALLARG|RET_INT: {
            result.force_null();
            Ident *id = cstate.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (!(aliasstack->usedargs & (1 << id->index))) FORCERESULT;
            CALLALIAS;
            continue;
        }
#undef SKIPARGS

#define SKIPARGS(offset) offset-1
        case CODE_CALLU|RET_NULL:
        case CODE_CALLU|RET_STR:
        case CODE_CALLU|RET_FLOAT:
        case CODE_CALLU|RET_INT: {
            int callargs = op >> 8, offset = numargs - callargs;
            TaggedValue &idarg = args[offset - 1];
            if (idarg.type != VAL_STR && idarg.type != VAL_MACRO && idarg.type != VAL_CSTR) {
litval:
                result.cleanup();
                result = idarg;
                result.force(op & CODE_RET_MASK);
                while (--numargs >= offset) args[numargs].cleanup();
                continue;
            }
            Ident *id = cstate.idents.at(idarg.s);
            if (!id) {
noid:
                if (checknumber(idarg.s)) goto litval;
                debugcode("unknown command: %s", idarg.s);
                result.force_null();
                FORCERESULT;
            }
            result.force_null();
            switch (id->type) {
            default:
                if (!id->fun) FORCERESULT;
            /* fallthrough */
            case ID_COMMAND:
                idarg.cleanup();
                callcommand(id, &args[offset], callargs);
                result.force(op & CODE_RET_MASK);
                numargs = offset - 1;
                continue;
            case ID_LOCAL: {
                IdentStack locals[MAX_ARGUMENTS];
                idarg.cleanup();
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) pushalias(*cstate.force_ident(args[offset + j]), locals[j]);
                code = runcode(code, result);
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) popalias(*args[offset + j].id);
                goto exit;
            }
            case ID_VAR:
                if (callargs <= 0) printvar(id);
                else setvarchecked(id, &args[offset], callargs);
                FORCERESULT;
            case ID_FVAR:
                if (callargs <= 0) printvar(id);
                else setfvarchecked(id, args[offset].force_float());
                FORCERESULT;
            case ID_SVAR:
                if (callargs <= 0) printvar(id);
                else setsvarchecked(id, args[offset].force_str());
                FORCERESULT;
            case ID_ALIAS:
                if (id->index < MAX_ARGUMENTS && !(aliasstack->usedargs & (1 << id->index))) FORCERESULT;
                if (id->valtype == VAL_NULL) goto noid;
                idarg.cleanup();
                CALLALIAS;
                continue;
            }
        }
#undef SKIPARGS
        }
    }
exit:
    cstate.result = prevret;
    --rundepth;
    return code;
}

void executeret(const ostd::uint *code, TaggedValue &result) {
    runcode(code, result);
}

void executeret(const char *p, TaggedValue &result) {
    ostd::Vector<ostd::uint> code;
    code.reserve(64);
    compilemain(code, p, VAL_ANY);
    runcode(code.data() + 1, result);
    if (int(code[0]) >= 0x100) code.disown();
}

void executeret(Ident *id, TaggedValue *args, int numargs, TaggedValue &result) {
    result.set_null();
    ++rundepth;
    TaggedValue *prevret = cstate.result;
    cstate.result = &result;
    if (rundepth > MAXRUNDEPTH) debugcode("exceeded recursion limit");
    else if (id) switch (id->type) {
        default:
            if (!id->fun) break;
        /* fallthrough */
        case ID_COMMAND:
            if (numargs < id->numargs) {
                TaggedValue buf[MAX_ARGUMENTS];
                memcpy(buf, args, numargs * sizeof(TaggedValue));
                callcommand(id, buf, numargs, false);
            } else callcommand(id, args, numargs, false);
            numargs = 0;
            break;
        case ID_VAR:
            if (numargs <= 0) printvar(id);
            else setvarchecked(id, args, numargs);
            break;
        case ID_FVAR:
            if (numargs <= 0) printvar(id);
            else setfvarchecked(id, args[0].force_float());
            break;
        case ID_SVAR:
            if (numargs <= 0) printvar(id);
            else setsvarchecked(id, args[0].force_str());
            break;
        case ID_ALIAS:
            if (id->index < MAX_ARGUMENTS && !(aliasstack->usedargs & (1 << id->index))) break;
            if (id->valtype == VAL_NULL) break;
#define callargs numargs
#define offset 0
#define op RET_NULL
#define SKIPARGS(offset) offset
            CALLALIAS;
#undef callargs
#undef offset
#undef op
#undef SKIPARGS
            break;
        }
    free_args(args, numargs, 0);
    cstate.result = prevret;
    --rundepth;
}

ostd::String CsState::run_str(const ostd::uint *code) {
    TaggedValue result;
    runcode(code, result);
    if (result.type == VAL_NULL) return ostd::String();
    result.force_str();
    ostd::String ret(result.s);
    delete[] result.s;
    return ret;
}

ostd::String CsState::run_str(ostd::ConstCharRange code) {
    TaggedValue result;
    /* FIXME range */
    executeret(code.data(), result);
    if (result.type == VAL_NULL) return ostd::String();
    result.force_str();
    ostd::String ret(result.s);
    delete[] result.s;
    return ret;
}

char *executestr(Ident *id, TaggedValue *args, int numargs) {
    TaggedValue result;
    executeret(id, args, numargs, result);
    if (result.type == VAL_NULL) return nullptr;
    result.force_str();
    return result.s;
}

int execute(const ostd::uint *code) {
    TaggedValue result;
    runcode(code, result);
    int i = result.get_int();
    result.cleanup();
    return i;
}

int execute(const char *p) {
    ostd::Vector<ostd::uint> code;
    code.reserve(64);
    compilemain(code, p, VAL_INT);
    TaggedValue result;
    runcode(code.data() + 1, result);
    if (int(code[0]) >= 0x100) code.disown();
    int i = result.get_int();
    result.cleanup();
    return i;
}

int execute(Ident *id, TaggedValue *args, int numargs) {
    TaggedValue result;
    executeret(id, args, numargs, result);
    int i = result.get_int();
    result.cleanup();
    return i;
}

float executefloat(const ostd::uint *code) {
    TaggedValue result;
    runcode(code, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

float executefloat(const char *p) {
    TaggedValue result;
    executeret(p, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

float executefloat(Ident *id, TaggedValue *args, int numargs) {
    TaggedValue result;
    executeret(id, args, numargs, result);
    float f = result.get_float();
    result.cleanup();
    return f;
}

bool executebool(const ostd::uint *code) {
    TaggedValue result;
    runcode(code, result);
    bool b = getbool(result);
    result.cleanup();
    return b;
}

bool executebool(const char *p) {
    TaggedValue result;
    executeret(p, result);
    bool b = getbool(result);
    result.cleanup();
    return b;
}

bool executebool(Ident *id, TaggedValue *args, int numargs) {
    TaggedValue result;
    executeret(id, args, numargs, result);
    bool b = getbool(result);
    result.cleanup();
    return b;
}

bool execfile(const char *cfgfile, bool msg) {
    const char *oldsourcefile = sourcefile, *oldsourcestr = sourcestr;
    char *buf = nullptr;
    ostd::Size len;

    ostd::FileStream f(cfgfile, ostd::StreamMode::read);
    if (!f.is_open()) goto error;

    len = f.size();
    buf = new char[len + 1];
    if (f.get(buf, len) != len) {
        delete[] buf;
        goto error;
    }
    buf[len] = '\0';

    sourcefile = cfgfile;
    sourcestr = buf;
    execute(buf);
    sourcefile = oldsourcefile;
    sourcestr = oldsourcestr;
    delete[] buf;
    return true;

error:
    if (msg) fprintf(stderr, "could not read file \"%s\"\n", cfgfile);
    return false;
}
ICOMMAND(exec, "sb", (CsState &cs, char *file, int *msg), cs.result->set_int(execfile(file, *msg != 0) ? 1 : 0));

const char *escapestring(const char *s) {
    stridx = (stridx + 1) % 4;
    ostd::Vector<char> &buf = strbuf[stridx];
    buf.clear();
    buf.push('"');
    for (; *s; s++) switch (*s) {
        case '\n':
            buf.push('^');
            buf.push('n');
            break;
        case '\t':
            buf.push('^');
            buf.push('t');
            break;
        case '\f':
            buf.push('^');
            buf.push('f');
            break;
        case '"':
            buf.push('^');
            buf.push('\"');
            break;
        case '^':
            buf.push('^');
            buf.push('^');
            break;
        default:
            buf.push(*s);
            break;
        }
    buf.push('"');
    buf.push('\0');
    return buf.data();
}

ICOMMAND(escape, "s", (CsState &, char *s), result(escapestring(s)));
ICOMMAND(unescape, "s", (CsState &, char *s), {
    int len = strlen(s);
    char *d = new char[len + 1];
    unescapestring(d, s, &s[len]);
    stringret(d);
});

const char *escapeid(const char *s) {
    const char *end = s + strcspn(s, "\"/;()[]@ \f\t\r\n\0");
    return *end ? escapestring(s) : s;
}

bool validateblock(const char *s) {
    const int maxbrak = 100;
    static char brakstack[maxbrak];
    int brakdepth = 0;
    for (; *s; s++) switch (*s) {
        case '[':
        case '(':
            if (brakdepth >= maxbrak) return false;
            brakstack[brakdepth++] = *s;
            break;
        case ']':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '[') return false;
            break;
        case ')':
            if (brakdepth <= 0 || brakstack[--brakdepth] != '(') return false;
            break;
        case '"':
            s = parsestring(s + 1);
            if (*s != '"') return false;
            break;
        case '/':
            if (s[1] == '/') return false;
            break;
        case '@':
        case '\f':
            return false;
        }
    return brakdepth == 0;
}

/* standard lib */

static char retbuf[4][256];
static int retidx = 0;

const char *intstr(int v) {
    retidx = (retidx + 1) % 4;
    intformat(retbuf[retidx], v);
    return retbuf[retidx];
}

const char *floatstr(float v) {
    retidx = (retidx + 1) % 4;
    floatformat(retbuf[retidx], v);
    return retbuf[retidx];
}

#undef ICOMMANDNAME
#define ICOMMANDNAME(name) _stdcmd
#undef ICOMMANDSNAME
#define ICOMMANDSNAME _stdcmd

ICOMMANDK(do, ID_DO, "e", (CsState &cs, ostd::uint *body), executeret(body, *cs.result));

static void doargs(CsState &cs, ostd::uint *body) {
    if (aliasstack != &noalias) {
        UNDOARGS
        executeret(body, *cs.result);
        REDOARGS
    } else executeret(body, *cs.result);
}
COMMANDK(doargs, ID_DOARGS, "e");

ICOMMANDK(if, ID_IF, "tee", (CsState &cs, TaggedValue *cond, ostd::uint *t, ostd::uint *f), executeret(getbool(*cond) ? t : f, *cs.result));
ICOMMAND( ?, "tTT", (CsState &, TaggedValue *cond, TaggedValue *t, TaggedValue *f), result(*(getbool(*cond) ? t : f)));

ICOMMAND(pushif, "rTe", (CsState &cs, Ident *id, TaggedValue *v, ostd::uint *code), {
    if (id->type != ID_ALIAS || id->index < MAX_ARGUMENTS) return;
    if (getbool(*v)) {
        IdentStack stack;
        pusharg(*id, *v, stack);
        v->type = VAL_NULL;
        id->flags &= ~IDF_UNKNOWN;
        executeret(code, *cs.result);
        poparg(*id);
    }
});

void loopiter(Ident *id, IdentStack &stack, const TaggedValue &v) {
    if (id->stack != &stack) {
        pusharg(*id, v, stack);
        id->flags &= ~IDF_UNKNOWN;
    } else {
        if (id->valtype == VAL_STR) delete[] id->val.s;
        id->clean_code();
        id->setval(v);
    }
}

void loopend(Ident *id, IdentStack &stack) {
    if (id->stack == &stack) poparg(*id);
}

static inline void setiter(Ident &id, int i, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.valtype != VAL_INT) {
            if (id.valtype == VAL_STR) delete[] id.val.s;
            id.clean_code();
            id.valtype = VAL_INT;
        }
        id.val.i = i;
    } else {
        TaggedValue t;
        t.set_int(i);
        pusharg(id, t, stack);
        id.flags &= ~IDF_UNKNOWN;
    }
}

static inline void doloop(Ident &id, int offset, int n, int step, ostd::uint *body) {
    if (n <= 0 || id.type != ID_ALIAS) return;
    IdentStack stack;
    for (int i = 0; i < n; ++i) {
        setiter(id, offset + i * step, stack);
        execute(body);
    }
    poparg(id);
}
ICOMMAND(loop, "rie", (CsState &, Ident *id, int *n, ostd::uint *body), doloop(*id, 0, *n, 1, body));
ICOMMAND(loop+, "riie", (CsState &, Ident *id, int *offset, int *n, ostd::uint *body), doloop(*id, *offset, *n, 1, body));
ICOMMAND(loop*, "riie", (CsState &, Ident *id, int *step, int *n, ostd::uint *body), doloop(*id, 0, *n, *step, body));
ICOMMAND(loop+*, "riiie", (CsState &, Ident *id, int *offset, int *step, int *n, ostd::uint *body), doloop(*id, *offset, *n, *step, body));

static inline void loopwhile(Ident &id, int offset, int n, int step, ostd::uint *cond, ostd::uint *body) {
    if (n <= 0 || id.type != ID_ALIAS) return;
    IdentStack stack;
    for (int i = 0; i < n; ++i) {
        setiter(id, offset + i * step, stack);
        if (!executebool(cond)) break;
        execute(body);
    }
    poparg(id);
}
ICOMMAND(loopwhile, "riee", (CsState &, Ident *id, int *n, ostd::uint *cond, ostd::uint *body), loopwhile(*id, 0, *n, 1, cond, body));
ICOMMAND(loopwhile+, "riiee", (CsState &, Ident *id, int *offset, int *n, ostd::uint *cond, ostd::uint *body), loopwhile(*id, *offset, *n, 1, cond, body));
ICOMMAND(loopwhile*, "riiee", (CsState &, Ident *id, int *step, int *n, ostd::uint *cond, ostd::uint *body), loopwhile(*id, 0, *n, *step, cond, body));
ICOMMAND(loopwhile+*, "riiiee", (CsState &, Ident *id, int *offset, int *step, int *n, ostd::uint *cond, ostd::uint *body), loopwhile(*id, *offset, *n, *step, cond, body));

ICOMMAND(while, "ee", (CsState &, ostd::uint *cond, ostd::uint *body), while (executebool(cond)) execute(body));

static inline void loopconc(Ident &id, int offset, int n, int step, ostd::uint *body, bool space) {
    if (n <= 0 || id.type != ID_ALIAS) return;
    IdentStack stack;
    ostd::Vector<char> s;
    for (int i = 0; i < n; ++i) {
        setiter(id, offset + i * step, stack);
        TaggedValue v;
        executeret(body, v);
        const char *vstr = v.get_str();
        int len = strlen(vstr);
        if (space && i) s.push(' ');
        s.push_n(vstr, len);
        v.cleanup();
    }
    if (n > 0) poparg(id);
    s.push('\0');
    cstate.result->set_str(s.disown());
}
ICOMMAND(loopconcat, "rie", (CsState &, Ident *id, int *n, ostd::uint *body), loopconc(*id, 0, *n, 1, body, true));
ICOMMAND(loopconcat+, "riie", (CsState &, Ident *id, int *offset, int *n, ostd::uint *body), loopconc(*id, *offset, *n, 1, body, true));
ICOMMAND(loopconcat*, "riie", (CsState &, Ident *id, int *step, int *n, ostd::uint *body), loopconc(*id, 0, *n, *step, body, true));
ICOMMAND(loopconcat+*, "riiie", (CsState &, Ident *id, int *offset, int *step, int *n, ostd::uint *body), loopconc(*id, *offset, *n, *step, body, true));
ICOMMAND(loopconcatword, "rie", (CsState &, Ident *id, int *n, ostd::uint *body), loopconc(*id, 0, *n, 1, body, false));
ICOMMAND(loopconcatword+, "riie", (CsState &, Ident *id, int *offset, int *n, ostd::uint *body), loopconc(*id, *offset, *n, 1, body, false));
ICOMMAND(loopconcatword*, "riie", (CsState &, Ident *id, int *step, int *n, ostd::uint *body), loopconc(*id, 0, *n, *step, body, false));
ICOMMAND(loopconcatword+*, "riiie", (CsState &, Ident *id, int *offset, int *step, int *n, ostd::uint *body), loopconc(*id, *offset, *n, *step, body, false));

void concat(CsState &cs, TaggedValue *v, int n) {
    cs.result->set_str(conc(v, n, true));
}
COMMAND(concat, "V");

void concatword(CsState &cs, TaggedValue *v, int n) {
    cs.result->set_str(conc(v, n, false));
}
COMMAND(concatword, "V");

void result(TaggedValue &v) {
    *cstate.result = v;
    v.type = VAL_NULL;
}

void stringret(char *s) {
    cstate.result->set_str(s);
}

void result(const char *s) {
    cstate.result->set_str(dup_ostr(s));
}

ICOMMANDK(result, ID_RESULT, "T", (CsState &cs, TaggedValue *v), {
    *cs.result = *v;
    v->type = VAL_NULL;
});

void format(CsState &cs, TaggedValue *args, int numargs) {
    ostd::Vector<char> s;
    const char *f = args[0].get_str();
    while (*f) {
        int c = *f++;
        if (c == '%') {
            int i = *f++;
            if (i >= '1' && i <= '9') {
                i -= '0';
                const char *sub = i < numargs ? args[i].get_str() : "";
                while (*sub) s.push(*sub++);
            } else s.push(i);
        } else s.push(c);
    }
    s.push('\0');
    cs.result->set_str(s.disown());
}
COMMAND(format, "V");

static const char *liststart = nullptr, *listend = nullptr, *listquotestart = nullptr, *listquoteend = nullptr;

static inline void skiplist(const char *&p) {
    for (;;) {
        p += strspn(p, " \t\r\n");
        if (p[0] != '/' || p[1] != '/') break;
        p += strcspn(p, "\n\0");
    }
}

static bool parselist(const char *&s, const char *&start = liststart, const char *&end = listend, const char *&quotestart = listquotestart, const char *&quoteend = listquoteend) {
    skiplist(s);
    switch (*s) {
    case '"':
        quotestart = s++;
        start = s;
        s = parsestring(s);
        end = s;
        if (*s == '"') s++;
        quoteend = s;
        break;
    case '(':
    case '[':
        quotestart = s;
        start = s + 1;
        for (int braktype = *s++, brak = 1;;) {
            s += strcspn(s, "\"/;()[]\0");
            int c = *s++;
            switch (c) {
            case '\0':
                s--;
                quoteend = end = s;
                return true;
            case '"':
                s = parsestring(s);
                if (*s == '"') s++;
                break;
            case '/':
                if (*s == '/') s += strcspn(s, "\n\0");
                break;
            case '(':
            case '[':
                if (c == braktype) brak++;
                break;
            case ')':
                if (braktype == '(' && --brak <= 0) goto endblock;
                break;
            case ']':
                if (braktype == '[' && --brak <= 0) goto endblock;
                break;
            }
        }
endblock:
        end = s - 1;
        quoteend = s;
        break;
    case '\0':
    case ')':
    case ']':
        return false;
    default:
        quotestart = start = s;
        s = parseword(s);
        quoteend = end = s;
        break;
    }
    skiplist(s);
    if (*s == ';') s++;
    return true;
}

static inline char *listelem(const char *start = liststart, const char *end = listend, const char *quotestart = listquotestart) {
    ostd::Size len = end - start;
    char *s = new char[len + 1];
    if (*quotestart == '"') unescapestring(s, start, end);
    else {
        memcpy(s, start, len);
        s[len] = '\0';
    }
    return s;
}

void explodelist(const char *s, ostd::Vector<ostd::String> &elems, int limit) {
    const char *start, *end, *qstart;
    while ((limit < 0 || int(elems.size()) < limit) && parselist(s, start, end, qstart)) {
        char *s = listelem(start, end, qstart);
        elems.push(s);
        delete[] s;
    }
}

char *indexlist(const char *s, int pos) {
    for (int i = 0; i < pos; ++i) if (!parselist(s)) return dup_ostr("");
    const char *start, *end, *qstart;
    return parselist(s, start, end, qstart) ? listelem(start, end, qstart) : dup_ostr("");
}

int listlen(CsState &, const char *s) {
    int n = 0;
    while (parselist(s)) n++;
    return n;
}
ICOMMAND(listlen, "s", (CsState &cs, char *s), cs.result->set_int(listlen(cs, s)));

void at(CsState &cs, TaggedValue *args, int numargs) {
    if (!numargs) return;
    const char *start = args[0].get_str(), *end = start + strlen(start), *qstart = "";
    for (int i = 1; i < numargs; i++) {
        const char *list = start;
        int pos = args[i].get_int();
        for (; pos > 0; pos--) if (!parselist(list)) break;
        if (pos > 0 || !parselist(list, start, end, qstart)) start = end = qstart = "";
    }
    cs.result->set_str(listelem(start, end, qstart));
}
COMMAND(at, "si1V");

void substr(CsState &cs, char *s, int *start, int *count, int *numargs) {
    int len = strlen(s), offset = ostd::clamp(*start, 0, len);
    cs.result->set_str(dup_ostr(ostd::ConstCharRange(&s[offset], *numargs >= 3 ? ostd::clamp(*count, 0, len - offset) : len - offset)));
}
COMMAND(substr, "siiN");

void sublist(CsState &cs, const char *s, int *skip, int *count, int *numargs) {
    int offset = ostd::max(*skip, 0), len = *numargs >= 3 ? ostd::max(*count, 0) : -1;
    for (int i = 0; i < offset; ++i) if (!parselist(s)) break;
    if (len < 0) {
        if (offset > 0) skiplist(s);
        cs.result->set_str(dup_ostr(s));
        return;
    }
    const char *list = s, *start, *end, *qstart, *qend = s;
    if (len > 0 && parselist(s, start, end, list, qend)) while (--len > 0 && parselist(s, start, end, qstart, qend));
    cs.result->set_str(dup_ostr(ostd::ConstCharRange(list, qend - list)));
}
COMMAND(sublist, "siiN");

static inline void setiter(Ident &id, char *val, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.valtype == VAL_STR) delete[] id.val.s;
        else id.valtype = VAL_STR;
        id.clean_code();
        id.val.s = val;
    } else {
        TaggedValue t;
        t.set_str(val);
        pusharg(id, t, stack);
        id.flags &= ~IDF_UNKNOWN;
    }
}

void listfind(CsState &cs, Ident *id, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS) {
        cs.result->set_int(-1);
        return;
    }
    IdentStack stack;
    int n = -1;
    for (const char *s = list, *start, *end; parselist(s, start, end);) {
        ++n;
        setiter(*id, dup_ostr(ostd::ConstCharRange(start, end - start)), stack);
        if (executebool(body)) {
            cs.result->set_int(n);
            goto found;
        }
    }
    cs.result->set_int(-1);
found:
    if (n >= 0) poparg(*id);
}
COMMAND(listfind, "rse");

void listassoc(CsState &, Ident *id, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS) return;
    IdentStack stack;
    int n = -1;
    for (const char *s = list, *start, *end, *qstart; parselist(s, start, end);) {
        ++n;
        setiter(*id, dup_ostr(ostd::ConstCharRange(start, end - start)), stack);
        if (executebool(body)) {
            if (parselist(s, start, end, qstart)) stringret(listelem(start, end, qstart));
            break;
        }
        if (!parselist(s)) break;
    }
    if (n >= 0) poparg(*id);
}
COMMAND(listassoc, "rse");

#define LISTFIND(name, fmt, type, init, cmp) \
    ICOMMAND(name, "s" fmt "i", (CsState &cs, char *list, type *val, int *skip), \
    { \
        int n = 0; \
        init; \
        for(const char *s = list, *start, *end, *qstart; parselist(s, start, end, qstart); n++) \
        { \
            if(cmp) { cs.result->set_int(n); return; } \
            for (int i = 0; i < *skip; ++i) { if(!parselist(s)) goto notfound; n++; } \
        } \
    notfound: \
        cs.result->set_int(-1); \
    });
LISTFIND(listfind=, "i", int, , parseint(start) == *val);
LISTFIND(listfind=f, "f", float, , parsefloat(start) == *val);
LISTFIND(listfind=s, "s", char, int len = (int)strlen(val), int(end - start) == len && !memcmp(start, val, len));

#define LISTASSOC(name, fmt, type, init, cmp) \
    ICOMMAND(name, "s" fmt, (CsState &, char *list, type *val), \
    { \
        init; \
        for(const char *s = list, *start, *end, *qstart; parselist(s, start, end);) \
        { \
            if(cmp) { if(parselist(s, start, end, qstart)) stringret(listelem(start, end, qstart)); return; } \
            if(!parselist(s)) break; \
        } \
    });
LISTASSOC(listassoc=, "i", int, , parseint(start) == *val);
LISTASSOC(listassoc=f, "f", float, , parsefloat(start) == *val);
LISTASSOC(listassoc=s, "s", char, int len = (int)strlen(val), int(end - start) == len && !memcmp(start, val, len));

void looplist(CsState &, Ident *id, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS) return;
    IdentStack stack;
    int n = 0;
    for (const char *s = list, *start, *end, *qstart; parselist(s, start, end, qstart); n++) {
        setiter(*id, listelem(start, end, qstart), stack);
        execute(body);
    }
    if (n) poparg(*id);
}
COMMAND(looplist, "rse");

void looplist2(CsState &, Ident *id, Ident *id2, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS || id2->type != ID_ALIAS) return;
    IdentStack stack, stack2;
    int n = 0;
    for (const char *s = list, *start, *end, *qstart; parselist(s, start, end, qstart); n += 2) {
        setiter(*id, listelem(start, end, qstart), stack);
        setiter(*id2, parselist(s, start, end, qstart) ? listelem(start, end, qstart) : dup_ostr(""), stack2);
        execute(body);
    }
    if (n) {
        poparg(*id);
        poparg(*id2);
    }
}
COMMAND(looplist2, "rrse");

void looplist3(CsState &, Ident *id, Ident *id2, Ident *id3, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS || id2->type != ID_ALIAS || id3->type != ID_ALIAS) return;
    IdentStack stack, stack2, stack3;
    int n = 0;
    for (const char *s = list, *start, *end, *qstart; parselist(s, start, end, qstart); n += 3) {
        setiter(*id, listelem(start, end, qstart), stack);
        setiter(*id2, parselist(s, start, end, qstart) ? listelem(start, end, qstart) : dup_ostr(""), stack2);
        setiter(*id3, parselist(s, start, end, qstart) ? listelem(start, end, qstart) : dup_ostr(""), stack3);
        execute(body);
    }
    if (n) {
        poparg(*id);
        poparg(*id2);
        poparg(*id3);
    }
}
COMMAND(looplist3, "rrrse");

void looplistconc(CsState &cs, Ident *id, const char *list, const ostd::uint *body, bool space) {
    if (id->type != ID_ALIAS) return;
    IdentStack stack;
    ostd::Vector<char> r;
    int n = 0;
    for (const char *s = list, *start, *end, *qstart; parselist(s, start, end, qstart); n++) {
        char *val = listelem(start, end, qstart);
        setiter(*id, val, stack);

        if (n && space) r.push(' ');

        TaggedValue v;
        executeret(body, v);
        const char *vstr = v.get_str();
        int len = strlen(vstr);
        r.push_n(vstr, len);
        v.cleanup();
    }
    if (n) poparg(*id);
    r.push('\0');
    cs.result->set_str(r.disown());
}
ICOMMAND(looplistconcat, "rse", (CsState &cs, Ident *id, char *list, ostd::uint *body), looplistconc(cs, id, list, body, true));
ICOMMAND(looplistconcatword, "rse", (CsState &cs, Ident *id, char *list, ostd::uint *body), looplistconc(cs, id, list, body, false));

void listfilter(CsState &cs, Ident *id, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS) return;
    IdentStack stack;
    ostd::Vector<char> r;
    int n = 0;
    for (const char *s = list, *start, *end, *qstart, *qend; parselist(s, start, end, qstart, qend); n++) {
        char *val = dup_ostr(ostd::ConstCharRange(start, end - start));
        setiter(*id, val, stack);

        if (executebool(body)) {
            if (r.size()) r.push(' ');
            r.push_n(qstart, qend - qstart);
        }
    }
    if (n) poparg(*id);
    r.push('\0');
    cs.result->set_str(r.disown());
}
COMMAND(listfilter, "rse");

void listcount(CsState &cs, Ident *id, const char *list, const ostd::uint *body) {
    if (id->type != ID_ALIAS) return;
    IdentStack stack;
    int n = 0, r = 0;
    for (const char *s = list, *start, *end; parselist(s, start, end); n++) {
        char *val = dup_ostr(ostd::ConstCharRange(start, end - start));
        setiter(*id, val, stack);
        if (executebool(body)) r++;
    }
    if (n) poparg(*id);
    cs.result->set_int(r);
}
COMMAND(listcount, "rse");

void prettylist(CsState &cs, const char *s, const char *conj) {
    ostd::Vector<char> p;
    const char *start, *end, *qstart;
    for (int len = listlen(cs, s), n = 0; parselist(s, start, end, qstart); n++) {
        if (*qstart == '"') {
            p.reserve(p.size() + end - start);
            p.advance(unescapestring(&p[p.size()], start, end));
        } else p.push_n(start, end - start);
        if (n + 1 < len) {
            if (len > 2 || !conj[0]) p.push(',');
            if (n + 2 == len && conj[0]) {
                p.push(' ');
                p.push_n(conj, strlen(conj));
            }
            p.push(' ');
        }
    }
    p.push('\0');
    cs.result->set_str(p.disown());
}
COMMAND(prettylist, "ss");

int listincludes(CsState &, const char *list, const char *needle, int needlelen) {
    int offset = 0;
    for (const char *s = list, *start, *end; parselist(s, start, end);) {
        int len = end - start;
        if (needlelen == len && !strncmp(needle, start, len)) return offset;
        offset++;
    }
    return -1;
}
ICOMMAND(indexof, "ss", (CsState &cs, char *list, char *elem), cs.result->set_int(listincludes(cs, list, elem, strlen(elem))));

#define LISTMERGECMD(name, init, iter, filter, dir) \
    ICOMMAND(name, "ss", (CsState &cs, const char *list, const char *elems), \
    { \
        ostd::Vector<char> p; \
        init; \
        for(const char *start, *end, *qstart, *qend; parselist(iter, start, end, qstart, qend);) \
        { \
            int len = end - start; \
            if(listincludes(cs, filter, start, len) dir 0) \
            { \
                if(!p.empty()) p.push(' '); \
                p.push_n(qstart, qend-qstart); \
            } \
        } \
        p.push('\0'); \
        cs.result->set_str(p.disown()); \
    })

LISTMERGECMD(listdel, , list, elems, < );
LISTMERGECMD(listintersect, , list, elems, >= );
LISTMERGECMD(listunion, p.push_n(list, strlen(list)), elems, list, < );

void listsplice(CsState &cs, const char *s, const char *vals, int *skip, int *count) {
    int offset = ostd::max(*skip, 0), len = ostd::max(*count, 0);
    const char *list = s, *start, *end, *qstart, *qend = s;
    for (int i = 0; i < offset; ++i) if (!parselist(s, start, end, qstart, qend)) break;
    ostd::Vector<char> p;
    if (qend > list) p.push_n(list, qend - list);
    if (*vals) {
        if (!p.empty()) p.push(' ');
        p.push_n(vals, strlen(vals));
    }
    for (int i = 0; i < len; ++i) if (!parselist(s)) break;
    skiplist(s);
    switch (*s) {
    case '\0':
    case ')':
    case ']':
        break;
    default:
        if (!p.empty()) p.push(' ');
        p.push_n(s, strlen(s));
        break;
    }
    p.push('\0');
    cs.result->set_str(p.disown());
}
COMMAND(listsplice, "ssii");

struct sortitem {
    const char *str, *quotestart, *quoteend;

    int quotelength() const {
        return int(quoteend - quotestart);
    }
};

struct sortfun {
    Ident *x, *y;
    ostd::uint *body;

    bool operator()(const sortitem &xval, const sortitem &yval) {
        if (x->valtype != VAL_CSTR) x->valtype = VAL_CSTR;
        x->clean_code();
        x->val.code = (const ostd::uint *)xval.str;
        if (y->valtype != VAL_CSTR) y->valtype = VAL_CSTR;
        y->clean_code();
        y->val.code = (const ostd::uint *)yval.str;
        return executebool(body);
    }
};

void sortlist(CsState &cs, char *list, Ident *x, Ident *y, ostd::uint *body, ostd::uint *unique) {
    if (x == y || x->type != ID_ALIAS || y->type != ID_ALIAS) return;

    ostd::Vector<sortitem> items;
    int clen = strlen(list), total = 0;
    char *cstr = dup_ostr(ostd::ConstCharRange(list, clen));
    const char *curlist = list, *start, *end, *quotestart, *quoteend;
    while (parselist(curlist, start, end, quotestart, quoteend)) {
        cstr[end - list] = '\0';
        sortitem item = { &cstr[start - list], quotestart, quoteend };
        items.push(item);
        total += item.quotelength();
    }

    if (items.empty()) {
        cs.result->set_str(cstr);
        return;
    }

    IdentStack xstack, ystack;
    pusharg(*x, null_value, xstack);
    x->flags &= ~IDF_UNKNOWN;
    pusharg(*y, null_value, ystack);
    y->flags &= ~IDF_UNKNOWN;

    int totalunique = total, numunique = items.size();
    if (body) {
        sortfun f = { x, y, body };
        ostd::sort(items.iter(), f);
        if ((*unique & CODE_OP_MASK) != CODE_EXIT) {
            f.body = unique;
            totalunique = items[0].quotelength();
            numunique = 1;
            for (ostd::Size i = 1; i < items.size(); i++) {
                sortitem &item = items[i];
                if (f(items[i - 1], item)) item.quotestart = nullptr;
                else {
                    totalunique += item.quotelength();
                    numunique++;
                }
            }
        }
    } else {
        sortfun f = { x, y, unique };
        totalunique = items[0].quotelength();
        numunique = 1;
        for (ostd::Size i = 1; i < items.size(); i++) {
            sortitem &item = items[i];
            for (ostd::Size j = 0; j < i; ++j) {
                sortitem &prev = items[j];
                if (prev.quotestart && f(item, prev)) {
                    item.quotestart = nullptr;
                    break;
                }
            }
            if (item.quotestart) {
                totalunique += item.quotelength();
                numunique++;
            }
        }
    }

    poparg(*x);
    poparg(*y);

    char *sorted = cstr;
    int sortedlen = totalunique + ostd::max(numunique - 1, 0);
    if (clen < sortedlen) {
        delete[] cstr;
        sorted = new char[sortedlen + 1];
    }

    int offset = 0;
    for (ostd::Size i = 0; i < items.size(); ++i) {
        sortitem &item = items[i];
        if (!item.quotestart) continue;
        int len = item.quotelength();
        if (i) sorted[offset++] = ' ';
        memcpy(&sorted[offset], item.quotestart, len);
        offset += len;
    }
    sorted[offset] = '\0';

    cs.result->set_str(sorted);
}
COMMAND(sortlist, "srree");
ICOMMAND(uniquelist, "srre", (CsState &cs, char *list, Ident *x, Ident *y, ostd::uint *body), sortlist(cs, list, x, y, nullptr, body));

#define MATHCMD(name, fmt, type, op, initval, unaryop) \
    ICOMMANDS(name, #fmt "1V", (CsState &cs, TaggedValue *args, int numargs), \
    { \
        type val; \
        if(numargs >= 2) \
        { \
            val = args[0].fmt; \
            type val2 = args[1].fmt; \
            op; \
            for(int i = 2; i < numargs; i++) { val2 = args[i].fmt; op; } \
        } \
        else { val = numargs > 0 ? args[0].fmt : initval; unaryop; } \
        cs.result->set_##type(val); \
    })
#define MATHICMDN(name, op, initval, unaryop) MATHCMD(#name, i, int, val = val op val2, initval, unaryop)
#define MATHICMD(name, initval, unaryop) MATHICMDN(name, name, initval, unaryop)
#define MATHFCMDN(name, op, initval, unaryop) MATHCMD(#name "f", f, float, val = val op val2, initval, unaryop)
#define MATHFCMD(name, initval, unaryop) MATHFCMDN(name, name, initval, unaryop)

#define CMPCMD(name, fmt, type, op) \
    ICOMMANDS(name, #fmt "1V", (CsState &cs, TaggedValue *args, int numargs), \
    { \
        bool val; \
        if(numargs >= 2) \
        { \
            val = args[0].fmt op args[1].fmt; \
            for(int i = 2; i < numargs && val; i++) val = args[i-1].fmt op args[i].fmt; \
        } \
        else val = (numargs > 0 ? args[0].fmt : 0) op 0; \
        cs.result->set_int(int(val)); \
    })
#define CMPICMDN(name, op) CMPCMD(#name, i, int, op)
#define CMPICMD(name) CMPICMDN(name, name)
#define CMPFCMDN(name, op) CMPCMD(#name "f", f, float, op)
#define CMPFCMD(name) CMPFCMDN(name, name)

MATHICMD(+, 0, );
MATHICMD(*, 1, );
MATHICMD(-, 0, val = -val);
CMPICMDN(=, ==);
CMPICMD(!=);
CMPICMD(<);
CMPICMD(>);
CMPICMD(<=);
CMPICMD(>=);
MATHICMD(^, 0, val = ~val);
MATHICMDN(~, ^, 0, val = ~val);
MATHICMD(&, 0, );
MATHICMD(|, 0, );
MATHICMD(^~, 0, );
MATHICMD(&~, 0, );
MATHICMD(|~, 0, );
MATHCMD("<<", i, int, val = val2 < 32 ? val << ostd::max(val2, 0) : 0, 0, );
MATHCMD(">>", i, int, val >>= ostd::clamp(val2, 0, 31), 0, );

MATHFCMD(+, 0, );
MATHFCMD(*, 1, );
MATHFCMD(-, 0, val = -val);
CMPFCMDN(=, ==);
CMPFCMD(!=);
CMPFCMD(<);
CMPFCMD(>);
CMPFCMD(<=);
CMPFCMD(>=);

ICOMMANDK(!, ID_NOT, "t", (CsState &cs, TaggedValue *a), cs.result->set_int(getbool(*a) ? 0 : 1));
ICOMMANDK(&&, ID_AND, "E1V", (CsState &cs, TaggedValue *args, int numargs), {
    if (!numargs) cs.result->set_int(1);
    else for (int i = 0; i < numargs; ++i) {
            if (i) cs.result->cleanup();
            if (args[i].type == VAL_CODE) executeret(args[i].code, *cs.result);
            else *cs.result = args[i];
            if (!getbool(*cs.result)) break;
        }
});
ICOMMANDK( ||, ID_OR, "E1V", (CsState &cs, TaggedValue *args, int numargs), {
    if (!numargs) cs.result->set_int(0);
    else for (int i = 0; i < numargs; ++i) {
            if (i) cs.result->cleanup();
            if (args[i].type == VAL_CODE) executeret(args[i].code, *cs.result);
            else *cs.result = args[i];
            if (getbool(*cs.result)) break;
        }
});


#define DIVCMD(name, fmt, type, op) MATHCMD(#name, fmt, type, { if(val2) op; else val = 0; }, 0, )

DIVCMD(div, i, int, val /= val2);
DIVCMD(mod, i, int, val %= val2);
DIVCMD(divf, f, float, val /= val2);
DIVCMD(modf, f, float, val = fmod(val, val2));
MATHCMD("pow", f, float, val = pow(val, val2), 0, );

static constexpr float PI = 3.14159265358979f;
static constexpr float RAD = PI / 180.0f;

void init_lib_math(CsState &cs) {
    cs.add_command("sin", "f", [](CsState &cs, float *a) {
        cs.result->set_float(sin(*a * RAD));
    });
    cs.add_command("cos", "f", [](CsState &cs, float *a) {
        cs.result->set_float(cos(*a * RAD));
    });
    cs.add_command("tan", "f", [](CsState &cs, float *a) {
        cs.result->set_float(tan(*a * RAD));
    });

    cs.add_command("asin", "f", [](CsState &cs, float *a) {
        cs.result->set_float(asin(*a) / RAD);
    });
    cs.add_command("acos", "f", [](CsState &cs, float *a) {
        cs.result->set_float(acos(*a) / RAD);
    });
    cs.add_command("atan", "f", [](CsState &cs, float *a) {
        cs.result->set_float(atan(*a) / RAD);
    });
    cs.add_command("atan2", "ff", [](CsState &cs, float *y, float *x) {
        cs.result->set_float(atan2(*y, *x) / RAD);
    });

    cs.add_command("sqrt", "f", [](CsState &cs, float *a) {
        cs.result->set_float(sqrt(*a));
    });
    cs.add_command("loge", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log(*a));
    });
    cs.add_command("log2", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log(*a) / M_LN2);
    });
    cs.add_command("log10", "f", [](CsState &cs, float *a) {
        cs.result->set_float(log10(*a));
    });

    cs.add_command("exp", "f", [](CsState &cs, float *a) {
        cs.result->set_float(exp(*a));
    });

#define CS_CMD_MIN_MAX(name, fmt, type, op) \
    cs.add_command(#name, #fmt "1V", [](CsState &cs, TaggedValue *args, \
                   int nargs) { \
        type v = (nargs > 0) ? args[0].fmt : 0; \
        for (int i = 1; i < nargs; ++i) v = op(v, args[i].fmt); \
        cs.result->set_##type(v); \
    })

    CS_CMD_MIN_MAX(min, i, int, ostd::min);
    CS_CMD_MIN_MAX(max, i, int, ostd::max);
    CS_CMD_MIN_MAX(minf, f, float, ostd::min);
    CS_CMD_MIN_MAX(maxf, f, float, ostd::max);

#undef CS_CMD_MIN_MAX

    cs.add_command("abs", "i", [](CsState &cs, int *v) {
        cs.result->set_int(abs(*v));
    });
    cs.add_command("absf", "f", [](CsState &cs, float *v) {
        cs.result->set_float(fabs(*v));
    });

    cs.add_command("floor", "f", [](CsState &cs, float *v) {
        cs.result->set_float(floor(*v));
    });
    cs.add_command("ceil", "f", [](CsState &cs, float *v) {
        cs.result->set_float(ceil(*v));
    });
}

ICOMMAND(round, "ff", (CsState &cs, float *n, float *k), {
    double step = *k;
    double r = *n;
    if (step > 0) {
        r += step * (r < 0 ? -0.5 : 0.5);
        r -= fmod(r, step);
    } else r = r < 0 ? ceil(r - 0.5) : floor(r + 0.5);
    cs.result->set_float(float(r));
});

ICOMMAND(cond, "ee2V", (CsState &cs, TaggedValue *args, int numargs), {
    for (int i = 0; i < numargs; i += 2) {
        if (i + 1 < numargs) {
            if (executebool(args[i].code)) {
                executeret(args[i + 1].code, *cs.result);
                break;
            }
        } else {
            executeret(args[i].code, *cs.result);
            break;
        }
    }
});

#define CASECOMMAND(name, fmt, type, acc, compare) \
    ICOMMAND(name, fmt "te2V", (CsState &cs, TaggedValue *args, int numargs), \
    { \
        type val = acc; \
        int i; \
        for(i = 1; i+1 < numargs; i += 2) \
        { \
            if(compare) \
            { \
                executeret(args[i+1].code, *cs.result); \
                return; \
            } \
        } \
    })

CASECOMMAND(case, "i", int, args[0].get_int(), args[i].type == VAL_NULL || args[i].get_int() == val);
CASECOMMAND(casef, "f", float, args[0].get_float(), args[i].type == VAL_NULL || args[i].get_float() == val);
CASECOMMAND(cases, "s", const char *, args[0].get_str(), args[i].type == VAL_NULL || !strcmp(args[i].get_str(), val));

ICOMMAND(tohex, "ii", (CsState &, int *n, int *p), {
    auto r = ostd::appender<ostd::Vector<char>>();
    ostd::format(r, "0x%.*X", ostd::max(*p, 1), *n);
    r.put('\0');
    stringret(r.get().disown());
});

#define CMPSCMD(name, op) \
    ICOMMAND(name, "s1V", (CsState &cs, TaggedValue *args, int numargs), \
    { \
        bool val; \
        if(numargs >= 2) \
        { \
            val = strcmp(args[0].s, args[1].s) op 0; \
            for(int i = 2; i < numargs && val; i++) val = strcmp(args[i-1].s, args[i].s) op 0; \
        } \
        else val = (numargs > 0 ? args[0].s[0] : 0) op 0; \
        cs.result->set_int(int(val)); \
    })

CMPSCMD(strcmp, ==);
CMPSCMD(=s, ==);
CMPSCMD(!=s, !=);
CMPSCMD(<s, <);
CMPSCMD(>s, >);
CMPSCMD(<=s, <=);
CMPSCMD(>=s, >=);

ICOMMAND(echo, "C", (CsState &, char *s), printf("%s\n", s));
ICOMMAND(strstr, "ss", (CsState &cs, char *a, char *b), { char *s = strstr(a, b); cs.result->set_int(s ? s - a : -1); });
ICOMMAND(strlen, "s", (CsState &cs, char *s), cs.result->set_int(strlen(s)));
ICOMMAND(strcode, "si", (CsState &cs, char *s, int *i), cs.result->set_int(*i > 0 ? (memchr(s, 0, *i) ? 0 : ostd::byte(s[*i])) : ostd::byte(s[0])));
ICOMMAND(codestr, "i", (CsState &, int *i), { char *s = new char[2]; s[0] = char(*i); s[1] = '\0'; stringret(s); });

#define STRMAPCOMMAND(name, map) \
    ICOMMAND(name, "s", (CsState &, char *s), \
    { \
        int len = strlen(s); \
        char *m = new char[len + 1]; \
        for (int i = 0; i < len; ++i) m[i] = map(s[i]); \
        m[len] = '\0'; \
        stringret(m); \
    })

STRMAPCOMMAND(strlower, tolower);
STRMAPCOMMAND(strupper, toupper);

char *strreplace(CsState &, const char *s, const char *oldval, const char *newval, const char *newval2) {
    ostd::Vector<char> buf;

    int oldlen = strlen(oldval);
    if (!oldlen) return dup_ostr(s);
    for (int i = 0;; i++) {
        const char *found = strstr(s, oldval);
        if (found) {
            while (s < found) buf.push(*s++);
            for (const char *n = i & 1 ? newval2 : newval; *n; n++) buf.push(*n);
            s = found + oldlen;
        } else {
            while (*s) buf.push(*s++);
            buf.push('\0');
            return dup_ostr(ostd::ConstCharRange(buf.data(), buf.size()));
        }
    }
}

ICOMMAND(strreplace, "ssss", (CsState &cs, char *s, char *o, char *n, char *n2), cs.result->set_str(strreplace(cs, s, o, n, n2[0] ? n2 : n)));

void strsplice(CsState &cs, const char *s, const char *vals, int *skip, int *count) {
    int slen = strlen(s), vlen = strlen(vals),
        offset = ostd::clamp(*skip, 0, slen),
        len = ostd::clamp(*count, 0, slen - offset);
    char *p = new char[slen - len + vlen + 1];
    if (offset) memcpy(p, s, offset);
    if (vlen) memcpy(&p[offset], vals, vlen);
    if (offset + len < slen) memcpy(&p[offset + vlen], &s[offset + len], slen - (offset + len));
    p[slen - len + vlen] = '\0';
    cs.result->set_str(p);
}
COMMAND(strsplice, "ssii");

void init_lib_shell(CsState &cs) {
    cs.add_command("shell", "C", [](CsState &cs, char *s) {
        cs.result->set_int(system(s));
    });
}