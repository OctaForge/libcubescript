#include "cubescript/cubescript.hh"
#include "cs_vm.hh"
#include "cs_util.hh"

#include <limits>

namespace cscript {

struct cs_cmd_internal {
    static void call(
        cs_state &cs, cs_command *c, cs_value_r args, cs_value &ret
    ) {
        c->p_cb_cftv(cs, args, ret);
    }

    static bool has_cb(cs_ident *id) {
        if (!id->is_command() && !id->is_special()) {
            return false;
        }
        cs_command *cb = static_cast<cs_command *>(id);
        return !!cb->p_cb_cftv;
    }
};

static inline void cs_push_alias(cs_ident *id, cs_ident_stack &st) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        cs_value nv;
        cs_aliasInternal::push_arg(static_cast<cs_alias *>(id), nv, st);
    }
}

static inline void cs_pop_alias(cs_ident *id) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        cs_aliasInternal::pop_arg(static_cast<cs_alias *>(id));
    }
}

cs_stack_state::cs_stack_state(cs_state &cs, cs_stack_state_node *nd, bool gap):
    p_state(cs), p_node(nd), p_gap(gap)
{}
cs_stack_state::cs_stack_state(cs_stack_state &&st):
    p_state(st.p_state), p_node(st.p_node), p_gap(st.p_gap)
{
    st.p_node = nullptr;
    st.p_gap = false;
}

cs_stack_state::~cs_stack_state() {
    size_t len = 0;
    for (cs_stack_state_node const *nd = p_node; nd; nd = nd->next) {
        ++len;
    }
    p_state.p_state->destroy_array(p_node, len);
}

cs_stack_state &cs_stack_state::operator=(cs_stack_state &&st) {
    p_node = st.p_node;
    p_gap = st.p_gap;
    st.p_node = nullptr;
    st.p_gap = false;
    return *this;
}

cs_stack_state_node const *cs_stack_state::get() const {
    return p_node;
}

bool cs_stack_state::gap() const {
    return p_gap;
}

cs_stack_state cs_error::save_stack(cs_state &cs) {
    cs_ivar *dalias = static_cast<cs_ivar *>(cs.p_state->identmap[DbgaliasIdx]);
    if (!dalias->get_value()) {
        return cs_stack_state(cs, nullptr, !!cs.p_callstack);
    }
    int total = 0, depth = 0;
    for (cs_identLink *l = cs.p_callstack; l; l = l->next) {
        total++;
    }
    if (!total) {
        return cs_stack_state(cs, nullptr, false);
    }
    cs_stack_state_node *st = cs.p_state->create_array<cs_stack_state_node>(
        ostd::min(total, dalias->get_value())
    );
    cs_stack_state_node *ret = st, *nd = st;
    ++st;
    for (cs_identLink *l = cs.p_callstack; l; l = l->next) {
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
    return cs_stack_state(cs, ret, total > dalias->get_value());
}

ostd::ConstCharRange cs_error::save_msg(
    cs_state &cs, ostd::ConstCharRange msg
) {
    if (msg.size() > sizeof(cs.p_errbuf)) {
        msg = msg.slice(0, sizeof(cs.p_errbuf));
    }
    cs_gen_state *gs = cs.p_pstate;
    if (gs) {
        /* we can attach line number */
        std::size_t sz = 0;
        try {
            ostd::CharRange r(cs.p_errbuf, cs.p_errbuf + sizeof(cs.p_errbuf));
            if (!gs->src_name.empty()) {
                sz = ostd::format(r, "%s:%d: %s", gs->src_name, gs->current_line, msg);
            } else {
                sz = ostd::format(r, "%d: %s", gs->current_line, msg);
            }
        } catch (ostd::format_error const &e) {
            memcpy(cs.p_errbuf, msg.data(), msg.size());
            sz = msg.size();
        }
        return ostd::ConstCharRange(cs.p_errbuf, cs.p_errbuf + sz);
    }
    memcpy(cs.p_errbuf, msg.data(), msg.size());
    return ostd::ConstCharRange(cs.p_errbuf, cs.p_errbuf + msg.size());
}

static void bcode_ref(uint32_t *code) {
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
            code -= std::ptrdiff_t(code[-1] >> 8);
            bcode_incr(code);
            break;
    }
}

static void bcode_unref(uint32_t *code) {
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
            code -= std::ptrdiff_t(code[-1] >> 8);
            bcode_decr(code);
            break;
    }
}

cs_bcode_ref::cs_bcode_ref(cs_bcode *v): p_code(v) {
    bcode_ref(reinterpret_cast<uint32_t *>(p_code));
}
cs_bcode_ref::cs_bcode_ref(cs_bcode_ref const &v): p_code(v.p_code) {
    bcode_ref(reinterpret_cast<uint32_t *>(p_code));
}

cs_bcode_ref::~cs_bcode_ref() {
    bcode_unref(reinterpret_cast<uint32_t *>(p_code));
}

cs_bcode_ref &cs_bcode_ref::operator=(cs_bcode_ref const &v) {
    bcode_unref(reinterpret_cast<uint32_t *>(p_code));
    p_code = v.p_code;
    bcode_ref(reinterpret_cast<uint32_t *>(p_code));
    return *this;
}

cs_bcode_ref &cs_bcode_ref::operator=(cs_bcode_ref &&v) {
    bcode_unref(reinterpret_cast<uint32_t *>(p_code));
    p_code = v.p_code;
    v.p_code = nullptr;
    return *this;
}

static inline uint32_t *forcecode(cs_state &cs, cs_value &v) {
    uint32_t *code = reinterpret_cast<uint32_t *>(v.get_code());
    if (!code) {
        cs_gen_state gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        gs.done();
        uint32_t *cbuf = new uint32_t[gs.code.size()];
        memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        v.set_code(reinterpret_cast<cs_bcode *>(cbuf + 1));
        code = reinterpret_cast<uint32_t *>(v.get_code());
    }
    return code;
}

static inline void forcecond(cs_state &cs, cs_value &v) {
    switch (v.get_type()) {
        case cs_value_type::String:
        case cs_value_type::Macro:
        case cs_value_type::Cstring:
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

static uint32_t emptyblock[CsValAny][2] = {
    { CsCodeStart + 0x100, CsCodeExit | CsRetNull },
    { CsCodeStart + 0x100, CsCodeExit | CsRetInt },
    { CsCodeStart + 0x100, CsCodeExit | CsRetFloat },
    { CsCodeStart + 0x100, CsCodeExit | CsRetString }
};

static inline void force_arg(cs_value &v, int type) {
    switch (type) {
        case CsRetString:
            if (v.get_type() != cs_value_type::String) {
                v.force_str();
            }
            break;
        case CsRetInt:
            if (v.get_type() != cs_value_type::Int) {
                v.force_int();
            }
            break;
        case CsRetFloat:
            if (v.get_type() != cs_value_type::Float) {
                v.force_float();
            }
            break;
    }
}

static uint32_t *skipcode(uint32_t *code) {
    int depth = 0;
    for (;;) {
        uint32_t op = *code++;
        switch (op & 0xFF) {
            case CsCodeMacro:
            case CsCodeVal | CsRetString: {
                uint32_t len = op >> 8;
                code += len / sizeof(uint32_t) + 1;
                continue;
            }
            case CsCodeBlock:
            case CsCodeJump:
            case CsCodeJumpB | CsCodeFlagTrue:
            case CsCodeJumpB | CsCodeFlagFalse:
            case CsCodeJumpResult | CsCodeFlagTrue:
            case CsCodeJumpResult | CsCodeFlagFalse: {
                uint32_t len = op >> 8;
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

cs_bcode *cs_copy_code(cs_bcode *c) {
    uint32_t *bcode = reinterpret_cast<uint32_t *>(c);
    uint32_t *end = skipcode(bcode);
    uint32_t *dst = new uint32_t[end - bcode + 1];
    *dst++ = CsCodeStart;
    memcpy(dst, bcode, (end - bcode) * sizeof(uint32_t));
    return reinterpret_cast<cs_bcode *>(dst);
}

static inline void callcommand(
    cs_state &cs, cs_command *id, cs_value *args, cs_value &res, int numargs,
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
                    args[i].set_int(std::numeric_limits<cs_int>::min());
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
                        reinterpret_cast<cs_bcode *>(emptyblock[CsValNull] + 1)
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
                args[i].set_int(cs_int(lookup ? -1 : i - fakeargs));
                break;
            case 'C': {
                i = ostd::max(i + 1, numargs);
                auto buf = ostd::appender<cs_string>();
                cscript::util::tvals_concat(buf, ostd::iter(args, i), " ");
                cs_value tv;
                tv.set_str(std::move(buf.get()));
                cs_cmd_internal::call(cs, id, cs_value_r(&tv, &tv + 1), res);
                return;
            }
            case 'V':
                i = ostd::max(i + 1, numargs);
                cs_cmd_internal::call(cs, id, ostd::iter(args, i), res);
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
    cs_cmd_internal::call(cs, id, cs_value_r(args, args + i), res);
}

static uint32_t *runcode(cs_state &cs, uint32_t *code, cs_value &result);

static inline void cs_call_alias(
    cs_state &cs, cs_alias *a, cs_value *args, cs_value &result,
    int callargs, int &nargs, int offset, int skip, uint32_t op
) {
    cs_ivar *anargs = static_cast<cs_ivar *>(cs.p_state->identmap[NumargsIdx]);
    cs_ident_stack argstack[MaxArguments];
    for(int i = 0; i < callargs; i++) {
        cs_aliasInternal::push_arg(
            static_cast<cs_alias *>(cs.p_state->identmap[i]),
            args[offset + i], argstack[i], false
        );
    }
    int oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = cs.identflags;
    cs.identflags |= a->get_flags()&CS_IDF_OVERRIDDEN;
    cs_identLink aliaslink = {
        a, cs.p_callstack, (1<<callargs)-1, argstack
    };
    cs.p_callstack = &aliaslink;
    uint32_t *codep = reinterpret_cast<uint32_t *>(
        cs_aliasInternal::compile_code(a, cs)
    );
    bcode_incr(codep);
    cs_do_and_cleanup([&]() {
        runcode(cs, codep+1, result);
    }, [&]() {
        bcode_decr(codep);
        cs.p_callstack = aliaslink.next;
        cs.identflags = oldflags;
        for (int i = 0; i < callargs; i++) {
            cs_aliasInternal::pop_arg(
                static_cast<cs_alias *>(cs.p_state->identmap[i])
            );
        }
        int argmask = aliaslink.usedargs & (~0 << callargs);
        for (; argmask; ++callargs) {
            if (argmask & (1 << callargs)) {
                cs_aliasInternal::pop_arg(static_cast<cs_alias *>(
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
    RunDepthRef(cs_state &cs) {
        if (rundepth >= MaxRunDepth) {
            throw cs_error(cs, "exceeded recursion limit");
        }
        ++rundepth;
    }
    RunDepthRef(RunDepthRef const &) = delete;
    RunDepthRef(RunDepthRef &&) = delete;
    ~RunDepthRef() { --rundepth; }
};

static inline cs_alias *cs_get_lookup_id(cs_state &cs, uint32_t op) {
    cs_ident *id = cs.p_state->identmap[op >> 8];
    if (id->get_flags() & CS_IDF_UNKNOWN) {
        throw cs_error(cs, "unknown alias lookup: %s", id->get_name());
    }
    return static_cast<cs_alias *>(id);
}

static inline cs_alias *cs_get_lookuparg_id(cs_state &cs, uint32_t op) {
    cs_ident *id = cs.p_state->identmap[op >> 8];
    if (!cs_is_arg_used(cs, id)) {
        return nullptr;
    }
    return static_cast<cs_alias *>(id);
}

static inline int cs_get_lookupu_type(
    cs_state &cs, cs_value &arg, cs_ident *&id, uint32_t op
) {
    if (
        arg.get_type() != cs_value_type::String &&
        arg.get_type() != cs_value_type::Macro &&
        arg.get_type() != cs_value_type::Cstring
    ) {
        return -2; /* default case */
    }
    id = cs.get_ident(arg.get_strr());
    if (id) {
        switch(id->get_type()) {
            case cs_ident_type::Alias:
                if (id->get_flags() & CS_IDF_UNKNOWN) {
                    break;
                }
                if ((id->get_index() < MaxArguments) && !cs_is_arg_used(cs, id)) {
                    return CsIdUnknown;
                }
                return CsIdAlias;
            case cs_ident_type::Svar:
                return CsIdSvar;
            case cs_ident_type::Ivar:
                return CsIdIvar;
            case cs_ident_type::Fvar:
                return CsIdFvar;
            case cs_ident_type::Command: {
                arg.set_null();
                cs_value buf[MaxArguments];
                callcommand(cs, static_cast<cs_command *>(id), buf, arg, 0, true);
                force_arg(arg, op & CsCodeRetMask);
                return -2; /* ignore */
            }
            default:
                return CsIdUnknown;
        }
    }
    throw cs_error(cs, "unknown alias lookup: %s", arg.get_strr());
}

static uint32_t *runcode(cs_state &cs, uint32_t *code, cs_value &result) {
    result.set_null();
    RunDepthRef level{cs}; /* incr and decr on scope exit */
    int numargs = 0;
    cs_value args[MaxArguments + MaxResults];
    auto &chook = cs.get_call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        uint32_t op = *code++;
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
                result.set_float(cs_float(!args[numargs].get_bool()));
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
                args[numargs++] = std::move(result);
                continue;
            case CsCodePrint:
                cs.print_var(static_cast<cs_var *>(cs.p_state->identmap[op >> 8]));
                continue;

            case CsCodeLocal: {
                int numlocals = op >> 8, offset = numargs - numlocals;
                cs_ident_stack locals[MaxArguments];
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
                uint32_t len = op >> 8;
                code += len;
                continue;
            }
            case CsCodeJumpB | CsCodeFlagTrue: {
                uint32_t len = op >> 8;
                if (args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpB | CsCodeFlagFalse: {
                uint32_t len = op >> 8;
                if (!args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpResult | CsCodeFlagTrue: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == cs_value_type::Code) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CsCodeJumpResult | CsCodeFlagFalse: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == cs_value_type::Code) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
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
                    throw cs_error(cs, "no loop to break");
                }
                break;
            case CsCodeBreak | CsCodeFlagTrue:
                if (cs.is_in_loop()) {
                    throw CsContinueException();
                } else {
                    throw cs_error(cs, "no loop to continue");
                }
                break;

            case CsCodeMacro: {
                uint32_t len = op >> 8;
                args[numargs++].set_macro(ostd::ConstCharRange(
                    reinterpret_cast<char const *>(code),
                    reinterpret_cast<char const *>(code) + len
                ));
                code += len / sizeof(uint32_t) + 1;
                continue;
            }

            case CsCodeVal | CsRetString: {
                uint32_t len = op >> 8;
                args[numargs++].set_str(cs_string{
                    reinterpret_cast<char const *>(code),
                    reinterpret_cast<char const *>(code) + len
                });
                code += len / sizeof(uint32_t) + 1;
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
                    *reinterpret_cast<cs_int const *>(code)
                );
                code += CsTypeStorageSize<cs_int>;
                continue;
            case CsCodeValInt | CsRetInt:
                args[numargs++].set_int(cs_int(op) >> 8);
                continue;
            case CsCodeVal | CsRetFloat:
                args[numargs++].set_float(
                    *reinterpret_cast<cs_float const *>(code)
                );
                code += CsTypeStorageSize<cs_float>;
                continue;
            case CsCodeValInt | CsRetFloat:
                args[numargs++].set_float(cs_float(cs_int(op) >> 8));
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
                args[numargs].set_str(args[numargs - 1].get_str());
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
                result = std::move(args[--numargs]);
                continue;
            case CsCodeResult | CsRetString:
            case CsCodeResult | CsRetInt:
            case CsCodeResult | CsRetFloat:
                result = std::move(args[--numargs]);
                force_arg(result, op & CsCodeRetMask);
                continue;

            case CsCodeEmpty | CsRetNull:
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(emptyblock[CsValNull] + 1)
                );
                break;
            case CsCodeEmpty | CsRetString:
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(emptyblock[CsValString] + 1)
                );
                break;
            case CsCodeEmpty | CsRetInt:
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(emptyblock[CsValInt] + 1)
                );
                break;
            case CsCodeEmpty | CsRetFloat:
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(emptyblock[CsValFloat] + 1)
                );
                break;
            case CsCodeBlock: {
                uint32_t len = op >> 8;
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(code + 1)
                );
                code += len;
                continue;
            }
            case CsCodeCompile: {
                cs_value &arg = args[numargs - 1];
                cs_gen_state gs(cs);
                switch (arg.get_type()) {
                    case cs_value_type::Int:
                        gs.code.reserve(8);
                        gs.code.push_back(CsCodeStart);
                        gs.gen_int(arg.get_int());
                        gs.code.push_back(CsCodeResult);
                        gs.code.push_back(CsCodeExit);
                        break;
                    case cs_value_type::Float:
                        gs.code.reserve(8);
                        gs.code.push_back(CsCodeStart);
                        gs.gen_float(arg.get_float());
                        gs.code.push_back(CsCodeResult);
                        gs.code.push_back(CsCodeExit);
                        break;
                    case cs_value_type::String:
                    case cs_value_type::Macro:
                    case cs_value_type::Cstring:
                        gs.code.reserve(64);
                        gs.gen_main(arg.get_strr());
                        break;
                    default:
                        gs.code.reserve(8);
                        gs.code.push_back(CsCodeStart);
                        gs.gen_null();
                        gs.code.push_back(CsCodeResult);
                        gs.code.push_back(CsCodeExit);
                        break;
                }
                gs.done();
                uint32_t *cbuf = new uint32_t[gs.code.size()];
                memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
                arg.set_code(
                    reinterpret_cast<cs_bcode *>(cbuf + 1)
                );
                continue;
            }
            case CsCodeCond: {
                cs_value &arg = args[numargs - 1];
                switch (arg.get_type()) {
                    case cs_value_type::String:
                    case cs_value_type::Macro:
                    case cs_value_type::Cstring: {
                        ostd::ConstCharRange s = arg.get_strr();
                        if (!s.empty()) {
                            cs_gen_state gs(cs);
                            gs.code.reserve(64);
                            gs.gen_main(s);
                            gs.done();
                            uint32_t *cbuf = new uint32_t[gs.code.size()];
                            memcpy(
                                cbuf, gs.code.data(),
                                gs.code.size() * sizeof(uint32_t)
                            );
                            arg.set_code(reinterpret_cast<cs_bcode *>(cbuf + 1));
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
                cs_alias *a = static_cast<cs_alias *>(
                    cs.p_state->identmap[op >> 8]
                );
                if (!cs_is_arg_used(cs, a)) {
                    cs_value nv;
                    cs_aliasInternal::push_arg(
                        a, nv, cs.p_callstack->argstack[a->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << a->get_index();
                }
                args[numargs++].set_ident(a);
                continue;
            }
            case CsCodeIdentU: {
                cs_value &arg = args[numargs - 1];
                cs_ident *id = cs.p_state->identmap[DummyIdx];
                if (
                    arg.get_type() == cs_value_type::String ||
                    arg.get_type() == cs_value_type::Macro ||
                    arg.get_type() == cs_value_type::Cstring
                ) {
                    id = cs.new_ident(arg.get_strr());
                }
                if ((id->get_index() < MaxArguments) && !cs_is_arg_used(cs, id)) {
                    cs_value nv;
                    cs_aliasInternal::push_arg(
                        static_cast<cs_alias *>(id), nv,
                        cs.p_callstack->argstack[id->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << id->get_index();
                }
                arg.set_ident(id);
                continue;
            }

            case CsCodeLookupU | CsRetString: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_str(
                            static_cast<cs_alias *>(id)->get_value().get_str()
                        );
                        continue;
                    case CsIdSvar:
                        arg.set_str(cs_string{
                            static_cast<cs_svar *>(id)->get_value()
                        });
                        continue;
                    case CsIdIvar:
                        arg.set_str(
                            intstr(static_cast<cs_ivar *>(id)->get_value())
                        );
                        continue;
                    case CsIdFvar:
                        arg.set_str(
                            floatstr(static_cast<cs_fvar *>(id)->get_value())
                        );
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
                    cs_get_lookup_id(cs, op)->get_value().get_str()
                );
                continue;
            case CsCodeLookupArg | CsRetString: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs++].set_str(a->get_value().get_str());
                }
                continue;
            }
            case CsCodeLookupU | CsRetInt: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_int(
                            static_cast<cs_alias *>(id)->get_value().get_int()
                        );
                        continue;
                    case CsIdSvar:
                        arg.set_int(cs_parse_int(
                            static_cast<cs_svar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_int(
                            cs_int(static_cast<cs_fvar *>(id)->get_value())
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
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_int(0);
                } else {
                    args[numargs++].set_int(a->get_value().get_int());
                }
                continue;
            }
            case CsCodeLookupU | CsRetFloat: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        arg.set_float(
                            static_cast<cs_alias *>(id)->get_value().get_float()
                        );
                        continue;
                    case CsIdSvar:
                        arg.set_float(cs_parse_float(
                            static_cast<cs_svar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdIvar:
                        arg.set_float(cs_float(
                            static_cast<cs_ivar *>(id)->get_value()
                        ));
                        continue;
                    case CsIdFvar:
                        arg.set_float(
                            static_cast<cs_fvar *>(id)->get_value()
                        );
                        continue;
                    case CsIdUnknown:
                        arg.set_float(cs_float(0));
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
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_float(cs_float(0));
                } else {
                    args[numargs++].set_float(a->get_value().get_float());
                }
                continue;
            }
            case CsCodeLookupU | CsRetNull: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<cs_alias *>(id)->get_value().get_val(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_str(cs_string{
                            static_cast<cs_svar *>(id)->get_value()
                        });
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_float(
                            static_cast<cs_fvar *>(id)->get_value()
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
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->get_value().get_val(args[numargs++]);
                }
                continue;
            }

            case CsCodeLookupMu | CsRetString: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<cs_alias *>(id)->get_cstr(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_cstr(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_str(
                            intstr(static_cast<cs_ivar *>(id)->get_value())
                        );
                        continue;
                    case CsIdFvar:
                        arg.set_str(
                            floatstr(static_cast<cs_fvar *>(id)->get_value())
                        );
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
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_cstr("");
                } else {
                    a->get_cstr(args[numargs++]);
                }
                continue;
            }
            case CsCodeLookupMu | CsRetNull: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case CsIdAlias:
                        static_cast<cs_alias *>(id)->get_cval(arg);
                        continue;
                    case CsIdSvar:
                        arg.set_cstr(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case CsIdIvar:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case CsIdFvar:
                        arg.set_float(static_cast<cs_fvar *>(id)->get_value());
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
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_null();
                } else {
                    a->get_cval(args[numargs++]);
                }
                continue;
            }

            case CsCodeSvar | CsRetString:
            case CsCodeSvar | CsRetNull:
                args[numargs++].set_str(cs_string{static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()});
                continue;
            case CsCodeSvar | CsRetInt:
                args[numargs++].set_int(cs_parse_int(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeSvar | CsRetFloat:
                args[numargs++].set_float(cs_parse_float(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeSvarM:
                args[numargs++].set_cstr(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeSvar1:
                cs.set_var_str_checked(
                    static_cast<cs_svar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_strr()
                );
                continue;

            case CsCodeIvar | CsRetInt:
            case CsCodeIvar | CsRetNull:
                args[numargs++].set_int(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeIvar | CsRetString:
                args[numargs++].set_str(intstr(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeIvar | CsRetFloat:
                args[numargs++].set_float(cs_float(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeIvar1:
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_int()
                );
                continue;
            case CsCodeIvar2:
                numargs -= 2;
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                );
                continue;
            case CsCodeIvar3:
                numargs -= 3;
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                        | (args[numargs + 2].get_int()));
                continue;

            case CsCodeFvar | CsRetFloat:
            case CsCodeFvar | CsRetNull:
                args[numargs++].set_float(static_cast<cs_fvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CsCodeFvar | CsRetString:
                args[numargs++].set_str(floatstr(
                    static_cast<cs_fvar *>(
                        cs.p_state->identmap[op >> 8]
                    )->get_value()
                ));
                continue;
            case CsCodeFvar | CsRetInt:
                args[numargs++].set_int(int(static_cast<cs_fvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CsCodeFvar1:
                cs.set_var_float_checked(
                    static_cast<cs_fvar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_float()
                );
                continue;

            case CsCodeCom | CsRetNull:
            case CsCodeCom | CsRetString:
            case CsCodeCom | CsRetFloat:
            case CsCodeCom | CsRetInt: {
                cs_command *id = static_cast<cs_command *>(
                    cs.p_state->identmap[op >> 8]
                );
                int offset = numargs - id->get_num_args();
                result.force_null();
                cs_cmd_internal::call(cs, id, cs_value_r(
                    args + offset, args + offset + id->get_num_args()
                ), result);
                force_arg(result, op & CsCodeRetMask);
                numargs = offset;
                continue;
            }

            case CsCodeComV | CsRetNull:
            case CsCodeComV | CsRetString:
            case CsCodeComV | CsRetFloat:
            case CsCodeComV | CsRetInt: {
                cs_command *id = static_cast<cs_command *>(
                    cs.p_state->identmap[op >> 13]
                );
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                cs_cmd_internal::call(
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
                cs_command *id = static_cast<cs_command *>(
                    cs.p_state->identmap[op >> 13]
                );
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                result.force_null();
                {
                    auto buf = ostd::appender<cs_string>();
                    cscript::util::tvals_concat(
                        buf, ostd::iter(&args[offset], callargs), " "
                    );
                    cs_value tv;
                    tv.set_str(std::move(buf.get()));
                    cs_cmd_internal::call(cs, id, cs_value_r(&tv, &tv + 1), result);
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
                auto buf = ostd::appender<cs_string>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc),
                    ((op & CsCodeOpMask) == CsCodeConc) ? " " : ""
                );
                numargs = numargs - numconc;
                args[numargs].set_str(std::move(buf.get()));
                force_arg(args[numargs], op & CsCodeRetMask);
                numargs++;
                continue;
            }

            case CsCodeConcM | CsRetNull:
            case CsCodeConcM | CsRetString:
            case CsCodeConcM | CsRetFloat:
            case CsCodeConcM | CsRetInt: {
                int numconc = op >> 8;
                auto buf = ostd::appender<cs_string>();
                cscript::util::tvals_concat(
                    buf, ostd::iter(&args[numargs - numconc], numconc)
                );
                numargs = numargs - numconc;
                result.set_str(std::move(buf.get()));
                force_arg(result, op & CsCodeRetMask);
                continue;
            }

            case CsCodeAlias:
                cs_aliasInternal::set_alias(
                    static_cast<cs_alias *>(cs.p_state->identmap[op >> 8]),
                    cs, args[--numargs]
                );
                continue;
            case CsCodeAliasArg:
                cs_aliasInternal::set_arg(
                    static_cast<cs_alias *>(cs.p_state->identmap[op >> 8]),
                    cs, args[--numargs]
                );
                continue;
            case CsCodeAliasU:
                numargs -= 2;
                cs.set_alias(
                    args[numargs].get_str(), std::move(args[numargs + 1])
                );
                continue;

            case CsCodeCall | CsRetNull:
            case CsCodeCall | CsRetString:
            case CsCodeCall | CsRetFloat:
            case CsCodeCall | CsRetInt: {
                result.force_null();
                cs_ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (id->get_flags() & CS_IDF_UNKNOWN) {
                    force_arg(result, op & CsCodeRetMask);
                    throw cs_error(
                        cs, "unknown command: %s", id->get_name()
                    );
                }
                cs_call_alias(
                    cs, static_cast<cs_alias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }
            case CsCodeCallArg | CsRetNull:
            case CsCodeCallArg | CsRetString:
            case CsCodeCallArg | CsRetFloat:
            case CsCodeCallArg | CsRetInt: {
                result.force_null();
                cs_ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (!cs_is_arg_used(cs, id)) {
                    numargs = offset;
                    force_arg(result, op & CsCodeRetMask);
                    continue;
                }
                cs_call_alias(
                    cs, static_cast<cs_alias *>(id), args, result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }

            case CsCodeCallU | CsRetNull:
            case CsCodeCallU | CsRetString:
            case CsCodeCallU | CsRetFloat:
            case CsCodeCallU | CsRetInt: {
                int callargs = op >> 8, offset = numargs - callargs;
                cs_value &idarg = args[offset - 1];
                if (
                    idarg.get_type() != cs_value_type::String &&
                    idarg.get_type() != cs_value_type::Macro &&
                    idarg.get_type() != cs_value_type::Cstring
                ) {
litval:
                    result = std::move(idarg);
                    force_arg(result, op & CsCodeRetMask);
                    numargs = offset - 1;
                    continue;
                }
                cs_ident *id = cs.get_ident(idarg.get_strr());
                if (!id) {
noid:
                    if (cs_check_num(idarg.get_strr())) {
                        goto litval;
                    }
                    result.force_null();
                    force_arg(result, op & CsCodeRetMask);
                    throw cs_error(
                        cs, "unknown command: %s", idarg.get_strr()
                    );
                }
                result.force_null();
                switch (id->get_type_raw()) {
                    default:
                        if (!cs_cmd_internal::has_cb(id)) {
                            numargs = offset - 1;
                            force_arg(result, op & CsCodeRetMask);
                            continue;
                        }
                    /* fallthrough */
                    case CsIdCommand:
                        callcommand(
                            cs, static_cast<cs_command *>(id), &args[offset],
                            result, callargs
                        );
                        force_arg(result, op & CsCodeRetMask);
                        numargs = offset - 1;
                        continue;
                    case CsIdLocal: {
                        cs_ident_stack locals[MaxArguments];
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            cs_push_alias(cs.force_ident(
                                args[offset + j]
                            ), locals[j]);
                        }
                        cs_do_and_cleanup([&]() {
                            code = runcode(cs, code, result);
                        }, [&]() {
                            for (size_t j = 0; j < size_t(callargs); ++j) {
                                cs_pop_alias(args[offset + j].get_ident());
                            }
                        });
                        return code;
                    }
                    case CsIdIvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_int_checked(
                                static_cast<cs_ivar *>(id),
                                ostd::iter(&args[offset], callargs)
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdFvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_float_checked(
                                static_cast<cs_fvar *>(id),
                                args[offset].force_float()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdSvar:
                        if (callargs <= 0) {
                            cs.print_var(static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_str_checked(
                                static_cast<cs_svar *>(id),
                                args[offset].force_str()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CsCodeRetMask);
                        continue;
                    case CsIdAlias: {
                        cs_alias *a = static_cast<cs_alias *>(id);
                        if (
                            (a->get_index() < MaxArguments) &&
                            !cs_is_arg_used(cs, a)
                        ) {
                            numargs = offset - 1;
                            force_arg(result, op & CsCodeRetMask);
                            continue;
                        }
                        if (a->get_value().get_type() == cs_value_type::Null) {
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

void cs_state::run(cs_bcode *code, cs_value &ret) {
    runcode(*this, reinterpret_cast<uint32_t *>(code), ret);
}

static void cs_run(
    cs_state &cs, ostd::ConstCharRange file, ostd::ConstCharRange code,
    cs_value &ret
) {
    cs_gen_state gs(cs);
    gs.src_name = file;
    gs.code.reserve(64);
    gs.gen_main(code, CsValAny);
    gs.done();
    uint32_t *cbuf = new uint32_t[gs.code.size()];
    memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
    runcode(cs, cbuf + 1, ret);
    if (int(cbuf[0]) < 0x100) {
        delete[] cbuf;
    }
}

void cs_state::run(ostd::ConstCharRange code, cs_value &ret) {
    cs_run(*this, ostd::ConstCharRange(), code, ret);
}

void cs_state::run(cs_ident *id, cs_value_r args, cs_value &ret) {
    int nargs = int(args.size());
    ret.set_null();
    RunDepthRef level{*this}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!cs_cmd_internal::has_cb(id)) {
                    break;
                }
            /* fallthrough */
            case cs_ident_type::Command:
                if (nargs < static_cast<cs_command *>(id)->get_num_args()) {
                    cs_value buf[MaxArguments];
                    memcpy(buf, &args[0], args.size() * sizeof(cs_value));
                    callcommand(
                        *this, static_cast<cs_command *>(id), buf, ret,
                        nargs, false
                    );
                } else {
                    callcommand(
                        *this, static_cast<cs_command *>(id), &args[0],
                        ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            case cs_ident_type::Ivar:
                if (args.empty()) {
                    print_var(static_cast<cs_var *>(id));
                } else {
                    set_var_int_checked(static_cast<cs_ivar *>(id), args);
                }
                break;
            case cs_ident_type::Fvar:
                if (args.empty()) {
                    print_var(static_cast<cs_var *>(id));
                } else {
                    set_var_float_checked(
                        static_cast<cs_fvar *>(id), args[0].force_float()
                    );
                }
                break;
            case cs_ident_type::Svar:
                if (args.empty()) {
                    print_var(static_cast<cs_var *>(id));
                } else {
                    set_var_str_checked(
                        static_cast<cs_svar *>(id), args[0].force_str()
                    );
                }
                break;
            case cs_ident_type::Alias: {
                cs_alias *a = static_cast<cs_alias *>(id);
                if (
                    (a->get_index() < MaxArguments) && !cs_is_arg_used(*this, a)
                ) {
                    break;
                }
                if (a->get_value().get_type() == cs_value_type::Null) {
                    break;
                }
                cs_call_alias(
                    *this, a, &args[0], ret, nargs, nargs, 0, 0, CsRetNull
                );
                break;
            }
        }
    }
}

cs_string cs_state::run_str(cs_bcode *code) {
    cs_value ret;
    run(code, ret);
    return ret.get_str();
}

cs_string cs_state::run_str(ostd::ConstCharRange code) {
    cs_value ret;
    run(code, ret);
    return ret.get_str();
}

cs_string cs_state::run_str(cs_ident *id, cs_value_r args) {
    cs_value ret;
    run(id, args, ret);
    return ret.get_str();
}

cs_int cs_state::run_int(cs_bcode *code) {
    cs_value ret;
    run(code, ret);
    return ret.get_int();
}

cs_int cs_state::run_int(ostd::ConstCharRange code) {
    cs_value ret;
    run(code, ret);
    return ret.get_int();
}

cs_int cs_state::run_int(cs_ident *id, cs_value_r args) {
    cs_value ret;
    run(id, args, ret);
    return ret.get_int();
}

cs_float cs_state::run_float(cs_bcode *code) {
    cs_value ret;
    run(code, ret);
    return ret.get_float();
}

cs_float cs_state::run_float(ostd::ConstCharRange code) {
    cs_value ret;
    run(code, ret);
    return ret.get_float();
}

cs_float cs_state::run_float(cs_ident *id, cs_value_r args) {
    cs_value ret;
    run(id, args, ret);
    return ret.get_float();
}

bool cs_state::run_bool(cs_bcode *code) {
    cs_value ret;
    run(code, ret);
    return ret.get_bool();
}

bool cs_state::run_bool(ostd::ConstCharRange code) {
    cs_value ret;
    run(code, ret);
    return ret.get_bool();
}

bool cs_state::run_bool(cs_ident *id, cs_value_r args) {
    cs_value ret;
    run(id, args, ret);
    return ret.get_bool();
}

void cs_state::run(cs_bcode *code) {
    cs_value ret;
    run(code, ret);
}

void cs_state::run(ostd::ConstCharRange code) {
    cs_value ret;
    run(code, ret);
}

void cs_state::run(cs_ident *id, cs_value_r args) {
    cs_value ret;
    run(id, args, ret);
}

CsLoopState cs_state::run_loop(cs_bcode *code, cs_value &ret) {
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

CsLoopState cs_state::run_loop(cs_bcode *code) {
    cs_value ret;
    return run_loop(code, ret);
}

static bool cs_run_file(
    cs_state &cs, ostd::ConstCharRange fname, cs_value &ret
) {
    std::unique_ptr<char[]> buf;
    size_t len;

    ostd::FileStream f(fname, ostd::StreamMode::read);
    if (!f.is_open()) {
        return false;
    }

    len = f.size();
    buf = std::make_unique<char[]>(len + 1);
    if (!buf || f.get(buf.get(), len) != len) {
        return false;
    }
    buf[len] = '\0';

    cs_run(cs, fname, ostd::ConstCharRange(buf.get(), buf.get() + len), ret);
    return true;
}

std::optional<cs_string> cs_state::run_file_str(ostd::ConstCharRange fname) {
    cs_value ret;
    if (!cs_run_file(*this, fname, ret)) {
        return std::nullopt;
    }
    return ret.get_str();
}

std::optional<cs_int> cs_state::run_file_int(ostd::ConstCharRange fname) {
    cs_value ret;
    if (!cs_run_file(*this, fname, ret)) {
        return std::nullopt;
    }
    return ret.get_int();
}

std::optional<cs_float> cs_state::run_file_float(ostd::ConstCharRange fname) {
    cs_value ret;
    if (!cs_run_file(*this, fname, ret)) {
        return std::nullopt;
    }
    return ret.get_float();
}

std::optional<bool> cs_state::run_file_bool(ostd::ConstCharRange fname) {
    cs_value ret;
    if (!cs_run_file(*this, fname, ret)) {
        return std::nullopt;
    }
    return ret.get_bool();
}

bool cs_state::run_file(ostd::ConstCharRange fname, cs_value &ret) {
    return cs_run_file(*this, fname, ret);
}

bool cs_state::run_file(ostd::ConstCharRange fname) {
    cs_value ret;
    if (!cs_run_file(*this, fname, ret)) {
        return false;
    }
    return true;
}

} /* namespace cscript */
