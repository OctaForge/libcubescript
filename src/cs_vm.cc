#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_std.hh"

#include <cstdio>
#include <limits>

namespace cscript {

static inline bool cs_ident_has_cb(cs_ident *id) {
    if (!id->is_command() && !id->is_special()) {
        return false;
    }
    return !!static_cast<cs_command_impl *>(id)->p_cb_cftv;
}

static inline void cs_push_alias(cs_state &cs, cs_ident *id, cs_ident_stack &st) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        cs_value nv{cs};
        static_cast<cs_alias_impl *>(id)->push_arg(nv, st);
    }
}

static inline void cs_pop_alias(cs_ident *id) {
    if (id->is_alias() && (id->get_index() >= MaxArguments)) {
        static_cast<cs_alias_impl *>(id)->pop_arg();
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

static inline uint32_t *forcecode(cs_state &cs, cs_value &v) {
    auto *code = v.get_code();
    if (!code) {
        cs_gen_state gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        gs.done();
        uint32_t *cbuf = bcode_alloc(cs, gs.code.size());
        memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        v.set_code(reinterpret_cast<cs_bcode *>(cbuf + 1));
        code = v.get_code();
    }
    return code->get_raw();
}

static inline void forcecond(cs_state &cs, cs_value &v) {
    switch (v.get_type()) {
        case cs_value_type::STRING:
            if (!std::string_view{v.get_str()}.empty()) {
                forcecode(cs, v);
            } else {
                v.set_int(0);
            }
            break;
        default:
            break;
    }
}

static inline void force_arg(cs_value &v, int type) {
    switch (type) {
        case CS_RET_STRING:
            if (v.get_type() != cs_value_type::STRING) {
                v.force_str();
            }
            break;
        case CS_RET_INT:
            if (v.get_type() != cs_value_type::INT) {
                v.force_int();
            }
            break;
        case CS_RET_FLOAT:
            if (v.get_type() != cs_value_type::FLOAT) {
                v.force_float();
            }
            break;
    }
}

static inline void callcommand(
    cs_state &cs, cs_command_impl *id, cs_value *args, cs_value &res,
    int numargs, bool lookup = false
) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    auto fmt = id->get_args();
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
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
            case 's':
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
            case 'T':
            case 't':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_none();
                    fakeargs++;
                }
                break;
            case 'E':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_none();
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
                        bcode_get_empty(cs_get_sstate(cs)->empty, CS_VAL_NULL)
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
                i = std::max(i + 1, numargs);
                cs_value tv{cs};
                tv.set_str(value_list_concat(
                    cs, std::span{args, std::size_t(i)}, " "
                ));
                static_cast<cs_command_impl *>(id)->call(
                    cs, std::span<cs_value>(&tv, &tv + 1), res
                );
                return;
            }
            case 'V':
                i = std::max(i + 1, numargs);
                static_cast<cs_command_impl *>(id)->call(
                    cs, std::span{args, std::size_t(i)}, res
                );
                return;
            case '1':
            case '2':
            case '3':
            case '4':
                if (i + 1 < numargs) {
                    it -= *it - '0' + 1;
                    rep = true;
                }
                break;
        }
    }
    ++i;
    static_cast<cs_command_impl *>(id)->call(
        cs, std::span<cs_value>{args, std::size_t(i)}, res
    );
}

static uint32_t *runcode(cs_state &cs, uint32_t *code, cs_value &result);

static inline void cs_call_alias(
    cs_state &cs, cs_alias *a, cs_value *args, cs_value &result,
    int callargs, int &nargs, int offset, int skip, uint32_t op
) {
    cs_ivar *anargs = static_cast<cs_ivar *>(cs.p_state->identmap[NumargsIdx]);
    cs_valarray<cs_ident_stack, MaxArguments> argstack{cs};
    for(int i = 0; i < callargs; i++) {
        static_cast<cs_alias_impl *>(cs.p_state->identmap[i])->push_arg(
            args[offset + i], argstack[i], false
        );
    }
    int oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = cs.identflags;
    cs.identflags |= a->get_flags()&CS_IDF_OVERRIDDEN;
    cs_ident_link aliaslink = {
        a, cs.p_callstack, (1<<callargs)-1, &argstack[0]
    };
    cs.p_callstack = &aliaslink;
    uint32_t *codep = static_cast<cs_alias_impl *>(a)->compile_code(cs)->get_raw();
    bcode_incr(codep);
    call_with_cleanup([&]() {
        runcode(cs, codep+1, result);
    }, [&]() {
        bcode_decr(codep);
        cs.p_callstack = aliaslink.next;
        cs.identflags = oldflags;
        for (int i = 0; i < callargs; i++) {
            static_cast<cs_alias_impl *>(cs.p_state->identmap[i])->pop_arg();
        }
        int argmask = aliaslink.usedargs & int(~0U << callargs);
        for (; argmask; ++callargs) {
            if (argmask & (1 << callargs)) {
                static_cast<cs_alias_impl *>(
                    cs.p_state->identmap[callargs]
                )->pop_arg();
                argmask &= ~(1 << callargs);
            }
        }
        force_arg(result, op & CS_CODE_RET_MASK);
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
        throw cs_error(cs, "unknown alias lookup: %s", id->get_name().data());
    }
    return static_cast<cs_alias *>(id);
}

static inline cs_alias *cs_get_lookuparg_id(cs_state &cs, uint32_t op) {
    cs_ident *id = cs.p_state->identmap[op >> 8];
    if (!ident_is_used_arg(id, cs)) {
        return nullptr;
    }
    return static_cast<cs_alias *>(id);
}

static inline int cs_get_lookupu_type(
    cs_state &cs, cs_value &arg, cs_ident *&id, uint32_t op
) {
    if (arg.get_type() != cs_value_type::STRING) {
        return -2; /* default case */
    }
    id = cs.get_ident(arg.get_str());
    if (id) {
        switch(id->get_type()) {
            case cs_ident_type::ALIAS:
                if (id->get_flags() & CS_IDF_UNKNOWN) {
                    break;
                }
                if ((id->get_index() < MaxArguments) && !ident_is_used_arg(id, cs)) {
                    return ID_UNKNOWN;
                }
                return ID_ALIAS;
            case cs_ident_type::SVAR:
                return ID_SVAR;
            case cs_ident_type::IVAR:
                return ID_IVAR;
            case cs_ident_type::FVAR:
                return ID_FVAR;
            case cs_ident_type::COMMAND: {
                arg.set_none();
                cs_valarray<cs_value, MaxArguments> buf{cs};
                callcommand(
                    cs, static_cast<cs_command_impl *>(id),
                    &buf[0], arg, 0, true
                );
                force_arg(arg, op & CS_CODE_RET_MASK);
                return -2; /* ignore */
            }
            default:
                return ID_UNKNOWN;
        }
    }
    throw cs_error(cs, "unknown alias lookup: %s", arg.get_str().data());
}

static uint32_t *runcode(cs_state &cs, uint32_t *code, cs_value &result) {
    result.set_none();
    RunDepthRef level{cs}; /* incr and decr on scope exit */
    int numargs = 0;
    cs_valarray<cs_value, MaxArguments + MaxResults> args{cs};
    auto &chook = cs.get_call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        uint32_t op = *code++;
        switch (op & 0xFF) {
            case CS_CODE_START:
            case CS_CODE_OFFSET:
                continue;

            case CS_CODE_NULL | CS_RET_NULL:
                result.set_none();
                continue;
            case CS_CODE_NULL | CS_RET_STRING:
                result.set_str("");
                continue;
            case CS_CODE_NULL | CS_RET_INT:
                result.set_int(0);
                continue;
            case CS_CODE_NULL | CS_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case CS_CODE_FALSE | CS_RET_STRING:
                result.set_str("0");
                continue;
            case CS_CODE_FALSE | CS_RET_NULL:
            case CS_CODE_FALSE | CS_RET_INT:
                result.set_int(0);
                continue;
            case CS_CODE_FALSE | CS_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case CS_CODE_TRUE | CS_RET_STRING:
                result.set_str("1");
                continue;
            case CS_CODE_TRUE | CS_RET_NULL:
            case CS_CODE_TRUE | CS_RET_INT:
                result.set_int(1);
                continue;
            case CS_CODE_TRUE | CS_RET_FLOAT:
                result.set_float(1.0f);
                continue;

            case CS_CODE_NOT | CS_RET_STRING:
                --numargs;
                result.set_str(args[numargs].get_bool() ? "0" : "1");
                continue;
            case CS_CODE_NOT | CS_RET_NULL:
            case CS_CODE_NOT | CS_RET_INT:
                --numargs;
                result.set_int(!args[numargs].get_bool());
                continue;
            case CS_CODE_NOT | CS_RET_FLOAT:
                --numargs;
                result.set_float(cs_float(!args[numargs].get_bool()));
                continue;

            case CS_CODE_POP:
                numargs -= 1;
                continue;
            case CS_CODE_ENTER:
                code = runcode(cs, code, args[numargs++]);
                continue;
            case CS_CODE_ENTER_RESULT:
                code = runcode(cs, code, result);
                continue;
            case CS_CODE_EXIT | CS_RET_STRING:
            case CS_CODE_EXIT | CS_RET_INT:
            case CS_CODE_EXIT | CS_RET_FLOAT:
                force_arg(result, op & CS_CODE_RET_MASK);
            /* fallthrough */
            case CS_CODE_EXIT | CS_RET_NULL:
                return code;
            case CS_CODE_RESULT_ARG | CS_RET_STRING:
            case CS_CODE_RESULT_ARG | CS_RET_INT:
            case CS_CODE_RESULT_ARG | CS_RET_FLOAT:
                force_arg(result, op & CS_CODE_RET_MASK);
            /* fallthrough */
            case CS_CODE_RESULT_ARG | CS_RET_NULL:
                args[numargs++] = std::move(result);
                continue;
            case CS_CODE_PRINT:
                cs.print_var(*static_cast<cs_var *>(cs.p_state->identmap[op >> 8]));
                continue;

            case CS_CODE_LOCAL: {
                int numlocals = op >> 8, offset = numargs - numlocals;
                cs_valarray<cs_ident_stack, MaxArguments> locals{cs};
                for (int i = 0; i < numlocals; ++i) {
                    cs_push_alias(cs, args[offset + i].get_ident(), locals[i]);
                }
                call_with_cleanup([&]() {
                    code = runcode(cs, code, result);
                }, [&]() {
                    for (int i = offset; i < numargs; i++) {
                        cs_pop_alias(args[i].get_ident());
                    }
                });
                return code;
            }

            case CS_CODE_DO_ARGS | CS_RET_NULL:
            case CS_CODE_DO_ARGS | CS_RET_STRING:
            case CS_CODE_DO_ARGS | CS_RET_INT:
            case CS_CODE_DO_ARGS | CS_RET_FLOAT:
                cs_do_args(cs, [&]() {
                    cs.run(args[--numargs].get_code(), result);
                    force_arg(result, op & CS_CODE_RET_MASK);
                });
                continue;
            /* fallthrough */
            case CS_CODE_DO | CS_RET_NULL:
            case CS_CODE_DO | CS_RET_STRING:
            case CS_CODE_DO | CS_RET_INT:
            case CS_CODE_DO | CS_RET_FLOAT:
                cs.run(args[--numargs].get_code(), result);
                force_arg(result, op & CS_CODE_RET_MASK);
                continue;

            case CS_CODE_JUMP: {
                uint32_t len = op >> 8;
                code += len;
                continue;
            }
            case CS_CODE_JUMP_B | CS_CODE_FLAG_TRUE: {
                uint32_t len = op >> 8;
                if (args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CS_CODE_JUMP_B | CS_CODE_FLAG_FALSE: {
                uint32_t len = op >> 8;
                if (!args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case CS_CODE_JUMP_RESULT | CS_CODE_FLAG_TRUE: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == cs_value_type::CODE) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CS_CODE_JUMP_RESULT | CS_CODE_FLAG_FALSE: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == cs_value_type::CODE) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
                }
                if (!result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case CS_CODE_BREAK | CS_CODE_FLAG_FALSE:
                if (cs.is_in_loop()) {
                    throw CsBreakException();
                } else {
                    throw cs_error(cs, "no loop to break");
                }
                break;
            case CS_CODE_BREAK | CS_CODE_FLAG_TRUE:
                if (cs.is_in_loop()) {
                    throw CsContinueException();
                } else {
                    throw cs_error(cs, "no loop to continue");
                }
                break;

            case CS_CODE_VAL | CS_RET_STRING: {
                uint32_t len = op >> 8;
                args[numargs++].set_str(std::string_view{
                    reinterpret_cast<char const *>(code), len
                });
                code += len / sizeof(uint32_t) + 1;
                continue;
            }
            case CS_CODE_VAL_INT | CS_RET_STRING: {
                char s[4] = {
                    char((op >> 8) & 0xFF),
                    char((op >> 16) & 0xFF),
                    char((op >> 24) & 0xFF), '\0'
                };
                /* gotta cast or r.size() == potentially 3 */
                args[numargs++].set_str(static_cast<char const *>(s));
                continue;
            }
            case CS_CODE_VAL | CS_RET_NULL:
            case CS_CODE_VAL_INT | CS_RET_NULL:
                args[numargs++].set_none();
                continue;
            case CS_CODE_VAL | CS_RET_INT:
                args[numargs++].set_int(
                    *reinterpret_cast<cs_int const *>(code)
                );
                code += CsTypeStorageSize<cs_int>;
                continue;
            case CS_CODE_VAL_INT | CS_RET_INT:
                args[numargs++].set_int(cs_int(op) >> 8);
                continue;
            case CS_CODE_VAL | CS_RET_FLOAT:
                args[numargs++].set_float(
                    *reinterpret_cast<cs_float const *>(code)
                );
                code += CsTypeStorageSize<cs_float>;
                continue;
            case CS_CODE_VAL_INT | CS_RET_FLOAT:
                args[numargs++].set_float(cs_float(cs_int(op) >> 8));
                continue;

            case CS_CODE_DUP | CS_RET_NULL:
                args[numargs - 1].get_val(args[numargs]);
                numargs++;
                continue;
            case CS_CODE_DUP | CS_RET_INT:
                args[numargs].set_int(args[numargs - 1].get_int());
                numargs++;
                continue;
            case CS_CODE_DUP | CS_RET_FLOAT:
                args[numargs].set_float(args[numargs - 1].get_float());
                numargs++;
                continue;
            case CS_CODE_DUP | CS_RET_STRING:
                args[numargs] = args[numargs - 1];
                args[numargs].force_str();
                numargs++;
                continue;

            case CS_CODE_FORCE | CS_RET_STRING:
                args[numargs - 1].force_str();
                continue;
            case CS_CODE_FORCE | CS_RET_INT:
                args[numargs - 1].force_int();
                continue;
            case CS_CODE_FORCE | CS_RET_FLOAT:
                args[numargs - 1].force_float();
                continue;

            case CS_CODE_RESULT | CS_RET_NULL:
                result = std::move(args[--numargs]);
                continue;
            case CS_CODE_RESULT | CS_RET_STRING:
            case CS_CODE_RESULT | CS_RET_INT:
            case CS_CODE_RESULT | CS_RET_FLOAT:
                result = std::move(args[--numargs]);
                force_arg(result, op & CS_CODE_RET_MASK);
                continue;

            case CS_CODE_EMPTY | CS_RET_NULL:
                args[numargs++].set_code(
                    bcode_get_empty(cs_get_sstate(cs)->empty, CS_VAL_NULL)
                );
                break;
            case CS_CODE_EMPTY | CS_RET_STRING:
                args[numargs++].set_code(
                    bcode_get_empty(cs_get_sstate(cs)->empty, CS_VAL_STRING)
                );
                break;
            case CS_CODE_EMPTY | CS_RET_INT:
                args[numargs++].set_code(
                    bcode_get_empty(cs_get_sstate(cs)->empty, CS_VAL_INT)
                );
                break;
            case CS_CODE_EMPTY | CS_RET_FLOAT:
                args[numargs++].set_code(
                    bcode_get_empty(cs_get_sstate(cs)->empty, CS_VAL_FLOAT)
                );
                break;
            case CS_CODE_BLOCK: {
                uint32_t len = op >> 8;
                args[numargs++].set_code(
                    reinterpret_cast<cs_bcode *>(code + 1)
                );
                code += len;
                continue;
            }
            case CS_CODE_COMPILE: {
                cs_value &arg = args[numargs - 1];
                cs_gen_state gs(cs);
                switch (arg.get_type()) {
                    case cs_value_type::INT:
                        gs.code.reserve(8);
                        gs.code.push_back(CS_CODE_START);
                        gs.gen_int(arg.get_int());
                        gs.code.push_back(CS_CODE_RESULT);
                        gs.code.push_back(CS_CODE_EXIT);
                        break;
                    case cs_value_type::FLOAT:
                        gs.code.reserve(8);
                        gs.code.push_back(CS_CODE_START);
                        gs.gen_float(arg.get_float());
                        gs.code.push_back(CS_CODE_RESULT);
                        gs.code.push_back(CS_CODE_EXIT);
                        break;
                    case cs_value_type::STRING:
                        gs.code.reserve(64);
                        gs.gen_main(arg.get_str());
                        break;
                    default:
                        gs.code.reserve(8);
                        gs.code.push_back(CS_CODE_START);
                        gs.gen_null();
                        gs.code.push_back(CS_CODE_RESULT);
                        gs.code.push_back(CS_CODE_EXIT);
                        break;
                }
                gs.done();
                uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
                memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
                arg.set_code(
                    reinterpret_cast<cs_bcode *>(cbuf + 1)
                );
                continue;
            }
            case CS_CODE_COND: {
                cs_value &arg = args[numargs - 1];
                switch (arg.get_type()) {
                    case cs_value_type::STRING: {
                        std::string_view s = arg.get_str();
                        if (!s.empty()) {
                            cs_gen_state gs(cs);
                            gs.code.reserve(64);
                            gs.gen_main(s);
                            gs.done();
                            uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
                            memcpy(
                                cbuf, gs.code.data(),
                                gs.code.size() * sizeof(uint32_t)
                            );
                            arg.set_code(reinterpret_cast<cs_bcode *>(cbuf + 1));
                        } else {
                            arg.force_none();
                        }
                        break;
                    }
                    default:
                        break;
                }
                continue;
            }

            case CS_CODE_IDENT:
                args[numargs++].set_ident(cs.p_state->identmap[op >> 8]);
                continue;
            case CS_CODE_IDENT_ARG: {
                cs_alias *a = static_cast<cs_alias *>(
                    cs.p_state->identmap[op >> 8]
                );
                if (!ident_is_used_arg(a, cs)) {
                    cs_value nv{cs};
                    static_cast<cs_alias_impl *>(a)->push_arg(
                        nv, cs.p_callstack->argstack[a->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << a->get_index();
                }
                args[numargs++].set_ident(a);
                continue;
            }
            case CS_CODE_IDENT_U: {
                cs_value &arg = args[numargs - 1];
                cs_ident *id = cs.p_state->identmap[DummyIdx];
                if (arg.get_type() == cs_value_type::STRING) {
                    id = cs.new_ident(arg.get_str());
                }
                if ((id->get_index() < MaxArguments) && !ident_is_used_arg(id, cs)) {
                    cs_value nv{cs};
                    static_cast<cs_alias_impl *>(id)->push_arg(
                        nv, cs.p_callstack->argstack[id->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << id->get_index();
                }
                arg.set_ident(id);
                continue;
            }

            case CS_CODE_LOOKUP_U | CS_RET_STRING: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg = static_cast<cs_alias *>(id)->get_value();
                        arg.force_str();
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<cs_fvar *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_UNKNOWN:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP | CS_RET_STRING:
                args[numargs] = cs_get_lookup_id(cs, op)->get_value();
                args[numargs++].force_str();
                continue;
            case CS_CODE_LOOKUP_ARG | CS_RET_STRING: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs] = a->get_value();
                    args[numargs++].force_str();
                }
                continue;
            }
            case CS_CODE_LOOKUP_U | CS_RET_INT: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_int(
                            static_cast<cs_alias *>(id)->get_value().get_int()
                        );
                        continue;
                    case ID_SVAR:
                        arg.set_int(parse_int(
                            static_cast<cs_svar *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_int(
                            cs_int(static_cast<cs_fvar *>(id)->get_value())
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_int(0);
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP | CS_RET_INT:
                args[numargs++].set_int(
                    cs_get_lookup_id(cs, op)->get_value().get_int()
                );
                continue;
            case CS_CODE_LOOKUP_ARG | CS_RET_INT: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_int(0);
                } else {
                    args[numargs++].set_int(a->get_value().get_int());
                }
                continue;
            }
            case CS_CODE_LOOKUP_U | CS_RET_FLOAT: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_float(
                            static_cast<cs_alias *>(id)->get_value().get_float()
                        );
                        continue;
                    case ID_SVAR:
                        arg.set_float(parse_float(
                            static_cast<cs_svar *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_float(cs_float(
                            static_cast<cs_ivar *>(id)->get_value()
                        ));
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<cs_fvar *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_float(cs_float(0));
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP | CS_RET_FLOAT:
                args[numargs++].set_float(
                    cs_get_lookup_id(cs, op)->get_value().get_float()
                );
                continue;
            case CS_CODE_LOOKUP_ARG | CS_RET_FLOAT: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_float(cs_float(0));
                } else {
                    args[numargs++].set_float(a->get_value().get_float());
                }
                continue;
            }
            case CS_CODE_LOOKUP_U | CS_RET_NULL: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<cs_alias *>(id)->get_value().get_val(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<cs_fvar *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_none();
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP | CS_RET_NULL:
                cs_get_lookup_id(cs, op)->get_value().get_val(args[numargs++]);
                continue;
            case CS_CODE_LOOKUP_ARG | CS_RET_NULL: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_none();
                } else {
                    a->get_value().get_val(args[numargs++]);
                }
                continue;
            }

            case CS_CODE_LOOKUP_MU | CS_RET_STRING: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg = static_cast<cs_alias *>(id)->get_value();
                        arg.force_str();
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<cs_fvar *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_UNKNOWN:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP_M | CS_RET_STRING:
                args[numargs] = cs_get_lookup_id(cs, op)->get_value();
                args[numargs++].force_str();
                continue;
            case CS_CODE_LOOKUP_MARG | CS_RET_STRING: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs] = a->get_value();
                    args[numargs++].force_str();
                }
                continue;
            }
            case CS_CODE_LOOKUP_MU | CS_RET_NULL: {
                cs_ident *id = nullptr;
                cs_value &arg = args[numargs - 1];
                switch (cs_get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<cs_alias *>(id)->get_cval(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<cs_svar *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<cs_ivar *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<cs_fvar *>(id)->get_value());
                        continue;
                    case ID_UNKNOWN:
                        arg.set_none();
                        continue;
                    default:
                        continue;
                }
            }
            case CS_CODE_LOOKUP_M | CS_RET_NULL:
                cs_get_lookup_id(cs, op)->get_cval(args[numargs++]);
                continue;
            case CS_CODE_LOOKUP_MARG | CS_RET_NULL: {
                cs_alias *a = cs_get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_none();
                } else {
                    a->get_cval(args[numargs++]);
                }
                continue;
            }

            case CS_CODE_SVAR | CS_RET_STRING:
            case CS_CODE_SVAR | CS_RET_NULL:
                args[numargs++].set_str(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CS_CODE_SVAR | CS_RET_INT:
                args[numargs++].set_int(parse_int(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CS_CODE_SVAR | CS_RET_FLOAT:
                args[numargs++].set_float(parse_float(static_cast<cs_svar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CS_CODE_SVAR1:
                cs.set_var_str_checked(
                    static_cast<cs_svar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_str()
                );
                continue;

            case CS_CODE_IVAR | CS_RET_INT:
            case CS_CODE_IVAR | CS_RET_NULL:
                args[numargs++].set_int(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CS_CODE_IVAR | CS_RET_STRING:
                args[numargs].set_int(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                args[numargs++].force_str();
                continue;
            case CS_CODE_IVAR | CS_RET_FLOAT:
                args[numargs++].set_float(cs_float(static_cast<cs_ivar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CS_CODE_IVAR1:
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_int()
                );
                continue;
            case CS_CODE_IVAR2:
                numargs -= 2;
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                );
                continue;
            case CS_CODE_IVAR3:
                numargs -= 3;
                cs.set_var_int_checked(
                    static_cast<cs_ivar *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                        | (args[numargs + 2].get_int()));
                continue;

            case CS_CODE_FVAR | CS_RET_FLOAT:
            case CS_CODE_FVAR | CS_RET_NULL:
                args[numargs++].set_float(static_cast<cs_fvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case CS_CODE_FVAR | CS_RET_STRING:
                args[numargs].set_int(static_cast<cs_fvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                args[numargs++].force_str();
                continue;
            case CS_CODE_FVAR | CS_RET_INT:
                args[numargs++].set_int(int(static_cast<cs_fvar *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case CS_CODE_FVAR1:
                cs.set_var_float_checked(
                    static_cast<cs_fvar *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_float()
                );
                continue;

            case CS_CODE_COM | CS_RET_NULL:
            case CS_CODE_COM | CS_RET_STRING:
            case CS_CODE_COM | CS_RET_FLOAT:
            case CS_CODE_COM | CS_RET_INT: {
                cs_command_impl *id = static_cast<cs_command_impl *>(
                    cs.p_state->identmap[op >> 8]
                );
                int offset = numargs - id->get_num_args();
                result.force_none();
                id->call(cs, std::span<cs_value>{
                    &args[0] + offset, std::size_t(id->get_num_args())
                }, result);
                force_arg(result, op & CS_CODE_RET_MASK);
                numargs = offset;
                continue;
            }

            case CS_CODE_COM_V | CS_RET_NULL:
            case CS_CODE_COM_V | CS_RET_STRING:
            case CS_CODE_COM_V | CS_RET_FLOAT:
            case CS_CODE_COM_V | CS_RET_INT: {
                cs_command_impl *id = static_cast<cs_command_impl *>(
                    cs.p_state->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F,
                            offset = numargs - callargs;
                result.force_none();
                id->call(cs, std::span{&args[offset], callargs}, result);
                force_arg(result, op & CS_CODE_RET_MASK);
                numargs = offset;
                continue;
            }
            case CS_CODE_COM_C | CS_RET_NULL:
            case CS_CODE_COM_C | CS_RET_STRING:
            case CS_CODE_COM_C | CS_RET_FLOAT:
            case CS_CODE_COM_C | CS_RET_INT: {
                cs_command_impl *id = static_cast<cs_command_impl *>(
                    cs.p_state->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F,
                            offset = numargs - callargs;
                result.force_none();
                {
                    cs_value tv{cs};
                    tv.set_str(value_list_concat(cs, std::span{
                        &args[offset], callargs
                    }, " "));
                    id->call(cs, std::span<cs_value>{&tv, 1}, result);
                }
                force_arg(result, op & CS_CODE_RET_MASK);
                numargs = offset;
                continue;
            }

            case CS_CODE_CONC | CS_RET_NULL:
            case CS_CODE_CONC | CS_RET_STRING:
            case CS_CODE_CONC | CS_RET_FLOAT:
            case CS_CODE_CONC | CS_RET_INT:
            case CS_CODE_CONC_W | CS_RET_NULL:
            case CS_CODE_CONC_W | CS_RET_STRING:
            case CS_CODE_CONC_W | CS_RET_FLOAT:
            case CS_CODE_CONC_W | CS_RET_INT: {
                std::size_t numconc = op >> 8;
                auto buf = value_list_concat(
                    cs, std::span{&args[numargs - numconc], numconc},
                    ((op & CS_CODE_OP_MASK) == CS_CODE_CONC) ? " " : ""
                );
                numargs = numargs - numconc;
                args[numargs].set_str(buf);
                force_arg(args[numargs], op & CS_CODE_RET_MASK);
                numargs++;
                continue;
            }

            case CS_CODE_CONC_M | CS_RET_NULL:
            case CS_CODE_CONC_M | CS_RET_STRING:
            case CS_CODE_CONC_M | CS_RET_FLOAT:
            case CS_CODE_CONC_M | CS_RET_INT: {
                std::size_t numconc = op >> 8;
                auto buf = value_list_concat(
                    cs, std::span{&args[numargs - numconc], numconc}
                );
                numargs = numargs - numconc;
                result.set_str(buf);
                force_arg(result, op & CS_CODE_RET_MASK);
                continue;
            }

            case CS_CODE_ALIAS:
                static_cast<cs_alias_impl *>(
                    cs.p_state->identmap[op >> 8]
                )->set_alias(cs, args[--numargs]);
                continue;
            case CS_CODE_ALIAS_ARG:
                static_cast<cs_alias_impl *>(
                    cs.p_state->identmap[op >> 8]
                )->set_arg(cs, args[--numargs]);
                continue;
            case CS_CODE_ALIAS_U:
                numargs -= 2;
                cs.set_alias(
                    args[numargs].get_str(), std::move(args[numargs + 1])
                );
                continue;

            case CS_CODE_CALL | CS_RET_NULL:
            case CS_CODE_CALL | CS_RET_STRING:
            case CS_CODE_CALL | CS_RET_FLOAT:
            case CS_CODE_CALL | CS_RET_INT: {
                result.force_none();
                cs_ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (id->get_flags() & CS_IDF_UNKNOWN) {
                    force_arg(result, op & CS_CODE_RET_MASK);
                    throw cs_error(
                        cs, "unknown command: %s", id->get_name().data()
                    );
                }
                cs_call_alias(
                    cs, static_cast<cs_alias *>(id), &args[0], result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }
            case CS_CODE_CALL_ARG | CS_RET_NULL:
            case CS_CODE_CALL_ARG | CS_RET_STRING:
            case CS_CODE_CALL_ARG | CS_RET_FLOAT:
            case CS_CODE_CALL_ARG | CS_RET_INT: {
                result.force_none();
                cs_ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (!ident_is_used_arg(id, cs)) {
                    numargs = offset;
                    force_arg(result, op & CS_CODE_RET_MASK);
                    continue;
                }
                cs_call_alias(
                    cs, static_cast<cs_alias *>(id), &args[0], result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }

            case CS_CODE_CALL_U | CS_RET_NULL:
            case CS_CODE_CALL_U | CS_RET_STRING:
            case CS_CODE_CALL_U | CS_RET_FLOAT:
            case CS_CODE_CALL_U | CS_RET_INT: {
                int callargs = op >> 8, offset = numargs - callargs;
                cs_value &idarg = args[offset - 1];
                if (idarg.get_type() != cs_value_type::STRING) {
litval:
                    result = std::move(idarg);
                    force_arg(result, op & CS_CODE_RET_MASK);
                    numargs = offset - 1;
                    continue;
                }
                auto idn = idarg.get_str();
                cs_ident *id = cs.get_ident(idn);
                if (!id) {
noid:
                    if (cs_check_num(idn)) {
                        goto litval;
                    }
                    result.force_none();
                    force_arg(result, op & CS_CODE_RET_MASK);
                    std::string_view ids{idn};
                    throw cs_error(
                        cs, "unknown command: %s", ids.data()
                    );
                }
                result.force_none();
                switch (id->get_raw_type()) {
                    default:
                        if (!cs_ident_has_cb(id)) {
                            numargs = offset - 1;
                            force_arg(result, op & CS_CODE_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND:
                        callcommand(
                            cs, static_cast<cs_command_impl *>(id),
                            &args[offset], result, callargs
                        );
                        force_arg(result, op & CS_CODE_RET_MASK);
                        numargs = offset - 1;
                        continue;
                    case ID_LOCAL: {
                        cs_valarray<cs_ident_stack, MaxArguments> locals{cs};
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            cs_push_alias(cs, cs.force_ident(
                                args[offset + j]
                            ), locals[j]);
                        }
                        call_with_cleanup([&]() {
                            code = runcode(cs, code, result);
                        }, [&]() {
                            for (size_t j = 0; j < size_t(callargs); ++j) {
                                cs_pop_alias(args[offset + j].get_ident());
                            }
                        });
                        return code;
                    }
                    case ID_IVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_int_checked(
                                static_cast<cs_ivar *>(id),
                                std::span{&args[offset], std::size_t(callargs)}
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CS_CODE_RET_MASK);
                        continue;
                    case ID_FVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_float_checked(
                                static_cast<cs_fvar *>(id),
                                args[offset].force_float()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CS_CODE_RET_MASK);
                        continue;
                    case ID_SVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<cs_var *>(id));
                        } else {
                            cs.set_var_str_checked(
                                static_cast<cs_svar *>(id),
                                args[offset].force_str()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & CS_CODE_RET_MASK);
                        continue;
                    case ID_ALIAS: {
                        cs_alias *a = static_cast<cs_alias *>(id);
                        if (
                            (a->get_index() < MaxArguments) &&
                            !ident_is_used_arg(a, cs)
                        ) {
                            numargs = offset - 1;
                            force_arg(result, op & CS_CODE_RET_MASK);
                            continue;
                        }
                        if (a->get_value().get_type() == cs_value_type::NONE) {
                            goto noid;
                        }
                        cs_call_alias(
                            cs, a, &args[0], result, callargs, numargs,
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
    cs_state &cs, std::string_view file, std::string_view code,
    cs_value &ret
) {
    cs_gen_state gs(cs);
    gs.src_name = file;
    gs.code.reserve(64);
    gs.gen_main(code, CS_VAL_ANY);
    gs.done();
    uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
    memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
    bcode_incr(cbuf);
    runcode(cs, cbuf + 1, ret);
    bcode_decr(cbuf);
}

void cs_state::run(std::string_view code, cs_value &ret) {
    cs_run(*this, std::string_view{}, code, ret);
}

void cs_state::run(
    std::string_view code, cs_value &ret, std::string_view source
) {
    cs_run(*this, source, code, ret);
}

void cs_state::run(cs_ident *id, std::span<cs_value> args, cs_value &ret) {
    int nargs = int(args.size());
    ret.set_none();
    RunDepthRef level{*this}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!cs_ident_has_cb(id)) {
                    break;
                }
            /* fallthrough */
            case cs_ident_type::COMMAND:
                if (nargs < static_cast<cs_command_impl *>(id)->get_num_args()) {
                    cs_valarray<cs_value, MaxArguments> buf{*this};
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        buf[i] = args[i];
                    }
                    callcommand(
                        *this, static_cast<cs_command_impl *>(id), &buf[0], ret,
                        nargs, false
                    );
                } else {
                    callcommand(
                        *this, static_cast<cs_command_impl *>(id), &args[0],
                        ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            case cs_ident_type::IVAR:
                if (args.empty()) {
                    print_var(*static_cast<cs_var *>(id));
                } else {
                    set_var_int_checked(static_cast<cs_ivar *>(id), args);
                }
                break;
            case cs_ident_type::FVAR:
                if (args.empty()) {
                    print_var(*static_cast<cs_var *>(id));
                } else {
                    set_var_float_checked(
                        static_cast<cs_fvar *>(id), args[0].force_float()
                    );
                }
                break;
            case cs_ident_type::SVAR:
                if (args.empty()) {
                    print_var(*static_cast<cs_var *>(id));
                } else {
                    set_var_str_checked(
                        static_cast<cs_svar *>(id), args[0].force_str()
                    );
                }
                break;
            case cs_ident_type::ALIAS: {
                cs_alias *a = static_cast<cs_alias *>(id);
                if (
                    (a->get_index() < MaxArguments) && !ident_is_used_arg(a, *this)
                ) {
                    break;
                }
                if (a->get_value().get_type() == cs_value_type::NONE) {
                    break;
                }
                cs_call_alias(
                    *this, a, &args[0], ret, nargs, nargs, 0, 0, CS_RET_NULL
                );
                break;
            }
        }
    }
}

cs_value cs_state::run(cs_bcode *code) {
    cs_value ret{*this};
    run(code, ret);
    return ret;
}

cs_value cs_state::run(std::string_view code) {
    cs_value ret{*this};
    run(code, ret);
    return ret;
}

cs_value cs_state::run(std::string_view code, std::string_view source) {
    cs_value ret{*this};
    run(code, ret, source);
    return ret;
}

cs_value cs_state::run(cs_ident *id, std::span<cs_value> args) {
    cs_value ret{*this};
    run(id, args, ret);
    return ret;
}

cs_loop_state cs_state::run_loop(cs_bcode *code, cs_value &ret) {
    ++p_inloop;
    try {
        run(code, ret);
    } catch (CsBreakException) {
        --p_inloop;
        return cs_loop_state::BREAK;
    } catch (CsContinueException) {
        --p_inloop;
        return cs_loop_state::CONTINUE;
    } catch (...) {
        --p_inloop;
        throw;
    }
    return cs_loop_state::NORMAL;
}

cs_loop_state cs_state::run_loop(cs_bcode *code) {
    cs_value ret{*this};
    return run_loop(code, ret);
}

} /* namespace cscript */
