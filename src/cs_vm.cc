#include "cubescript/cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

#include <ostd/memory.hh>

namespace cscript {

struct CsCommandInternal {
    static void call(
        CsState &cs, CsCommand *c, CsValueRange args, CsValue &ret
    ) {
        c->p_cb_cftv(cs, args, ret);
    }

    static bool has_cb(CsIdent *id) {
        if (!id->is_command() && !id->is_special()) {
            return false;
        }
        CsCommand *cb = static_cast<CsCommand *>(id);
        return !!cb->p_cb_cftv;
    }
};

static inline void cs_push_alias(CsIdent *id, CsIdentStack &st) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        CsValue nv;
        CsAliasInternal::push_arg(static_cast<CsAlias *>(id), nv, st);
    }
}

static inline void cs_pop_alias(CsIdent *id) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        CsAliasInternal::pop_arg(static_cast<CsAlias *>(id));
    }
}

CsStackState::CsStackState(CsState &cs, CsStackStateNode *nd, bool gap):
    p_state(cs), p_node(nd), p_gap(gap)
{}
CsStackState::CsStackState(CsStackState &&st):
    p_state(st.p_state), p_node(st.p_node), p_gap(st.p_gap)
{
    st.p_node = nullptr;
    st.p_gap = false;
}

CsStackState::~CsStackState() {
    ostd::Size len = 0;
    for (CsStackStateNode const *nd = p_node; nd; nd = nd->next) {
        ++len;
    }
    p_state.p_state->destroy_array(p_node, len);
}

CsStackState &CsStackState::operator=(CsStackState &&st) {
    p_node = st.p_node;
    p_gap = st.p_gap;
    st.p_node = nullptr;
    st.p_gap = false;
    return *this;
}

CsStackStateNode const *CsStackState::get() const {
    return p_node;
}

bool CsStackState::gap() const {
    return p_gap;
}

CsStackState CsErrorException::save_stack(CsState &cs) {
    CsIvar *dalias = static_cast<CsIvar *>(cs.p_state->identmap[DbgaliasIdx]);
    if (!dalias->get_value()) {
        return CsStackState(cs, nullptr, !!cs.p_callstack);
    }
    int total = 0, depth = 0;
    for (CsIdentLink *l = cs.p_callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return CsStackState(cs, nullptr, false);
    }
    CsStackStateNode *st = cs.p_state->create_array<CsStackStateNode>(
        ostd::min(total, dalias->get_value())
    );
    CsStackStateNode *ret = st, *nd = st;
    ++st;
    for (CsIdentLink *l = cs.p_callstack; l; l = l->next) {
        ++depth;
        if (depth < dalias->get_value()) {
            nd->id = l->id;
            nd->index = total - depth + 1;
            if (!l->next) {
                nd->next = nullptr;
            } else {
                nd->next = st;
            }
            nd = st++;
        } else if (!l->next) {
            nd->id = l->id;
            nd->index = 1;
            nd->next = nullptr;
        }
    }
    return CsStackState(cs, ret, total > dalias->get_value());
}

ostd::ConstCharRange CsErrorException::save_msg(
    CsState &cs, ostd::ConstCharRange msg
) {
    if (msg.size() > sizeof(cs.p_errbuf)) {
        msg = msg.slice(0, sizeof(cs.p_errbuf));
    }
    GenState *gs = cs.p_pstate;
    if (gs) {
        /* we can attach line number */
        ostd::CharRange r(cs.p_errbuf, sizeof(cs.p_errbuf));
        ostd::Ptrdiff sz = -1;
        if (!gs->src_name.empty()) {
            sz = ostd::format(r, "%s:%d: %s", gs->src_name, gs->current_line, msg);
        } else {
            sz = ostd::format(r, "%d: %s", gs->current_line, msg);
        }
        if (sz > 0) {
            return ostd::ConstCharRange(cs.p_errbuf, sz);
        }
    }
    memcpy(cs.p_errbuf, msg.data(), msg.size());
    return ostd::ConstCharRange(cs.p_errbuf, msg.size());
}

static void bcode_ref(ostd::Uint32 *code) {
    if (!code) {
        return;
    }
    if ((*code & CsCodeOpMask) == CsCodeStart) {
        bcode_incr(code);
        return;
    }
    switch (code[-1]&CsCodeOpMask) {
        case CsCodeStart:
            bcode_incr(&code[-1]);
            break;
        case CsCodeOffset:
            code -= ostd::Ptrdiff(code[-1] >> 8);
            bcode_incr(code);
            break;
    }
}

static void bcode_unref(ostd::Uint32 *code) {
    if (!code) {
        return;
    }
    if ((*code & CsCodeOpMask) == CsCodeStart) {
        bcode_decr(code);
        return;
    }
    switch (code[-1]&CsCodeOpMask) {
        case CsCodeStart:
            bcode_decr(&code[-1]);
            break;
        case CsCodeOffset:
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

static inline ostd::Uint32 *forcecode(CsState &cs, CsValue &v) {
    ostd::Uint32 *code = reinterpret_cast<ostd::Uint32 *>(v.get_code());
    if (!code) {
        GenState gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        gs.done();
        v.set_code(reinterpret_cast<CsBytecode *>(gs.code.release() + 1));
        code = reinterpret_cast<ostd::Uint32 *>(v.get_code());
    }
    return code;
}

static inline void forcecond(CsState &cs, CsValue &v) {
    switch (v.get_type()) {
        case CsValueType::String:
        case CsValueType::Macro:
        case CsValueType::Cstring:
            if (!v.get_strr().empty()) {
                forcecode(cs, v);
            } else {
                v.set_int(0);
            }
            break;
        default:
            break;
    }
}

static ostd::Uint32 emptyblock[CsValAny][2] = {
    { CsCodeStart + 0x100, CsCodeExit | CsRetNull },
    { CsCodeStart + 0x100, CsCodeExit | CsRetInt },
    { CsCodeStart + 0x100, CsCodeExit | CsRetFloat },
    { CsCodeStart + 0x100, CsCodeExit | CsRetString }
};

static inline void force_arg(CsValue &v, int type) {
    switch (type) {
        case CsRetString:
            if (v.get_type() != CsValueType::String) {
                v.force_str();
            }
            break;
        case CsRetInt:
            if (v.get_type() != CsValueType::Int) {
                v.force_int();
            }
            break;
        case CsRetFloat:
            if (v.get_type() != CsValueType::Float) {
                v.force_float();
            }
            break;
    }
}

static ostd::Uint32 *skipcode(ostd::Uint32 *code) {
    int depth = 0;
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
            case CsCodeMacro:
            case CsCodeVal | CsRetString: {
                ostd::Uint32 len = op >> 8;
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }
            case CsCodeBlock:
            case CsCodeJump:
            case CsCodeJumpB | CsCodeFlagTrue:
            case CsCodeJumpB | CsCodeFlagFalse:
            case CsCodeJumpResult | CsCodeFlagTrue:
            case CsCodeJumpResult | CsCodeFlagFalse: {
                ostd::Uint32 len = op >> 8;
                code += len;
                continue;
            }
            case CsCodeEnter:
            case CsCodeEnterResult:
                ++depth;
                continue;
            case CsCodeExit | CsRetNull:
            case CsCodeExit | CsRetString:
            case CsCodeExit | CsRetInt:
            case CsCodeExit | CsRetFloat:
                if (depth <= 0) {
                    return code;
                }
                --depth;
                continue;
        }
    }
}

CsBytecode *cs_copy_code(CsBytecode *c) {
    ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(c);
    ostd::Uint32 *end = skipcode(bcode);
    ostd::Uint32 *dst = new ostd::Uint32[end - bcode + 1];
    *dst++ = CsCodeStart;
    memcpy(dst, bcode, (end - bcode) * sizeof(ostd::Uint32));
    return reinterpret_cast<CsBytecode *>(dst);
}

static inline void callcommand(
    CsState &cs, CsCommand *id, CsValue *args, CsValue &res, int numargs,
    bool lookup = false
) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (auto fmt = id->get_args(); !fmt.empty(); ++fmt) {
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
                        reinterpret_cast<CsBytecode *>(emptyblock[CsValNull] + 1)
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
                    args[i].set_ident(cs.p_state->identmap[DummyIdx]);
                    fakeargs++;
                } else {
                    cs.force_ident(args[i]);
                }
                break;
            case '$':
                i += 1;
                args[i].set_ident(id);
                break;
            case 'N':
                i += 1;
                args[i].set_int(CsInt(lookup ? -1 : i - fakeargs));
                break;
            case 'C': {
                i = ostd::max(i + 1, numargs);
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(buf, ostd::iter(args, i), " ");
                CsValue tv;
                tv.set_str(ostd::move(buf.get()));
                CsCommandInternal::call(cs, id, CsValueRange(&tv, 1), res);
                return;
            }
            case 'V':
                i = ostd::max(i + 1, numargs);
                CsCommandInternal::call(cs, id, ostd::iter(args, i), res);
                return;
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
    CsCommandInternal::call(cs, id, CsValueRange(args, i), res);
}

static ostd::Uint32 *runcode(CsState &cs, ostd::Uint32 *code, CsValue &result);

static inline void cs_call_alias(
    CsState &cs, CsAlias *a, CsValue *args, CsValue &result,
    int callargs, int &nargs, int offset, int skip, ostd::Uint32 op
) {
    CsIvar *anargs = static_cast<CsIvar *>(cs.p_state->identmap[NumargsIdx]);
    CsIdentStack argstack[MaxArguments];
    for(int i = 0; i < callargs; i++) {
        CsAliasInternal::push_arg(
            static_cast<CsAlias *>(cs.p_state->identmap[i]),
            args[offset + i], argstack[i], false
        );
    }
    int oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = cs.identflags;
    cs.identflags |= a->get_flags()&CsIdfOverridden;
    CsIdentLink aliaslink = {
        a, cs.p_callstack, (1<<callargs)-1, argstack
    };
    cs.p_callstack = &aliaslink;
    ostd::Uint32 *codep = reinterpret_cast<ostd::Uint32 *>(
        CsAliasInternal::compile_code(a, cs)
    );
    bcode_incr(codep);
    cs_do_and_cleanup([&]() {
        runcode(cs, codep+1, result);
    }, [&]() {
        bcode_decr(codep);
        cs.p_callstack = aliaslink.next;
        cs.identflags = oldflags;
        for (int i = 0; i < callargs; i++) {
            CsAliasInternal::pop_arg(
                static_cast<CsAlias *>(cs.p_state->identmap[i])
            );
        }
        int argmask = aliaslink.usedargs & (~0 << callargs);
        for (; argmask; ++callargs) {
            if (argmask & (1 << callargs)) {
                CsAliasInternal::pop_arg(static_cast<CsAlias *>(
                    cs.p_state->identmap[callargs])
                );
                argmask &= ~(1 << callargs);
            }
        }
        force_arg(result, op & CsCodeRetMask);
        anargs->set_value(oldargs);
        nargs = offset - skip;
    });
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

struct RunDepthRef {
    RunDepthRef() = delete;
    RunDepthRef(CsState &cs) {
        if (rundepth >= MaxRunDepth) {
            throw CsErrorException(cs, "exceeded recursion limit");
        }
        ++rundepth;
    }
    RunDepthRef(RunDepthRef const &) = delete;
    RunDepthRef(RunDepthRef &&) = delete;
    ~RunDepthRef() { --rundepth; }
};

static inline CsAlias *cs_get_lookup_id(CsState &cs, ostd::Uint32 op) {
    CsIdent *id = cs.p_state->identmap[op >> 8];
    if (id->get_flags() & CsIdfUnknown) {
        throw CsErrorException(cs, "unknown alias lookup: %s", id->get_name());
    }
    return static_cast<CsAlias *>(id);
}

static inline CsAlias *cs_get_lookuparg_id(CsState &cs, ostd::Uint32 op) {
    CsIdent *id = cs.p_state->identmap[op >> 8];
    if (!cs_is_arg_used(cs, id)) {
        return nullptr;
    }
    return static_cast<CsAlias *>(id);
}

static inline int cs_get_lookupu_type(
    CsState &cs, CsValue &arg, CsIdent *&id, ostd::Uint32 op
) {
    if (
        arg.get_type() != CsValueType::String &&
        arg.get_type() != CsValueType::Macro &&
        arg.get_type() != CsValueType::Cstring
    ) {
        return -2; /* default case */
    }
    id = cs.get_ident(arg.get_strr());
    if (id) {
        switch(id->get_type()) {
            case CsIdentType::Alias:
                if (id->get_flags() & CsIdfUnknown) {
                    break;
                }
                if ((id->get_index() < MaxArguments) && !cs_is_arg_used(cs, id)) {
                    return CsIdUnknown;
                }
                return CsIdAlias;
            case CsIdentType::Svar:
                return CsIdSvar;
            case CsIdentType::Ivar:
                return CsIdIvar;
            case CsIdentType::Fvar:
                return CsIdFvar;
            case CsIdentType::Command: {
                arg.set_null();
                CsValue buf[MaxArguments];
                callcommand(cs, static_cast<CsCommand *>(id), buf, arg, 0, true);
                force_arg(arg, op & CsCodeRetMask);
                return -2; /* ignore */
            }
            default:
                return CsIdUnknown;
        }
    }
    throw CsErrorException(cs, "unknown alias lookup: %s", arg.get_strr());
}

static ostd::Uint32 *runcode(CsState &cs, ostd::Uint32 *code, CsValue &result) {
    result.set_null();
    RunDepthRef level{cs}; /* incr and decr on scope exit */
    int numargs = 0;
    CsValue args[MaxArguments + MaxResults];
    auto &chook = cs.get_call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
            case CsCodeStart:
            case CsCodeOffset:
                continue;

            case CsCodeNull | CsRetNull:
                result.set_null();
                continue;
            case CsCodeNull | CsRetString:
                result.set_str("");
                continue;
            case CsCodeNull | CsRetInt:
                result.set_int(0);
                continue;
            case CsCodeNull | CsRetFloat:
                result.set_float(0.0f);
                continue;

            case CsCodeFalse | CsRetString:
                result.set_str("0");
                continue;
            case CsCodeFalse | CsRetNull:
            case CsCodeFalse | CsRetInt:
                result.set_int(0);
                continue;
            case CsCodeFalse | CsRetFloat:
                result.set_float(0.0f);
                continue;

            case CsCodeTrue | CsRetString:
                result.set_str("1");
                continue;
            case CsCodeTrue | CsRetNull:
            case CsCodeTrue | CsRetInt:
                result.set_int(1);
                continue;
            case CsCodeTrue | CsRetFloat:
                result.set_float(1.0f);
                continue;

            case CsCodeNot | CsRetString:
                --numargs;
                result.set_str(args[numargs].get_bool() ? "0" : "1");
                continue;
            case CsCodeNot | CsRetNull:
            case CsCodeNot | CsRetInt:
                --numargs;
                result.set_int(!args[numargs].get_bool());
                continue;
            case CsCodeNot | CsRetFloat:
                --numargs;
                result.set_float(CsFloat(!args[numargs].get_bool()));
                continue;

            case CsCodePop:
                numargs -= 1;
                continue;
            case CsCodeEnter:
                code = runcode(cs, code, args[numargs++]);
                continue;
            case CsCodeEnterResult:
                code = runcode(cs, code, result);
                continue;
            case CsCodeExit | CsRetString:
            case CsCodeExit | CsRetInt:
            case CsCodeExit | CsRetFloat:
                force_arg(result, op & CsCodeRetMask);
            /* fallthrough */
            case CsCodeExit | CsRetNull:
                return code;
            case CsCodeResultArg | CsRetString:
            case CsCodeResultArg | CsRetInt:
            case CsCodeResultArg | CsRetFloat:
                force_arg(result, op & CsCodeRetMask);
            /* fallthrough */
            case CsCodeResultArg | CsRetNull:
                args[numargs++] = ostd::move(result);
                continue;
            case CsCodePrint:
                cs.print_var(static_cast<CsVar *>(cs.p_state->identmap[op >> 8]));
                continue;

            case CsCodeLocal: {
                int numlocals = op >> 8, offset = numargs - numlocals;
                CsIdentStack locals[MaxArguments];
                for (int i = 0; i < numlocals; ++i) {
                    cs_push_alias(args[offset + i].get_ident(), locals[i]);
                }
                cs_do_and_cleanup([&]() {
                    code = runcode(cs, code, result);
                }, [&]() {
                    for (int i = offset; i < numargs; i++) {
                        cs_pop_alias(args[i].get_ident());
                    }
                });
                return code;
            }

            case CsCodeDoArgs | CsRetNull:
            case CsCodeDoArgs | CsRetString:
            case CsCodeDoArgs | CsRetInt:
            case CsCodeDoArgs | CsRetFloat:
                cs_do_args(cs, [&]() {
                    cs.run(args[--numargs].get_code(), result);
                    force_arg(result, op & CsCodeRetMask);
                });
                continue;
            /* fallthrough */
            case CsCodeDo | CsRetNull:
            case CsCodeDo | CsRetString:
            case CsCodeDo | CsRetInt:
            case CsCodeDo | CsRetFloat:
                cs.run(args[--numargs].get_code(), result);
                force_arg(result, op & CsCodeRetMask);
                continue;

            case CsCodeJump: {
                ostd::Uint32 len = op >> 8;
                code += len;
                continue;
            }
            case CsCodeJumpB | CsCodeFlagTrue: {
                ostd::Uint32 len = op >> 8;
                if (args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpB | CsCodeFlagFalse: {
                ostd::Uint32 len = op >> 8;
                if (!args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpResult | CsCodeFlagTrue: {
                ostd::Uint32 len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == CsValueType::Code) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = ostd::move(args[numargs]);
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpResult | CsCodeFlagFalse: {
                ostd::Uint32 len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == CsValueType::Code) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = ostd::move(args[numargs]);
                }
                if (!result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeBreak | CsCodeFlagFalse:
                if (cs.is_in_loop()) {
                    throw CsBreakException();
                } else {
                    throw CsErrorException(cs, "no loop to break");
                }
                break;
            case CsCodeBreak | CsCodeFlagTrue:
                if (cs.is_in_loop()) {
                    throw CsContinueException();
                } else {
                    throw CsErrorException(cs, "no loop to continue");
                }
                break;

            case CsCodeMacro: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_macro(ostd::ConstCharRange(
                    reinterpret_cast<char const *>(code), len
                ));
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }

            case CsCodeVal | CsRetString: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_str(ostd::ConstCharRange(
                    reinterpret_cast<char const *>(code), len
                ));
                code += len / sizeof(ostd::Uint32) + 1;
                continue;
            }
            case CsCodeValInt | CsRetString: {
                char s[4] = {
                    char((op >> 8) & 0xFF),
                    char((op >> 16) & 0xFF),
                    char((op >> 24) & 0xFF), '\0'
                };
                /* gotta cast or r.size() == potentially 3 */
                args[numargs++].set_str(static_cast<char const *>(s));
                continue;
            }
            case CsCodeVal | CsRetNull:
            case CsCodeValInt | CsRetNull:
                args[numargs++].set_null();
                continue;
            case CsCodeVal | CsRetInt:
                args[numargs++].set_int(
                    *reinterpret_cast<CsInt const *>(code)
                );
                code += CsTypeStorageSize<CsInt>;
                continue;
            case CsCodeValInt | CsRetInt:
                args[numargs++].set_int(CsInt(op) >> 8);
                continue;
            case CsCodeVal | CsRetFloat:
                args[numargs++].set_float(
                    *reinterpret_cast<CsFloat const *>(code)
                );
                code += CsTypeStorageSize<CsFloat>;
                continue;
            case CsCodeValInt | CsRetFloat:
                args[numargs++].set_float(CsFloat(CsInt(op) >> 8));
                continue;

            case CsCodeDup | CsRetNull:
                args[numargs - 1].get_val(args[numargs]);
                numargs++;
                continue;
            case CsCodeDup | CsRetInt:
                args[numargs].set_int(args[numargs - 1].get_int());
                numargs++;
                continue;
            case CsCodeDup | CsRetFloat:
                args[numargs].set_float(args[numargs - 1].get_float());
                numargs++;
                continue;
            case CsCodeDup | CsRetString:
                args[numargs].set_str(ostd::move(args[numargs - 1].get_str()));
                numargs++;
                continue;

            case CsCodeForce | CsRetString:
                args[numargs - 1].force_str();
                continue;
            case CsCodeForce | CsRetInt:
                args[numargs - 1].force_int();
                continue;
            case CsCodeForce | CsRetFloat:
                args[numargs - 1].force_float();
                continue;

            case CsCodeResult | CsRetNull:
                result = ostd::move(args[--numargs]);
                continue;
            case CsCodeResult | CsRetString:
            case CsCodeResult | CsRetInt:
            case CsCodeResult | CsRetFloat:
                result = ostd::move(args[--numargs]);
                force_arg(result, op & CsCodeRetMask);
                continue;

            case CsCodeEmpty | CsRetNull:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[CsValNull] + 1)
                );
                break;
            case CsCodeEmpty | CsRetString:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[CsValString] + 1)
                );
                break;
            case CsCodeEmpty | CsRetInt:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[CsValInt] + 1)
                );
                break;
            case CsCodeEmpty | CsRetFloat:
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(emptyblock[CsValFloat] + 1)
                );
                break;
            case CsCodeBlock: {
                ostd::Uint32 len = op >> 8;
                args[numargs++].set_code(
                    reinterpret_cast<CsBytecode *>(code + 1)
                );
                code += len;
                continue;
            }
            case CsCodeCompile: {
                CsValue &arg = args[numargs - 1];
                GenState gs(cs);
                switch (arg.get_type()) {
                    case CsValueType::Int:
                        gs.code.reserve(8);
                        gs.code.push(CsCodeStart);
                        gs.gen_int(arg.get_int());
                        gs.code.push(CsCodeResult);
                        gs.code.push(CsCodeExit);
                        break;
                    case CsValueType::Float:
                        gs.code.reserve(8);
                        gs.code.push(CsCodeStart);
                        gs.gen_float(arg.get_float());
                        gs.code.push(CsCodeResult);
                        gs.code.push(CsCodeExit);
                        break;
                    case CsValueType::String:
                    case CsValueType::Macro:
                    case CsValueType::Cstring:
                        gs.code.reserve(64);
                        gs.gen_main(arg.get_strr());
                        break;
                    default:
                        gs.code.reserve(8);
                        gs.code.push(CsCodeStart);
                        gs.gen_null();
                        gs.code.push(CsCodeResult);
                        gs.code.push(CsCodeExit);
                        break;
                }
                gs.done();
                arg.set_code(
                    reinterpret_cast<CsBytecode *>(gs.code.release() + 1)
                );
                continue;
            }
            case CsCodeCond: {
                CsValue &arg = args[numargs - 1];
                switch (arg.get_type()) {
                    case CsValueType::String:
                    case CsValueType::Macro:
                    case CsValueType::Cstring: {
                        ostd::ConstCharRange s = arg.get_strr();
                        if (!s.empty()) {
                            GenState gs(cs);
                            gs.code.reserve(64);
                            gs.gen_main(s);
                            gs.done();
                            arg.set_code(reinterpret_cast<CsBytecode *>(
                                gs.code.release() + 1
                            ));
                        } else {
                            arg.force_null();
                        }
                        break;
                    }
                    default:
                        break;
                }
                continue;
            }

            case CsCodeIdent:
                args[numargs++].set_ident(cs.p_state->identmap[op >> 8]);
                continue;
            case CsCodeIdentArg: {
                CsAlias *a = static_cast<CsAlias *>(
                    cs.p_state->identmap[op >> 8]
                );
                if (!cs_is_arg_used(cs, a)) {
                    CsValue nv;
                    CsAliasInternal::push_arg(
                        a, nv, cs.p_callstack->argstack[a->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << a->get_index();
                }
                args[numargs++].set_ident(a);
                continue;
            }
            case CsCodeIdentU: {
                CsValue &arg = args[numargs - 1];
                CsIdent *id = cs.p_state->identmap[DummyIdx];
                if (
                    arg.get_type() == CsValueType::String ||
                    arg.get_type() == CsValueType::Macro ||
                    arg.get_type() == CsValueType::Cstring
                ) {
                    id = cs.new_ident(arg.get_strr());
                }
                if ((id->get_index() < MaxArguments) && !cs_is_arg_used(cs, id)) {
                    CsValue nv;
                    CsAliasInternal::push_arg(
                        static_cast<CsAlias *>(id), nv,
                        cs.p_callstack->argstack[id->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << id->get_index();
                }
                arg.set_ident(id);
                continue;
            }

            case CsCodeLookupU | CsRetString: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_str(ostd::move(
                            static_cast<CsAlias *>(id)->get_value().get_str()
                        ));
                        continue;
                    case CsIdSvar:
                        arg.set_str(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_str(ostd::move(
                            intstr(static_cast<CsIvar *>(id)->get_value())
                        ));
                        continue;
                    case CsIdFvar:
                        arg.set_str(ostd::move(
                            floatstr(static_cast<CsFvar *>(id)->get_value())
                        ));
                        continue;
                    case CsIdUnknown:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookup | CsRetString:
                args[numargs++].set_str(
                    ostd::move(cs_get_lookup_id(cs, op)->get_value().get_str())
                );
                continue;
            case CsCodeLookupArg | CsRetString: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs++].set_str(
                        ostd::move(a->get_value().get_str())
                    );
                }
                continue;
            }
            case CsCodeLookupU | CsRetInt: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_int(
                            static_cast<CsAlias *>(id)->get_value().get_int()
                        );
                        continue;
                    case CsIdSvar:
                        arg.set_int(cs_parse_int(
                            static_cast<CsSvar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_int(
                            CsInt(static_cast<CsFvar *>(id)->get_value())
                        );
                        continue;
                    case CsIdUnknown:
                        arg.set_int(0);
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookup | CsRetInt:
                args[numargs++].set_int(
                    cs_get_lookup_id(cs, op)->get_value().get_int()
                );
                continue;
            case CsCodeLookupArg | CsRetInt: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_int(0);
                } else {
                    args[numargs++].set_int(a->get_value().get_int());
                }
                continue;
            }
            case CsCodeLookupU | CsRetFloat: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_float(
                            static_cast<CsAlias *>(id)->get_value().get_float()
                        );
                        continue;
                    case CsIdSvar:
                        arg.set_float(cs_parse_float(
                            static_cast<CsSvar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdIvar:
                        arg.set_float(CsFloat(
                            static_cast<CsIvar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdFvar:
                        arg.set_float(
                            static_cast<CsFvar *>(id)->get_value()
                        );
                        continue;
                    case CsIdUnknown:
                        arg.set_float(CsFloat(0));
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookup | CsRetFloat:
                args[numargs++].set_float(
                    cs_get_lookup_id(cs, op)->get_value().get_float()
                );
                continue;
            case CsCodeLookupArg | CsRetFloat: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_float(CsFloat(0));
                } else {
                    args[numargs++].set_float(a->get_value().get_float());
                }
                continue;
            }
            case CsCodeLookupU | CsRetNull: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<CsAlias *>(id)->get_value().get_val(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_str(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_float(
                            static_cast<CsFvar *>(id)->get_value()
                        );
                        continue;
                    case CsIdUnknown:
                        arg.set_null();
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookup | CsRetNull:
                cs_get_lookup_id(cs, op)->get_value().get_val(args[numargs++]);
                continue;
            case CsCodeLookupArg | CsRetNull: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->get_value().get_val(args[numargs++]);
                }
                continue;
            }

            case CsCodeLookupMu | CsRetString: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<CsAlias *>(id)->get_cstr(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_cstr(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_str(ostd::move(
                            intstr(static_cast<CsIvar *>(id)->get_value())
                        ));
                        continue;
                    case CsIdFvar:
                        arg.set_str(ostd::move(
                            floatstr(static_cast<CsFvar *>(id)->get_value())
                        ));
                        continue;
                    case CsIdUnknown:
                        arg.set_cstr("");
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookupM | CsRetString:
                cs_get_lookup_id(cs, op)->get_cstr(args[numargs++]);
                continue;
            case CsCodeLookupMarg | CsRetString: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_cstr("");
                } else {
                    a->get_cstr(args[numargs++]);
                }
                continue;
            }
            case CsCodeLookupMu | CsRetNull: {
                CsIdent *id = nullptr;
                CsValue &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<CsAlias *>(id)->get_cval(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_cstr(static_cast<CsSvar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<CsIvar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_float(static_cast<CsFvar *>(id)->get_value());
                        continue;
                    case CsIdUnknown:
                        arg.set_null();
                        continue;
                    default:
                        continue;
                }
            }
            case CsCodeLookupM | CsRetNull:
                cs_get_lookup_id(cs, op)->get_cval(args[numargs++]);
                continue;
            case CsCodeLookupMarg | CsRetNull: {
                CsAlias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->get_cval(args[numargs++]);
                }
                continue;
            }

            case CsCodeSvar | CsRetString:
            case CsCodeSvar | CsRetNull:
                args[numargs++].set_str(static_cast<CsSvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeSvar | CsRetInt:
                args[numargs++].set_int(cs_parse_int(static_cast<CsSvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeSvar | CsRetFloat:
                args[numargs++].set_float(cs_parse_float(static_cast<CsSvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeSvarM:
                args[numargs++].set_cstr(static_cast<CsSvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeSvar1:
                cs.set_var_str_checked(
                    static_cast<CsSvar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_strr()
                );
                continue;

            case CsCodeIvar | CsRetInt:
            case CsCodeIvar | CsRetNull:
                args[numargs++].set_int(static_cast<CsIvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeIvar | CsRetString:
                args[numargs++].set_str(ostd::move(intstr(static_cast<CsIvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value())));
                continue;
            case CsCodeIvar | CsRetFloat:
                args[numargs++].set_float(CsFloat(static_cast<CsIvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeIvar1:
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_int()
                );
                continue;
            case CsCodeIvar2:
                numargs -= 2;
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                );
                continue;
            case CsCodeIvar3:
                numargs -= 3;
                cs.set_var_int_checked(
                    static_cast<CsIvar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                        | (args[numargs + 2].get_int()));
                continue;

            case CsCodeFvar | CsRetFloat:
            case CsCodeFvar | CsRetNull:
                args[numargs++].set_float(static_cast<CsFvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeFvar | CsRetString:
                args[numargs++].set_str(ostd::move(floatstr(
                    static_cast<CsFvar *>(
                        cs.p_state->identmap[op >> 8]
                    )->get_value()
                )));
                continue;
            case CsCodeFvar | CsRetInt:
                args[numargs++].set_int(int(static_cast<CsFvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeFvar1:
                cs.set_var_float_checked(
                    static_cast<CsFvar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_float()
                );
                continue;

            case CsCodeCom | CsRetNull:
            case CsCodeCom | CsRetString:
            case CsCodeCom | CsRetFloat:
            case CsCodeCom | CsRetInt: {
                CsCommand *id = static_cast<CsCommand *>(
                    cs.p_state->identmap[op >> 8]
                );
                int offset = numargs - id->get_num_args();
                result.force_null();
                CsCommandInternal::call(
                    cs, id, CsValueRange(args + offset, id->get_num_args()),
                    result
                );
                force_arg(result, op & CsCodeRetMask);
                numargs = offset;
                continue;
            }

            case CsCodeComV | CsRetNull:
            case CsCodeComV | CsRetString:
            case CsCodeComV | CsRetFloat:
            case CsCodeComV | CsRetInt: {
                CsCommand *id = static_cast<CsCommand *>(
                    cs.p_state->identmap[op >> 13]
                );
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                CsCommandInternal::call(
                    cs, id, ostd::iter(&args[offset], callargs), result
                );
                force_arg(result, op & CsCodeRetMask);
                numargs = offset;
                continue;
            }
            case CsCodeComC | CsRetNull:
            case CsCodeComC | CsRetString:
            case CsCodeComC | CsRetFloat:
            case CsCodeComC | CsRetInt: {
                CsCommand *id = static_cast<CsCommand *>(
                    cs.p_state->identmap[op >> 13]
                );
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                {
                    auto buf = ostd::appender<CsString>();
                    cscript::util::tvals_concat(
                        buf, ostd::iter(&args[offset], callargs), " "
                    );
                    CsValue tv;
                    tv.set_str(ostd::move(buf.get()));
                    CsCommandInternal::call(cs, id, CsValueRange(&tv, 1), result);
                }
                force_arg(result, op & CsCodeRetMask);
                numargs = offset;
                continue;
            }

            case CsCodeConc | CsRetNull:
            case CsCodeConc | CsRetString:
            case CsCodeConc | CsRetFloat:
            case CsCodeConc | CsRetInt:
            case CsCodeConcW | CsRetNull:
            case CsCodeConcW | CsRetString:
            case CsCodeConcW | CsRetFloat:
            case CsCodeConcW | CsRetInt: {
                int numconc = op >> 8;
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc),
                    ((op & CsCodeOpMask) == CsCodeConc) ? " " : ""
                );
                numargs = numargs - numconc;
                args[numargs].set_str(ostd::move(buf.get()));
                force_arg(args[numargs], op & CsCodeRetMask);
                numargs++;
                continue;
            }

            case CsCodeConcM | CsRetNull:
            case CsCodeConcM | CsRetString:
            case CsCodeConcM | CsRetFloat:
            case CsCodeConcM | CsRetInt: {
                int numconc = op >> 8;
                auto buf = ostd::appender<CsString>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc)
                );
                numargs = numargs - numconc;
                result.set_str(ostd::move(buf.get()));
                force_arg(result, op & CsCodeRetMask);
                continue;
            }

            case CsCodeAlias:
                CsAliasInternal::set_alias(
                    static_cast<CsAlias *>(cs.p_state->identmap[op >> 8]),
                    cs, args[--numargs]
                );
                continue;
            case CsCodeAliasArg:
                CsAliasInternal::set_arg(
                    static_cast<CsAlias *>(cs.p_state->identmap[op >> 8]),
                    cs, args[--numargs]
                );
                continue;
            case CsCodeAliasU:
                numargs -= 2;
                cs.set_alias(
                    args[numargs].get_str(), ostd::move(args[numargs + 1])
                );
                continue;

            case CsCodeCall | CsRetNull:
            case CsCodeCall | CsRetString:
            case CsCodeCall | CsRetFloat:
            case CsCodeCall | CsRetInt: {
                result.force_null();
                CsIdent *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (id->get_flags() & CsIdfUnknown) {
                    force_arg(result, op & CsCodeRetMask);
                    throw CsErrorException(
                        cs, "unknown command: %s", id->get_name()
                    );
                }
                cs_call_alias(
                    cs, static_cast<CsAlias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }
            case CsCodeCallArg | CsRetNull:
            case CsCodeCallArg | CsRetString:
            case CsCodeCallArg | CsRetFloat:
            case CsCodeCallArg | CsRetInt: {
                result.force_null();
                CsIdent *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (!cs_is_arg_used(cs, id)) {
                    numargs = offset;
                    force_arg(result, op & CsCodeRetMask);
                    continue;
                }
                cs_call_alias(
                    cs, static_cast<CsAlias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }

            case CsCodeCallU | CsRetNull:
            case CsCodeCallU | CsRetString:
            case CsCodeCallU | CsRetFloat:
            case CsCodeCallU | CsRetInt: {
                int callargs = op >> 8, offset = numargs - callargs;
                CsValue &idarg = args[offset - 1];
                if (
                    idarg.get_type() != CsValueType::String &&
                    idarg.get_type() != CsValueType::Macro &&
                    idarg.get_type() != CsValueType::Cstring
                ) {
litval:
                    result = ostd::move(idarg);
                    force_arg(result, op & CsCodeRetMask);
                    numargs = offset - 1;
                    continue;
                }
                CsIdent *id = cs.get_ident(idarg.get_strr());
                if (!id) {
noid:
                    if (cs_check_num(idarg.get_strr())) {
                        goto litval;
                    }
                    result.force_null();
                    force_arg(result, op & CsCodeRetMask);
                    throw CsErrorException(
                        cs, "unknown command: %s", idarg.get_strr()
                    );
                }
                result.force_null();
                switch (id->get_type_raw()) {
                    default:
                        if (!CsCommandInternal::has_cb(id)) {
                            numargs = offset - 1;
                            force_arg(result, op & CsCodeRetMask);
                            continue;
                        }
                    /* fallthrough */
                    case CsIdCommand:
                        callcommand(
                            cs, static_cast<CsCommand *>(id), &args[offset],
                            result, callargs
                        );
                        force_arg(result, op & CsCodeRetMask);
                        numargs = offset - 1;
                        continue;
                    case CsIdLocal: {
                        CsIdentStack locals[MaxArguments];
                        for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) {
                            cs_push_alias(cs.force_ident(
                                args[offset + j]
                            ), locals[j]);
                        }
                        cs_do_and_cleanup([&]() {
                            code = runcode(cs, code, result);
                        }, [&]() {
                            for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) {
                                cs_pop_alias(args[offset + j].get_ident());
                            }
                        });
                        return code;
                    }
                    case CsIdIvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsVar *>(id));
                        } else {
                            cs.set_var_int_checked(
                                static_cast<CsIvar *>(id),
                                ostd::iter(&args[offset], callargs)
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdFvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsVar *>(id));
                        } else {
                            cs.set_var_float_checked(
                                static_cast<CsFvar *>(id),
                                args[offset].force_float()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdSvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<CsVar *>(id));
                        } else {
                            cs.set_var_str_checked(
                                static_cast<CsSvar *>(id),
                                args[offset].force_str()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdAlias: {
                        CsAlias *a = static_cast<CsAlias *>(id);
                        if (
                            (a->get_index() < MaxArguments) &&
                            !cs_is_arg_used(cs, a)
                        ) {
                            numargs = offset - 1;
                            force_arg(result, op & CsCodeRetMask);
                            continue;
                        }
                        if (a->get_value().get_type() == CsValueType::Null) {
                            goto noid;
                        }
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
    return code;
}

void CsState::run(CsBytecode *code, CsValue &ret) {
    runcode(*this, reinterpret_cast<ostd::Uint32 *>(code), ret);
}

static void cs_run(
    CsState &cs, ostd::ConstCharRange file, ostd::ConstCharRange code,
    CsValue &ret
) {
    GenState gs(cs);
    gs.src_name = file;
    gs.code.reserve(64);
    gs.gen_main(code, CsValAny);
    gs.done();
    runcode(cs, gs.code.data() + 1, ret);
    if (int(gs.code[0]) >= 0x100) {
        gs.code.release();
    }
}

void CsState::run(ostd::ConstCharRange code, CsValue &ret) {
    cs_run(*this, ostd::ConstCharRange(), code, ret);
}

void CsState::run(CsIdent *id, CsValueRange args, CsValue &ret) {
    int nargs = int(args.size());
    ret.set_null();
    RunDepthRef level{*this}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!CsCommandInternal::has_cb(id)) {
                    break;
                }
            /* fallthrough */
            case CsIdentType::Command:
                if (nargs < static_cast<CsCommand *>(id)->get_num_args()) {
                    CsValue buf[MaxArguments];
                    memcpy(buf, args.data(), args.size() * sizeof(CsValue));
                    callcommand(
                        *this, static_cast<CsCommand *>(id), buf, ret,
                        nargs, false
                    );
                } else {
                    callcommand(
                        *this, static_cast<CsCommand *>(id), args.data(),
                        ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            case CsIdentType::Ivar:
                if (args.empty()) {
                    print_var(static_cast<CsVar *>(id));
                } else {
                    set_var_int_checked(static_cast<CsIvar *>(id), args);
                }
                break;
            case CsIdentType::Fvar:
                if (args.empty()) {
                    print_var(static_cast<CsVar *>(id));
                } else {
                    set_var_float_checked(
                        static_cast<CsFvar *>(id), args[0].force_float()
                    );
                }
                break;
            case CsIdentType::Svar:
                if (args.empty()) {
                    print_var(static_cast<CsVar *>(id));
                } else {
                    set_var_str_checked(
                        static_cast<CsSvar *>(id), args[0].force_str()
                    );
                }
                break;
            case CsIdentType::Alias: {
                CsAlias *a = static_cast<CsAlias *>(id);
                if (
                    (a->get_index() < MaxArguments) && !cs_is_arg_used(*this, a)
                ) {
                    break;
                }
                if (a->get_value().get_type() == CsValueType::Null) {
                    break;
                }
                cs_call_alias(
                    *this, a, args.data(), ret, nargs, nargs, 0, 0, CsRetNull
                );
                break;
            }
        }
    }
}

CsString CsState::run_str(CsBytecode *code) {
    CsValue ret;
    run(code, ret);
    return ret.get_str();
}

CsString CsState::run_str(ostd::ConstCharRange code) {
    CsValue ret;
    run(code, ret);
    return ret.get_str();
}

CsString CsState::run_str(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run(id, args, ret);
    return ret.get_str();
}

CsInt CsState::run_int(CsBytecode *code) {
    CsValue ret;
    run(code, ret);
    return ret.get_int();
}

CsInt CsState::run_int(ostd::ConstCharRange code) {
    CsValue ret;
    run(code, ret);
    return ret.get_int();
}

CsInt CsState::run_int(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run(id, args, ret);
    return ret.get_int();
}

CsFloat CsState::run_float(CsBytecode *code) {
    CsValue ret;
    run(code, ret);
    return ret.get_float();
}

CsFloat CsState::run_float(ostd::ConstCharRange code) {
    CsValue ret;
    run(code, ret);
    return ret.get_float();
}

CsFloat CsState::run_float(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run(id, args, ret);
    return ret.get_float();
}

bool CsState::run_bool(CsBytecode *code) {
    CsValue ret;
    run(code, ret);
    return ret.get_bool();
}

bool CsState::run_bool(ostd::ConstCharRange code) {
    CsValue ret;
    run(code, ret);
    return ret.get_bool();
}

bool CsState::run_bool(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run(id, args, ret);
    return ret.get_bool();
}

void CsState::run(CsBytecode *code) {
    CsValue ret;
    run(code, ret);
}

void CsState::run(ostd::ConstCharRange code) {
    CsValue ret;
    run(code, ret);
}

void CsState::run(CsIdent *id, CsValueRange args) {
    CsValue ret;
    run(id, args, ret);
}

CsLoopState CsState::run_loop(CsBytecode *code, CsValue &ret) {
    ++p_inloop;
    try {
        run(code, ret);
    } catch (CsBreakException) {
        --p_inloop;
        return CsLoopState::Break;
    } catch (CsContinueException) {
        --p_inloop;
        return CsLoopState::Continue;
    } catch (...) {
        --p_inloop;
        throw;
    }
    return CsLoopState::Normal;
}

CsLoopState CsState::run_loop(CsBytecode *code) {
    CsValue ret;
    return run_loop(code, ret);
}

static bool cs_run_file(
    CsState &cs, ostd::ConstCharRange fname, CsValue &ret
) {
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

    cs_run(cs, fname, ostd::ConstCharRange(buf.get(), len), ret);
    return true;
}

ostd::Maybe<CsString> CsState::run_file_str(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    return ostd::move(ret.get_str());
}

ostd::Maybe<CsInt> CsState::run_file_int(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    return ret.get_int();
}

ostd::Maybe<CsFloat> CsState::run_file_float(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    return ret.get_float();
}

ostd::Maybe<bool> CsState::run_file_bool(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return ostd::nothing;
    }
    return ret.get_bool();
}

bool CsState::run_file(ostd::ConstCharRange fname, CsValue &ret) {
    return cs_run_file(*this, fname, ret);
}

bool CsState::run_file(ostd::ConstCharRange fname) {
    CsValue ret;
    if (!cs_run_file(*this, fname, ret)) {
        return false;
    }
    return true;
}

} /* namespace cscript */
