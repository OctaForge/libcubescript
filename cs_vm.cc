#include "cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

#include <ostd/memory.hh>

namespace cscript {

static inline bool cs_has_cmd_cb(CsIdent *id) {
    if (!id->is_command() && !id->is_special()) {
        return false;
    }
    Command *cb = static_cast<Command *>(id);
    return !!cb->cb_cftv;
}

static inline void cs_push_alias(CsIdent *id, CsIdentStack &st) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        static_cast<CsAlias *>(id)->push_arg(null_value, st);
    }
}

static inline void cs_pop_alias(CsIdent *id) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        static_cast<CsAlias *>(id)->pop_arg();
    }
}

/* TODO: make thread_local later */
static ostd::ConstCharRange cs_src_file, cs_src_str;

ostd::ConstCharRange cs_debug_line(
    ostd::ConstCharRange p, ostd::ConstCharRange fmt, ostd::CharRange buf
) {
    if (cs_src_str.empty()) {
        return fmt;
    }
    ostd::Size num = 1;
    ostd::ConstCharRange line(cs_src_str);
    for (;;) {
        ostd::ConstCharRange end = ostd::find(line, '\n');
        if (!end.empty()) {
            line = ostd::slice_until(line, end);
        }
        if (&p[0] >= &line[0] && &p[0] <= &line[line.size()]) {
            ostd::CharRange r(buf);
            if (!cs_src_file.empty()) {
                ostd::format(r, "%s:%d: %s", cs_src_file, num, fmt);
            } else {
                ostd::format(r, "%d: %s", num, fmt);
            }
            r.put('\0');
            return buf;
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

void cs_debug_alias(CsState &cs) {
    CsIvar *dalias = static_cast<CsIvar *>(cs.identmap[DbgaliasIdx]);
    if (!dalias->get_value()) {
        return;
    }
    int total = 0, depth = 0;
    for (CsIdentLink *l = cs.p_stack; l != &cs.noalias; l = l->next) {
        total++;
    }
    for (CsIdentLink *l = cs.p_stack; l != &cs.noalias; l = l->next) {
        CsIdent *id = l->id;
        ++depth;
        if (depth < dalias->get_value()) {
            ostd::err.writefln("  %d) %s", total - depth + 1, id->get_name());
        } else if (l->next == &cs.noalias) {
            ostd::err.writefln(
                depth == dalias->get_value() ? "  %d) %s" : "  ..%d) %s",
                total - depth + 1, id->get_name()
            );
        }
    }
}

static void bcode_ref(ostd::Uint32 *code) {
    if (!code) {
        return;
    }
    if ((*code & CODE_OP_MASK) == CODE_START) {
        bcode_incr(code);
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
        case CODE_START:
            bcode_incr(&code[-1]);
            break;
        case CODE_OFFSET:
            code -= ostd::Ptrdiff(code[-1] >> 8);
            bcode_incr(code);
            break;
    }
}

static void bcode_unref(ostd::Uint32 *code) {
    if (!code) {
        return;
    }
    if ((*code & CODE_OP_MASK) == CODE_START) {
        bcode_decr(code);
        return;
    }
    switch (code[-1]&CODE_OP_MASK) {
        case CODE_START:
            bcode_decr(&code[-1]);
            break;
        case CODE_OFFSET:
            code -= ostd::Ptrdiff(code[-1] >> 8);
            bcode_decr(code);
            break;
    }
}

CsBytecodeRef::CsBytecodeRef(CsBytecode *v): p_code(v) {
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
}
CsBytecodeRef::CsBytecodeRef(CsBytecodeRef const &v): p_code(v.p_code) {
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
}

CsBytecodeRef::~CsBytecodeRef() {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
}

CsBytecodeRef &CsBytecodeRef::operator=(CsBytecodeRef const &v) {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
    p_code = v.p_code;
    bcode_ref(reinterpret_cast<ostd::Uint32 *>(p_code));
    return *this;
}

CsBytecodeRef &CsBytecodeRef::operator=(CsBytecodeRef &&v) {
    bcode_unref(reinterpret_cast<ostd::Uint32 *>(p_code));
    p_code = v.p_code;
    v.p_code = nullptr;
    return *this;
}

static inline ostd::Uint32 const *forcecode(CsState &cs, CsValue &v) {
    if (v.get_type() != VAL_CODE) {
        GenState gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        v.cleanup();
        v.set_code(reinterpret_cast<CsBytecode *>(gs.code.disown() + 1));
    }
    return reinterpret_cast<ostd::Uint32 const *>(v.code);
}

static inline void forcecond(CsState &cs, CsValue &v) {
    switch (v.get_type()) {
        case VAL_STR:
        case VAL_MACRO:
        case VAL_CSTR:
            if (v.s[0]) {
                forcecode(cs, v);
            } else {
                v.set_int(0);
            }
            break;
    }
}

static ostd::Uint32 emptyblock[VAL_ANY][2] = {
    { CODE_START + 0x100, CODE_EXIT | RET_NULL },
    { CODE_START + 0x100, CODE_EXIT | RET_INT },
    { CODE_START + 0x100, CODE_EXIT | RET_FLOAT },
    { CODE_START + 0x100, CODE_EXIT | RET_STR }
};

static inline void force_arg(CsValue &v, int type) {
    switch (type) {
        case RET_STR:
            if (v.get_type() != VAL_STR) {
                v.force_str();
            }
            break;
        case RET_INT:
            if (v.get_type() != VAL_INT) {
                v.force_int();
            }
            break;
        case RET_FLOAT:
            if (v.get_type() != VAL_FLOAT) {
                v.force_float();
            }
            break;
    }
}

static inline void free_args(CsValue *args, int &oldnum, int newnum) {
    for (int i = newnum; i < oldnum; i++) {
        args[i].cleanup();
    }
    oldnum = newnum;
}

static ostd::Uint32 const *skipcode(
    ostd::Uint32 const *code, CsValue *result = nullptr
) {
    int depth = 0;
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
            case CODE_MACRO:
            case CODE_VAL | RET_STR: {
                ostd::Uint32 len = op >> 8;
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }
            case CODE_BLOCK:
            case CODE_JUMP:
            case CODE_JUMP_TRUE:
            case CODE_JUMP_FALSE:
            case CODE_JUMP_RESULT_TRUE:
            case CODE_JUMP_RESULT_FALSE: {
                ostd::Uint32 len = op >> 8;
                code += len;
                continue;
            }
            case CODE_ENTER:
            case CODE_ENTER_RESULT:
                ++depth;
                continue;
            case CODE_EXIT | RET_NULL:
            case CODE_EXIT | RET_STR:
            case CODE_EXIT | RET_INT:
            case CODE_EXIT | RET_FLOAT:
                if (depth <= 0) {
                    if (result) {
                        force_arg(*result, op & CODE_RET_MASK);
                    }
                    return code;
                }
                --depth;
                continue;
        }
    }
}

void CsValue::copy_arg(CsValue &r) const {
    r.cleanup();
    switch (get_type()) {
        case VAL_INT:
        case VAL_FLOAT:
        case VAL_IDENT:
            r = *this;
            break;
        case VAL_STR:
        case VAL_CSTR:
        case VAL_MACRO:
            r.set_str(ostd::ConstCharRange(s, len));
            break;
        case VAL_CODE: {
            ostd::Uint32 const *bcode = reinterpret_cast<ostd::Uint32 const *>(code);
            ostd::Uint32 const *end = skipcode(bcode);
            ostd::Uint32 *dst = new ostd::Uint32[end - bcode + 1];
            *dst++ = CODE_START;
            memcpy(dst, bcode, (end - bcode) * sizeof(ostd::Uint32));
            r.set_code(reinterpret_cast<CsBytecode const *>(dst));
            break;
        }
        default:
            r.set_null();
            break;
    }
}

static inline void callcommand(
    CsState &cs, Command *id, CsValue *args, CsValue &res, int numargs,
    bool lookup = false
) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (char const *fmt = id->cargs; *fmt; fmt++) {
        switch (*fmt) {
            case 'i':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_int(0);
                    fakeargs++;
                } else {
                    args[i].force_int();
                }
                break;
            case 'b':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_int(CsIntMin);
                    fakeargs++;
                } else {
                    args[i].force_int();
                }
                break;
            case 'f':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_float(0.0f);
                    fakeargs++;
                } else {
                    args[i].force_float();
                }
                break;
            case 'F':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_float(args[i - 1].get_float());
                    fakeargs++;
                } else {
                    args[i].force_float();
                }
                break;
            case 'S':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_str("");
                    fakeargs++;
                } else {
                    args[i].force_str();
                }
                break;
            case 's':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_cstr("");
                    fakeargs++;
                } else {
                    args[i].force_str();
                }
                break;
            case 'T':
            case 't':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_null();
                    fakeargs++;
                }
                break;
            case 'E':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_null();
                    fakeargs++;
                } else {
                    forcecond(cs, args[i]);
                }
                break;
            case 'e':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_code(
                        reinterpret_cast<CsBytecode *>(emptyblock[VAL_NULL] + 1)
                    );
                    fakeargs++;
                } else {
                    forcecode(cs, args[i]);
                }
                break;
            case 'r':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_ident(cs.identmap[DummyIdx]);
                    fakeargs++;
                } else {
                    cs.force_ident(args[i]);
                }
                break;
            case '$':
                if (++i < numargs) {
                    args[i].cleanup();
                }
                args[i].set_ident(id);
                break;
            case 'N':
                if (++i < numargs) {
                    args[i].cleanup();
                }
                args[i].set_int(CsInt(lookup ? -1 : i - fakeargs));
                break;
            case 'C': {
                i = ostd::max(i + 1, numargs);
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(buf, ostd::iter(args, i), " ");
                CsValue tv;
                tv.set_mstr(buf.get().iter());
                id->cb_cftv(CsValueRange(&tv, 1), res);
                goto cleanup;
            }
            case 'V':
                i = ostd::max(i + 1, numargs);
                id->cb_cftv(ostd::iter(args, i), res);
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
    }
    ++i;
    id->cb_cftv(CsValueRange(args, i), res);
cleanup:
    for (ostd::Size k = 0; k < ostd::Size(i); ++k) {
        args[k].cleanup();
    }
    for (; i < numargs; i++) {
        args[i].cleanup();
    }
}

static ostd::Uint32 const *runcode(
    CsState &cs, ostd::Uint32 const *code, CsValue &result
);

static inline void cs_call_alias(
    CsState &cs, CsAlias *a, CsValue *args, CsValue &result,
    int callargs, int &nargs, int offset, int skip, ostd::Uint32 op
) {
    CsIvar *anargs = static_cast<CsIvar *>(cs.identmap[NumargsIdx]);
    CsIdentStack argstack[MaxArguments];
    for(int i = 0; i < callargs; i++) {
        static_cast<CsAlias *>(cs.identmap[i])->push_arg(
            args[offset + i], argstack[i], false
        );
    }
    int oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = cs.identflags;
    cs.identflags |= a->get_flags()&IDF_OVERRIDDEN;
    CsIdentLink aliaslink = {
        a, cs.p_stack, (1<<callargs)-1, argstack
    };
    cs.p_stack = &aliaslink;
    ostd::Uint32 *codep = reinterpret_cast<ostd::Uint32 *>(a->compile_code(cs));
    bcode_incr(codep);
    runcode(cs, codep+1, (result));
    bcode_decr(codep);
    cs.p_stack = aliaslink.next;
    cs.identflags = oldflags;
    for (int i = 0; i < callargs; i++) {
        static_cast<CsAlias *>(cs.identmap[i])->pop_arg();
    }
    int argmask = aliaslink.usedargs & (~0 << callargs);
    for (; argmask; ++callargs) {
        if (argmask & (1 << callargs)) {
            static_cast<CsAlias *>(cs.identmap[callargs])->pop_arg();
            argmask &= ~(1 << callargs);
        }
    }
    force_arg(result, op & CODE_RET_MASK);
    anargs->set_value(oldargs);
    nargs = offset - skip;
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

static inline CsAlias *cs_get_lookup_id(CsState &cs, ostd::Uint32 op) {
    CsIdent *id = cs.identmap[op >> 8];
    if (id->get_flags() & IDF_UNKNOWN) {
        cs_debug_code(cs, "unknown alias lookup: %s", id->get_name());
    }
    return static_cast<CsAlias *>(id);
}

static inline CsAlias *cs_get_lookuparg_id(CsState &cs, ostd::Uint32 op) {
    CsIdent *id = cs.identmap[op >> 8];
    if (!(cs.p_stack->usedargs & (1 << id->get_index()))) {
        return nullptr;
    }
    return static_cast<CsAlias *>(id);
}

static inline int cs_get_lookupu_type(
    CsState &cs, CsValue &arg, CsIdent *&id, ostd::Uint32 op
) {
    if (
        arg.get_type() != VAL_STR &&
        arg.get_type() != VAL_MACRO &&
        arg.get_type() != VAL_CSTR
    ) {
        return -2; /* default case */
    }
    id = cs.get_ident(arg.s);
    if (id) {
        switch(id->get_type()) {
            case CsIdentType::alias:
                if (id->get_flags() & IDF_UNKNOWN) {
                    break;
                }
                arg.cleanup();
                if (
                    (id->get_index() < MaxArguments) &&
                    !(cs.p_stack->usedargs & (1 << id->get_index()))
                ) {
                    return ID_UNKNOWN;
                }
                return ID_ALIAS;
            case CsIdentType::svar:
                arg.cleanup();
                return ID_SVAR;
            case CsIdentType::ivar:
                arg.cleanup();
                return ID_IVAR;
            case CsIdentType::fvar:
                arg.cleanup();
                return ID_FVAR;
            case CsIdentType::command: {
                arg.cleanup();
                arg.set_null();
                CsValue buf[MaxArguments];
                callcommand(cs, static_cast<Command *>(id), buf, arg, 0, true);
                force_arg(arg, op & CODE_RET_MASK);
                return -2; /* ignore */
            }
            default:
                arg.cleanup();
                return ID_UNKNOWN;
        }
    }
    cs_debug_code(cs, "unknown alias lookup: %s", arg.s);
    arg.cleanup();
    return ID_UNKNOWN;
}

static ostd::Uint32 const *runcode(
    CsState &cs, ostd::Uint32 const *code, CsValue &result
) {
    result.set_null();
    if (rundepth >= MaxRunDepth) {
        cs_debug_code(cs, "exceeded recursion limit");
        return skipcode(code, &result);
    }
    ++rundepth;
    int numargs = 0;
    CsValue args[MaxArguments + MaxResults];
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
            case CODE_START:
            case CODE_OFFSET:
                continue;

            case CODE_NULL | RET_NULL:
                result.cleanup();
                result.set_null();
                continue;
            case CODE_NULL | RET_STR:
                result.cleanup();
                result.set_str("");
                continue;
            case CODE_NULL | RET_INT:
                result.cleanup();
                result.set_int(0);
                continue;
            case CODE_NULL | RET_FLOAT:
                result.cleanup();
                result.set_float(0.0f);
                continue;

            case CODE_FALSE | RET_STR:
                result.cleanup();
                result.set_str("0");
                continue;
            case CODE_FALSE | RET_NULL:
            case CODE_FALSE | RET_INT:
                result.cleanup();
                result.set_int(0);
                continue;
            case CODE_FALSE | RET_FLOAT:
                result.cleanup();
                result.set_float(0.0f);
                continue;

            case CODE_TRUE | RET_STR:
                result.cleanup();
                result.set_str("1");
                continue;
            case CODE_TRUE | RET_NULL:
            case CODE_TRUE | RET_INT:
                result.cleanup();
                result.set_int(1);
                continue;
            case CODE_TRUE | RET_FLOAT:
                result.cleanup();
                result.set_float(1.0f);
                continue;

            case CODE_NOT | RET_STR:
                result.cleanup();
                --numargs;
                result.set_str(args[numargs].get_bool() ? "0" : "1");
                args[numargs].cleanup();
                continue;
            case CODE_NOT | RET_NULL:
            case CODE_NOT | RET_INT:
                result.cleanup();
                --numargs;
                result.set_int(!args[numargs].get_bool());
                args[numargs].cleanup();
                continue;
            case CODE_NOT | RET_FLOAT:
                result.cleanup();
                --numargs;
                result.set_float(CsFloat(!args[numargs].get_bool()));
                args[numargs].cleanup();
                continue;

            case CODE_POP:
                args[--numargs].cleanup();
                continue;
            case CODE_ENTER:
                code = runcode(cs, code, args[numargs++]);
                continue;
            case CODE_ENTER_RESULT:
                result.cleanup();
                code = runcode(cs, code, result);
                continue;
            case CODE_EXIT | RET_STR:
            case CODE_EXIT | RET_INT:
            case CODE_EXIT | RET_FLOAT:
                force_arg(result, op & CODE_RET_MASK);
            /* fallthrough */
            case CODE_EXIT | RET_NULL:
                goto exit;
            case CODE_RESULT_ARG | RET_STR:
            case CODE_RESULT_ARG | RET_INT:
            case CODE_RESULT_ARG | RET_FLOAT:
                force_arg(result, op & CODE_RET_MASK);
            /* fallthrough */
            case CODE_RESULT_ARG | RET_NULL:
                args[numargs++] = result;
                result.set_null();
                continue;
            case CODE_PRINT:
                cs.print_var(static_cast<CsVar *>(cs.identmap[op >> 8]));
                continue;

            case CODE_LOCAL: {
                result.cleanup();
                int numlocals = op >> 8, offset = numargs - numlocals;
                CsIdentStack locals[MaxArguments];
                for (int i = 0; i < numlocals; ++i) {
                    cs_push_alias(args[offset + i].id, locals[i]);
                }
                code = runcode(cs, code, result);
                for (int i = offset; i < numargs; i++) {
                    cs_pop_alias(args[i].id);
                }
                goto exit;
            }

            case CODE_DOARGS | RET_NULL:
            case CODE_DOARGS | RET_STR:
            case CODE_DOARGS | RET_INT:
            case CODE_DOARGS | RET_FLOAT:
                if (cs.p_stack != &cs.noalias) {
                    cs_do_args(cs, [&]() {
                        result.cleanup();
                        cs.run_ret(args[--numargs].code, result);
                        args[numargs].cleanup();
                        force_arg(result, op & CODE_RET_MASK);
                    });
                    continue;
                }
            /* fallthrough */
            case CODE_DO | RET_NULL:
            case CODE_DO | RET_STR:
            case CODE_DO | RET_INT:
            case CODE_DO | RET_FLOAT:
                result.cleanup();
                cs.run_ret(args[--numargs].code, result);
                args[numargs].cleanup();
                force_arg(result, op & CODE_RET_MASK);
                continue;

            case CODE_JUMP: {
                ostd::Uint32 len = op >> 8;
                code += len;
                continue;
            }
            case CODE_JUMP_TRUE: {
                ostd::Uint32 len = op >> 8;
                if (args[--numargs].get_bool()) {
                    code += len;
                }
                args[numargs].cleanup();
                continue;
            }
            case CODE_JUMP_FALSE: {
                ostd::Uint32 len = op >> 8;
                if (!args[--numargs].get_bool()) {
                    code += len;
                }
                args[numargs].cleanup();
                continue;
            }
            case CODE_JUMP_RESULT_TRUE: {
                ostd::Uint32 len = op >> 8;
                result.cleanup();
                --numargs;
                if (args[numargs].get_type() == VAL_CODE) {
                    cs.run_ret(args[numargs].code, result);
                    args[numargs].cleanup();
                } else {
                    result = args[numargs];
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CODE_JUMP_RESULT_FALSE: {
                ostd::Uint32 len = op >> 8;
                result.cleanup();
                --numargs;
                if (args[numargs].get_type() == VAL_CODE) {
                    cs.run_ret(args[numargs].code, result);
                    args[numargs].cleanup();
                } else {
                    result = args[numargs];
                }
                if (!result.get_bool()) {
                    code += len;
                }
                continue;
            }

            case CODE_MACRO: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_macro(
                    reinterpret_cast<CsBytecode const *>(code), len
                );
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }

            case CODE_VAL | RET_STR: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_str(ostd::ConstCharRange(
                    reinterpret_cast<char const *>(code), len
                ));
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }
            case CODE_VALI | RET_STR: {
                char s[4] = {
                    char((op >> 8) & 0xFF),
                    char((op >> 16) & 0xFF),
                    char((op >> 24) & 0xFF), '\0'
                };
                args[numargs++].set_str(s);
                continue;
            }
            case CODE_VAL | RET_NULL:
            case CODE_VALI | RET_NULL:
                args[numargs++].set_null();
                continue;
            case CODE_VAL | RET_INT:
                args[numargs++].set_int(CsInt(*code++));
                continue;
            case CODE_VALI | RET_INT:
                args[numargs++].set_int(CsInt(op) >> 8);
                continue;
            case CODE_VAL | RET_FLOAT:
                args[numargs++].set_float(
                    *reinterpret_cast<CsFloat const *>(code++)
                );
                continue;
            case CODE_VALI | RET_FLOAT:
                args[numargs++].set_float(CsFloat(CsInt(op) >> 8));
                continue;

            case CODE_DUP | RET_NULL:
                args[numargs - 1].get_val(args[numargs]);
                numargs++;
                continue;
            case CODE_DUP | RET_INT:
                args[numargs].set_int(args[numargs - 1].get_int());
                numargs++;
                continue;
            case CODE_DUP | RET_FLOAT:
                args[numargs].set_float(args[numargs - 1].get_float());
                numargs++;
                continue;
            case CODE_DUP | RET_STR:
                args[numargs].set_str(ostd::move(args[numargs - 1].get_str()));
                numargs++;
                continue;

            case CODE_FORCE | RET_STR:
                args[numargs - 1].force_str();
                continue;
            case CODE_FORCE | RET_INT:
                args[numargs - 1].force_int();
                continue;
            case CODE_FORCE | RET_FLOAT:
                args[numargs - 1].force_float();
                continue;

            case CODE_RESULT | RET_NULL:
                result.cleanup();
                result = args[--numargs];
                continue;
            case CODE_RESULT | RET_STR:
            case CODE_RESULT | RET_INT:
            case CODE_RESULT | RET_FLOAT:
                result.cleanup();
                result = args[--numargs];
                force_arg(result, op & CODE_RET_MASK);
                continue;

            case CODE_EMPTY | RET_NULL:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[VAL_NULL] + 1)
                );
                break;
            case CODE_EMPTY | RET_STR:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[VAL_STR] + 1)
                );
                break;
            case CODE_EMPTY | RET_INT:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[VAL_INT] + 1)
                );
                break;
            case CODE_EMPTY | RET_FLOAT:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[VAL_FLOAT] + 1)
                );
                break;
            case CODE_BLOCK: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode const *>(code + 1)
                );
                code += len;
                continue;
            }
            case CODE_COMPILE: {
                CsValue &arg = args[numargs - 1];
                GenState gs(cs);
                switch (arg.get_type()) {
                    case VAL_INT:
                        gs.code.reserve(8);
                        gs.code.push(CODE_START);
                        gs.gen_int(arg.i);
                        gs.code.push(CODE_RESULT);
                        gs.code.push(CODE_EXIT);
                        break;
                    case VAL_FLOAT:
                        gs.code.reserve(8);
                        gs.code.push(CODE_START);
                        gs.gen_float(arg.f);
                        gs.code.push(CODE_RESULT);
                        gs.code.push(CODE_EXIT);
                        break;
                    case VAL_STR:
                    case VAL_MACRO:
                    case VAL_CSTR:
                        gs.code.reserve(64);
                        gs.gen_main(arg.s);
                        arg.cleanup();
                        break;
                    default:
                        gs.code.reserve(8);
                        gs.code.push(CODE_START);
                        gs.gen_null();
                        gs.code.push(CODE_RESULT);
                        gs.code.push(CODE_EXIT);
                        break;
                }
                arg.set_code(
                    reinterpret_cast<CsBytecode const *>(gs.code.disown() + 1)
                );
                continue;
            }
            case CODE_COND: {
                CsValue &arg = args[numargs - 1];
                switch (arg.get_type()) {
                    case VAL_STR:
                    case VAL_MACRO:
                    case VAL_CSTR:
                        if (arg.s[0]) {
                            GenState gs(cs);
                            gs.code.reserve(64);
                            gs.gen_main(arg.s);
                            arg.cleanup();
                            arg.set_code(reinterpret_cast<CsBytecode const *>(
                                gs.code.disown() + 1
                            ));
                        } else {
                            arg.force_null();
                        }
                        break;
                }
                continue;
            }

            case CODE_IDENT:
                args[numargs++].set_ident(cs.identmap[op >> 8]);
                continue;
            case CODE_IDENTARG: {
                CsAlias *a = static_cast<CsAlias *>(cs.identmap[op >> 8]);
                if (!(cs.p_stack->usedargs & (1 << a->get_index()))) {
                    a->push_arg(
                        null_value, cs.p_stack->argstack[a->get_index()], false
                    );
                    cs.p_stack->usedargs |= 1 << a->get_index();
                }
                args[numargs++].set_ident(a);
                continue;
            }
            case CODE_IDENTU: {
                CsValue &arg = args[numargs - 1];
                CsIdent *id = cs.identmap[DummyIdx];
                if (
                    arg.get_type() == VAL_STR ||
                    arg.get_type() == VAL_MACRO ||
                    arg.get_type() == VAL_CSTR
                ) {
                    id = cs.new_ident(ostd::ConstCharRange(arg.cstr, arg.len));
                }
                if (
                    id->get_index() < MaxArguments &&
                    !(cs.p_stack->usedargs & (1 << id->get_index()))
                ) {
                    static_cast<CsAlias *>(id)->push_arg(
                        null_value, cs.p_stack->argstack[id->get_index()], false
                    );
                    cs.p_stack->usedargs |= 1 << id->get_index();
                }
                arg.cleanup();
                arg.set_ident(id);
                continue;
            }

            case CODE_LOOKUPU | RET_STR: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_str(ostd::move(
                            static_cast<CsAlias *>(id)->val_v.get_str()
                        ));
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_str(ostd::move(
                            intstr(static_cast<CsIvar *>(id)->get_value())
                        ));
                        continue;
                    case ID_FVAR:
                        arg.set_str(ostd::move(
                            floatstr(static_cast<CsFvar *>(id)->get_value())
                        ));
                        continue;
                    case ID_UNKNOWN:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUP | RET_STR:
                args[numargs++].set_str(
                    ostd::move(cs_get_lookup_id(cs, op)->val_v.get_str())
                );
                continue;
            case CODE_LOOKUPARG | RET_STR: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs++].set_str(ostd::move(a->val_v.get_str()));
                }
                continue;
            }
            case CODE_LOOKUPU | RET_INT: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_int(static_cast<CsAlias *>(id)->val_v.get_int());
                        continue;
                    case ID_SVAR:
                        arg.set_int(cs_parse_int(
                            static_cast<CsSvar *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_int(
                            CsInt(static_cast<CsFvar *>(id)->get_value())
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_int(0);
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUP | RET_INT:
                args[numargs++].set_int(
                    cs_get_lookup_id(cs, op)->val_v.get_int()
                );
                continue;
            case CODE_LOOKUPARG | RET_INT: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_int(0);
                } else {
                    args[numargs++].set_int(a->val_v.get_int());
                }
                continue;
            }
            case CODE_LOOKUPU | RET_FLOAT: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_float(
                            static_cast<CsAlias *>(id)->val_v.get_float()
                        );
                        continue;
                    case ID_SVAR:
                        arg.set_float(cs_parse_float(
                            static_cast<CsSvar *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_float(CsFloat(
                            static_cast<CsIvar *>(id)->get_value()
                        ));
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<CsFvar *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_float(CsFloat(0));
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUP | RET_FLOAT:
                args[numargs++].set_float(
                    cs_get_lookup_id(cs, op)->val_v.get_float()
                );
                continue;
            case CODE_LOOKUPARG | RET_FLOAT: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_float(CsFloat(0));
                } else {
                    args[numargs++].set_float(a->val_v.get_float());
                }
                continue;
            }
            case CODE_LOOKUPU | RET_NULL: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<CsAlias *>(id)->val_v.get_val(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<CsFvar *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_null();
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUP | RET_NULL:
                cs_get_lookup_id(cs, op)->val_v.get_val(args[numargs++]);
                continue;
            case CODE_LOOKUPARG | RET_NULL: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->val_v.get_val(args[numargs++]);
                }
                continue;
            }

            case CODE_LOOKUPMU | RET_STR: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<CsAlias *>(id)->get_cstr(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_cstr(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_str(ostd::move(
                            intstr(static_cast<CsIvar *>(id)->get_value())
                        ));
                        continue;
                    case ID_FVAR:
                        arg.set_str(ostd::move(
                            floatstr(static_cast<CsFvar *>(id)->get_value())
                        ));
                        continue;
                    case ID_UNKNOWN:
                        arg.set_cstr("");
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUPM | RET_STR:
                cs_get_lookup_id(cs, op)->get_cstr(args[numargs++]);
                continue;
            case CODE_LOOKUPMARG | RET_STR: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_cstr("");
                } else {
                    a->get_cstr(args[numargs++]);
                }
                continue;
            }
            case CODE_LOOKUPMU | RET_NULL: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<CsAlias *>(id)->get_cval(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_cstr(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<CsFvar *>(id)->get_value());
                        continue;
                    case ID_UNKNOWN:
                        arg.set_null();
                        continue;
                    default:
                        continue;
                }
            }
            case CODE_LOOKUPM | RET_NULL:
                cs_get_lookup_id(cs, op)->get_cval(args[numargs++]);
                continue;
            case CODE_LOOKUPMARG | RET_NULL: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->get_cval(args[numargs++]);
                }
                continue;
            }

            case CODE_SVAR | RET_STR:
            case CODE_SVAR | RET_NULL:
                args[numargs++].set_str(
                    static_cast<CsSvar *>(cs.identmap[op >> 8])->get_value()
                );
                continue;
            case CODE_SVAR | RET_INT:
                args[numargs++].set_int(cs_parse_int(
                    static_cast<CsSvar *>(cs.identmap[op >> 8])->get_value()
                ));
                continue;
            case CODE_SVAR | RET_FLOAT:
                args[numargs++].set_float(cs_parse_float(
                    static_cast<CsSvar *>(cs.identmap[op >> 8])->get_value()
                ));
                continue;
            case CODE_SVARM:
                args[numargs++].set_cstr(
                    static_cast<CsSvar *>(cs.identmap[op >> 8])->get_value()
                );
                continue;
            case CODE_SVAR1:
                cs.set_var_str_checked(
                    static_cast<CsSvar *>(cs.identmap[op >> 8]), args[--numargs].s
                );
                args[numargs].cleanup();
                continue;

            case CODE_IVAR | RET_INT:
            case CODE_IVAR | RET_NULL:
                args[numargs++].set_int(
                    static_cast<CsIvar *>(cs.identmap[op >> 8])->get_value()
                );
                continue;
            case CODE_IVAR | RET_STR:
                args[numargs++].set_str(ostd::move(intstr(
                    static_cast<CsIvar *>(cs.identmap[op >> 8])->get_value()
                )));
                continue;
            case CODE_IVAR | RET_FLOAT:
                args[numargs++].set_float(CsFloat(
                    static_cast<CsIvar *>(cs.identmap[op >> 8])->get_value()
                ));
                continue;
            case CODE_IVAR1:
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.identmap[op >> 8]), args[--numargs].i
                );
                continue;
            case CODE_IVAR2:
                numargs -= 2;
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.identmap[op >> 8]),
                    (args[numargs].i << 16) | (args[numargs + 1].i << 8));
                continue;
            case CODE_IVAR3:
                numargs -= 3;
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.identmap[op >> 8]),
                    (args[numargs].i << 16)
                        | (args[numargs + 1].i << 8)
                        | args[numargs + 2].i);
                continue;

            case CODE_FVAR | RET_FLOAT:
            case CODE_FVAR | RET_NULL:
                args[numargs++].set_float(
                    static_cast<CsFvar *>(cs.identmap[op >> 8])->get_value()
                );
                continue;
            case CODE_FVAR | RET_STR:
                args[numargs++].set_str(ostd::move(floatstr(
                    static_cast<CsFvar *>(cs.identmap[op >> 8])->get_value()
                )));
                continue;
            case CODE_FVAR | RET_INT:
                args[numargs++].set_int(int(
                    static_cast<CsFvar *>(cs.identmap[op >> 8])->get_value()
                ));
                continue;
            case CODE_FVAR1:
                cs.set_var_float_checked(
                    static_cast<CsFvar *>(cs.identmap[op >> 8]), args[--numargs].f
                );
                continue;

            case CODE_COM | RET_NULL:
            case CODE_COM | RET_STR:
            case CODE_COM | RET_FLOAT:
            case CODE_COM | RET_INT: {
                Command *id = static_cast<Command *>(cs.identmap[op >> 8]);
                int offset = numargs - id->numargs;
                result.force_null();
                id->cb_cftv(CsValueRange(args + offset, id->numargs), result);
                force_arg(result, op & CODE_RET_MASK);
                free_args(args, numargs, offset);
                continue;
            }

            case CODE_COMV | RET_NULL:
            case CODE_COMV | RET_STR:
            case CODE_COMV | RET_FLOAT:
            case CODE_COMV | RET_INT: {
                Command *id = static_cast<Command *>(cs.identmap[op >> 13]);
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                id->cb_cftv(ostd::iter(&args[offset], callargs), result);
                force_arg(result, op & CODE_RET_MASK);
                free_args(args, numargs, offset);
                continue;
            }
            case CODE_COMC | RET_NULL:
            case CODE_COMC | RET_STR:
            case CODE_COMC | RET_FLOAT:
            case CODE_COMC | RET_INT: {
                Command *id = static_cast<Command *>(cs.identmap[op >> 13]);
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                {
                    auto buf = ostd::appender<CsString>();
                    cscript::util::tvals_concat(
                        buf, ostd::iter(&args[offset], callargs), " "
                    );
                    CsValue tv;
                    tv.set_mstr(buf.get().iter());
                    id->cb_cftv(CsValueRange(&tv, 1), result);
                }
                force_arg(result, op & CODE_RET_MASK);
                free_args(args, numargs, offset);
                continue;
            }

            case CODE_CONC | RET_NULL:
            case CODE_CONC | RET_STR:
            case CODE_CONC | RET_FLOAT:
            case CODE_CONC | RET_INT:
            case CODE_CONCW | RET_NULL:
            case CODE_CONCW | RET_STR:
            case CODE_CONCW | RET_FLOAT:
            case CODE_CONCW | RET_INT: {
                int numconc = op >> 8;
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc),
                    ((op & CODE_OP_MASK) == CODE_CONC) ? " " : ""
                );
                free_args(args, numargs, numargs - numconc);
                args[numargs].set_mstr(buf.get().iter());
                buf.get().disown();
                force_arg(args[numargs], op & CODE_RET_MASK);
                numargs++;
                continue;
            }

            case CODE_CONCM | RET_NULL:
            case CODE_CONCM | RET_STR:
            case CODE_CONCM | RET_FLOAT:
            case CODE_CONCM | RET_INT: {
                int numconc = op >> 8;
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc)
                );
                free_args(args, numargs, numargs - numconc);
                result.set_mstr(buf.get().iter());
                buf.get().disown();
                force_arg(result, op & CODE_RET_MASK);
                continue;
            }

            case CODE_ALIAS:
                static_cast<CsAlias *>(cs.identmap[op >> 8])->set_alias(
                    cs, args[--numargs]
                );
                continue;
            case CODE_ALIASARG:
                static_cast<CsAlias *>(cs.identmap[op >> 8])->set_arg(
                    cs, args[--numargs]
                );
                continue;
            case CODE_ALIASU:
                numargs -= 2;
                cs.set_alias(args[numargs].get_str(), args[numargs + 1]);
                args[numargs].cleanup();
                continue;

            case CODE_CALL | RET_NULL:
            case CODE_CALL | RET_STR:
            case CODE_CALL | RET_FLOAT:
            case CODE_CALL | RET_INT: {
                result.force_null();
                CsIdent *id = cs.identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (id->get_flags() & IDF_UNKNOWN) {
                    cs_debug_code(cs, "unknown command: %s", id->get_name());
                    free_args(args, numargs, offset);
                    force_arg(result, op & CODE_RET_MASK);
                    continue;
                }
                cs_call_alias(
                    cs, static_cast<CsAlias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }
            case CODE_CALLARG | RET_NULL:
            case CODE_CALLARG | RET_STR:
            case CODE_CALLARG | RET_FLOAT:
            case CODE_CALLARG | RET_INT: {
                result.force_null();
                CsIdent *id = cs.identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (!(cs.p_stack->usedargs & (1 << id->get_index()))) {
                    free_args(args, numargs, offset);
                    force_arg(result, op & CODE_RET_MASK);
                    continue;
                }
                cs_call_alias(
                    cs, static_cast<CsAlias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }

            case CODE_CALLU | RET_NULL:
            case CODE_CALLU | RET_STR:
            case CODE_CALLU | RET_FLOAT:
            case CODE_CALLU | RET_INT: {
                int callargs = op >> 8, offset = numargs - callargs;
                CsValue &idarg = args[offset - 1];
                if (
                    idarg.get_type() != VAL_STR &&
                    idarg.get_type() != VAL_MACRO &&
                    idarg.get_type() != VAL_CSTR
                ) {
litval:
                    result.cleanup();
                    result = idarg;
                    force_arg(result, op & CODE_RET_MASK);
                    while (--numargs >= offset) {
                        args[numargs].cleanup();
                    }
                    continue;
                }
                CsIdent *id = cs.get_ident(idarg.s);
                if (!id) {
noid:
                    if (cs_check_num(idarg.s)) {
                        goto litval;
                    }
                    cs_debug_code(cs, "unknown command: %s", idarg.s);
                    result.force_null();
                    free_args(args, numargs, offset - 1);
                    force_arg(result, op & CODE_RET_MASK);
                    continue;
                }
                result.force_null();
                switch (id->get_type_raw()) {
                    default:
                        if (!cs_has_cmd_cb(id)) {
                            free_args(args, numargs, offset - 1);
                            force_arg(result, op & CODE_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND:
                        idarg.cleanup();
                        callcommand(
                            cs, static_cast<Command *>(id), &args[offset],
                            result, callargs
                        );
                        force_arg(result, op & CODE_RET_MASK);
                        numargs = offset - 1;
                        continue;
                    case ID_LOCAL: {
                        CsIdentStack locals[MaxArguments];
                        idarg.cleanup();
                        for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) {
                            cs_push_alias(cs.force_ident(
                                args[offset + j]
                            ), locals[j]);
                        }
                        code = runcode(cs, code, result);
                        for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) {
                            cs_pop_alias(args[offset + j].id);
                        }
                        goto exit;
                    }
                    case ID_IVAR:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsIvar *>(id));
                        } else {
                            cs.set_var_int_checked(
                                static_cast<CsIvar *>(id),
                                ostd::iter(&args[offset], callargs)
                            );
                        }
                        free_args(args, numargs, offset - 1);
                        force_arg(result, op & CODE_RET_MASK);
                        continue;
                    case ID_FVAR:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsFvar *>(id));
                        } else {
                            cs.set_var_float_checked(
                                static_cast<CsFvar *>(id),
                                args[offset].force_float()
                            );
                        }
                        free_args(args, numargs, offset - 1);
                        force_arg(result, op & CODE_RET_MASK);
                        continue;
                    case ID_SVAR:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsSvar *>(id));
                        } else {
                            cs.set_var_str_checked(
                                static_cast<CsSvar *>(id),
                                args[offset].force_str()
                            );
                        }
                        free_args(args, numargs, offset - 1);
                        force_arg(result, op & CODE_RET_MASK);
                        continue;
                    case ID_ALIAS: {
                        CsAlias *a = static_cast<CsAlias *>(id);
                        if (
                            a->get_index() < MaxArguments &&
                            !(cs.p_stack->usedargs & (1 << a->get_index()))
                        ) {
                            free_args(args, numargs, offset - 1);
                            force_arg(result, op & CODE_RET_MASK);
                            continue;
                        }
                        if (a->val_v.get_type() == VAL_NULL) {
                            goto noid;
                        }
                        idarg.cleanup();
                        cs_call_alias(
                            cs, a, args, result, callargs, numargs,
                            offset, 1, op
                        );
                        continue;
                    }
                }
            }
        }
    }
exit:
    --rundepth;
    return code;
}

void CsState::run_ret(CsBytecode const *code, CsValue &ret) {
    runcode(*this, reinterpret_cast<ostd::Uint32 const *>(code), ret);
}

void CsState::run_ret(ostd::ConstCharRange code, CsValue &ret) {
    GenState gs(*this);
    gs.code.reserve(64);
    /* FIXME range */
    gs.gen_main(code.data(), VAL_ANY);
    runcode(*this, gs.code.data() + 1, ret);
    if (int(gs.code[0]) >= 0x100) {
        gs.code.disown();
    }
}

void CsState::run_ret(CsIdent *id, CsValueRange args, CsValue &ret) {
    int nargs = int(args.size());
    ret.set_null();
    ++rundepth;
    if (rundepth > MaxRunDepth) {
        cs_debug_code(*this, "exceeded recursion limit");
    } else if (id) {
        switch (id->get_type()) {
            default:
                if (!cs_has_cmd_cb(id)) {
                    break;
                }
            /* fallthrough */
            case CsIdentType::command:
                if (nargs < static_cast<Command *>(id)->numargs) {
                    CsValue buf[MaxArguments];
                    memcpy(buf, args.data(), args.size() * sizeof(CsValue));
                    callcommand(
                        *this, static_cast<Command *>(id), buf, ret,
                        nargs, false
                    );
                } else {
                    callcommand(
                        *this, static_cast<Command *>(id), args.data(),
                        ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            case CsIdentType::ivar:
                if (args.empty()) {
                    print_var(static_cast<CsIvar *>(id));
                } else {
                    set_var_int_checked(static_cast<CsIvar *>(id), args);
                }
                break;
            case CsIdentType::fvar:
                if (args.empty()) {
                    print_var(static_cast<CsFvar *>(id));
                } else {
                    set_var_float_checked(
                        static_cast<CsFvar *>(id), args[0].force_float()
                    );
                }
                break;
            case CsIdentType::svar:
                if (args.empty()) {
                    print_var(static_cast<CsSvar *>(id));
                } else {
                    set_var_str_checked(
                        static_cast<CsSvar *>(id), args[0].force_str()
                    );
                }
                break;
            case CsIdentType::alias: {
                CsAlias *a = static_cast<CsAlias *>(id);
                if (a->get_index() < MaxArguments) {
                    if (!(p_stack->usedargs & (1 << a->get_index()))) {
                        break;
                    }
                }
                if (a->val_v.get_type() == VAL_NULL) {
                    break;
                }
                cs_call_alias(
                    *this, a, args.data(), ret, nargs, nargs, 0, 0, RET_NULL
                );
                break;
            }
        }
    }
    free_args(args.data(), nargs, 0);
    --rundepth;
}

CsString CsState::run_str(CsBytecode const *code) {
    CsValue ret;
    run_ret(code, ret);
    CsString s = ret.get_str();
    ret.cleanup();
    return s;
}

CsString CsState::run_str(ostd::ConstCharRange code) {
    CsValue ret;
    run_ret(code, ret);
    CsString s = ret.get_str();
    ret.cleanup();
    return s;
}

CsString CsState::run_str(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run_ret(id, args, ret);
    CsString s = ret.get_str();
    ret.cleanup();
    return s;
}

CsInt CsState::run_int(CsBytecode const *code) {
    CsValue ret;
    run_ret(code, ret);
    CsInt i = ret.get_int();
    ret.cleanup();
    return i;
}

CsInt CsState::run_int(ostd::ConstCharRange code) {
    CsValue ret;
    run_ret(code, ret);
    CsInt i = ret.get_int();
    ret.cleanup();
    return i;
}

CsInt CsState::run_int(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run_ret(id, args, ret);
    CsInt i = ret.get_int();
    ret.cleanup();
    return i;
}

CsFloat CsState::run_float(CsBytecode const *code) {
    CsValue ret;
    run_ret(code, ret);
    CsFloat f = ret.get_float();
    ret.cleanup();
    return f;
}

CsFloat CsState::run_float(ostd::ConstCharRange code) {
    CsValue ret;
    run_ret(code, ret);
    CsFloat f = ret.get_float();
    ret.cleanup();
    return f;
}

CsFloat CsState::run_float(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run_ret(id, args, ret);
    CsFloat f = ret.get_float();
    ret.cleanup();
    return f;
}

bool CsState::run_bool(CsBytecode const *code) {
    CsValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(ostd::ConstCharRange code) {
    CsValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run_ret(id, args, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

void CsState::run(CsBytecode const *code) {
    CsValue ret;
    run_ret(code, ret);
    ret.cleanup();
}

void CsState::run(ostd::ConstCharRange code) {
    CsValue ret;
    run_ret(code, ret);
    ret.cleanup();
}

void CsState::run(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run_ret(id, args, ret);
    ret.cleanup();
}

static bool cs_run_file(
    CsState &cs, ostd::ConstCharRange fname, CsValue &ret
) {
    ostd::ConstCharRange old_src_file = cs_src_file, old_src_str = cs_src_str;
    ostd::Box<char[]> buf;
    ostd::Size len;

    ostd::FileStream f(fname, ostd::StreamMode::read);
    if (!f.is_open()) {
        return false;
    }

    len = f.size();
    buf = ostd::make_box<char[]>(len + 1);
    if (!buf || f.get(buf.get(), len) != len) {
        return false;
    }
    buf[len] = '\0';

    ostd::ConstCharRange src_str = ostd::ConstCharRange(buf.get(), len);
    cs_src_file = fname;
    cs_src_str = src_str;
    cs.run_ret(src_str, ret);
    cs_src_file = old_src_file;
    cs_src_str = old_src_str;
    return true;
}

ostd::Maybe<CsString> CsState::run_file_str(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    CsString s = ret.get_str();
    ret.cleanup();
    return ostd::move(s);
}

ostd::Maybe<CsInt> CsState::run_file_int(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    CsInt i = ret.get_int();
    ret.cleanup();
    return i;
}

ostd::Maybe<CsFloat> CsState::run_file_float(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    CsFloat f = ret.get_float();
    ret.cleanup();
    return f;
}

ostd::Maybe<bool> CsState::run_file_bool(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    bool i = ret.get_bool();
    ret.cleanup();
    return i;
}

bool CsState::run_file_ret(ostd::ConstCharRange fname, CsValue &ret) {
    return cs_run_file(*this, fname, ret);
}

bool CsState::run_file(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return false;
    }
    ret.cleanup();
    return true;
}

} /* namespace cscript */
