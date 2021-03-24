#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_std.hh"
#include "cs_parser.hh"

#include <cstdio>
#include <limits>

namespace cubescript {

static inline bool ident_has_cb(ident *id) {
    if (!id->is_command() && !id->is_special()) {
        return false;
    }
    return !!static_cast<command_impl *>(id)->p_cb_cftv;
}

static inline void push_alias(state &cs, ident *id, ident_stack &st) {
    if (id->is_alias() && (id->get_index() >= MAX_ARGUMENTS)) {
        any_value nv{cs};
        static_cast<alias_impl *>(id)->push_arg(nv, st);
    }
}

static inline void pop_alias(ident *id) {
    if (id->is_alias() && (id->get_index() >= MAX_ARGUMENTS)) {
        static_cast<alias_impl *>(id)->pop_arg();
    }
}

stack_state::stack_state(state &cs, stack_state_node *nd, bool gap):
    p_state(cs), p_node(nd), p_gap(gap)
{}
stack_state::stack_state(stack_state &&st):
    p_state(st.p_state), p_node(st.p_node), p_gap(st.p_gap)
{
    st.p_node = nullptr;
    st.p_gap = false;
}

stack_state::~stack_state() {
    size_t len = 0;
    for (stack_state_node const *nd = p_node; nd; nd = nd->next) {
        ++len;
    }
    p_state.p_state->destroy_array(p_node, len);
}

stack_state &stack_state::operator=(stack_state &&st) {
    p_node = st.p_node;
    p_gap = st.p_gap;
    st.p_node = nullptr;
    st.p_gap = false;
    return *this;
}

stack_state_node const *stack_state::get() const {
    return p_node;
}

bool stack_state::gap() const {
    return p_gap;
}

static inline uint32_t *forcecode(state &cs, any_value &v) {
    auto *code = v.get_code();
    if (!code) {
        codegen_state gs(cs);
        gs.code.reserve(64);
        gs.gen_main(v.get_str());
        gs.done();
        uint32_t *cbuf = bcode_alloc(cs, gs.code.size());
        memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        v.set_code(reinterpret_cast<bcode *>(cbuf + 1));
        code = v.get_code();
    }
    return code->get_raw();
}

static inline void forcecond(state &cs, any_value &v) {
    switch (v.get_type()) {
        case value_type::STRING:
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

static inline void force_arg(any_value &v, int type) {
    switch (type) {
        case BC_RET_STRING:
            if (v.get_type() != value_type::STRING) {
                v.force_str();
            }
            break;
        case BC_RET_INT:
            if (v.get_type() != value_type::INT) {
                v.force_int();
            }
            break;
        case BC_RET_FLOAT:
            if (v.get_type() != value_type::FLOAT) {
                v.force_float();
            }
            break;
    }
}

static inline void callcommand(
    state &cs, command_impl *id, any_value *args, any_value &res,
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
                    args[i].set_int(std::numeric_limits<integer_type>::min());
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
                        bcode_get_empty(state_get_internal(cs)->empty, VAL_NULL)
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
                    args[i].set_ident(cs.p_state->identmap[ID_IDX_DUMMY]);
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
                args[i].set_int(integer_type(lookup ? -1 : i - fakeargs));
                break;
            case 'C': {
                i = std::max(i + 1, numargs);
                any_value tv{cs};
                tv.set_str(concat_values(
                    cs, std::span{args, std::size_t(i)}, " "
                ));
                static_cast<command_impl *>(id)->call(
                    cs, std::span<any_value>(&tv, &tv + 1), res
                );
                return;
            }
            case 'V':
                i = std::max(i + 1, numargs);
                static_cast<command_impl *>(id)->call(
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
    static_cast<command_impl *>(id)->call(
        cs, std::span<any_value>{args, std::size_t(i)}, res
    );
}

static uint32_t *runcode(state &cs, uint32_t *code, any_value &result);

static inline void call_alias(
    state &cs, alias *a, any_value *args, any_value &result,
    int callargs, int &nargs, int offset, int skip, uint32_t op
) {
    integer_var *anargs = static_cast<integer_var *>(cs.p_state->identmap[ID_IDX_NUMARGS]);
    valarray<ident_stack, MAX_ARGUMENTS> argstack{cs};
    for(int i = 0; i < callargs; i++) {
        static_cast<alias_impl *>(cs.p_state->identmap[i])->push_arg(
            args[offset + i], argstack[i], false
        );
    }
    int oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = cs.identflags;
    cs.identflags |= a->get_flags()&IDENT_FLAG_OVERRIDDEN;
    ident_link aliaslink = {
        a, cs.p_callstack, (1<<callargs)-1, &argstack[0]
    };
    cs.p_callstack = &aliaslink;
    uint32_t *codep = static_cast<alias_impl *>(a)->compile_code(cs)->get_raw();
    bcode_incr(codep);
    call_with_cleanup([&]() {
        runcode(cs, codep+1, result);
    }, [&]() {
        bcode_decr(codep);
        cs.p_callstack = aliaslink.next;
        cs.identflags = oldflags;
        for (int i = 0; i < callargs; i++) {
            static_cast<alias_impl *>(cs.p_state->identmap[i])->pop_arg();
        }
        int argmask = aliaslink.usedargs & int(~0U << callargs);
        for (; argmask; ++callargs) {
            if (argmask & (1 << callargs)) {
                static_cast<alias_impl *>(
                    cs.p_state->identmap[callargs]
                )->pop_arg();
                argmask &= ~(1 << callargs);
            }
        }
        force_arg(result, op & BC_INST_RET_MASK);
        anargs->set_value(oldargs);
        nargs = offset - skip;
    });
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

struct RunDepthRef {
    RunDepthRef() = delete;
    RunDepthRef(state &cs) {
        if (rundepth >= MaxRunDepth) {
            throw error(cs, "exceeded recursion limit");
        }
        ++rundepth;
    }
    RunDepthRef(RunDepthRef const &) = delete;
    RunDepthRef(RunDepthRef &&) = delete;
    ~RunDepthRef() { --rundepth; }
};

static inline alias *get_lookup_id(state &cs, uint32_t op) {
    ident *id = cs.p_state->identmap[op >> 8];
    if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
        throw error(cs, "unknown alias lookup: %s", id->get_name().data());
    }
    return static_cast<alias *>(id);
}

static inline alias *get_lookuparg_id(state &cs, uint32_t op) {
    ident *id = cs.p_state->identmap[op >> 8];
    if (!ident_is_used_arg(id, cs)) {
        return nullptr;
    }
    return static_cast<alias *>(id);
}

static inline int get_lookupu_type(
    state &cs, any_value &arg, ident *&id, uint32_t op
) {
    if (arg.get_type() != value_type::STRING) {
        return -2; /* default case */
    }
    id = cs.get_ident(arg.get_str());
    if (id) {
        switch(id->get_type()) {
            case ident_type::ALIAS:
                if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
                    break;
                }
                if ((id->get_index() < MAX_ARGUMENTS) && !ident_is_used_arg(id, cs)) {
                    return ID_UNKNOWN;
                }
                return ID_ALIAS;
            case ident_type::SVAR:
                return ID_SVAR;
            case ident_type::IVAR:
                return ID_IVAR;
            case ident_type::FVAR:
                return ID_FVAR;
            case ident_type::COMMAND: {
                arg.set_none();
                valarray<any_value, MAX_ARGUMENTS> buf{cs};
                callcommand(
                    cs, static_cast<command_impl *>(id),
                    &buf[0], arg, 0, true
                );
                force_arg(arg, op & BC_INST_RET_MASK);
                return -2; /* ignore */
            }
            default:
                return ID_UNKNOWN;
        }
    }
    throw error(cs, "unknown alias lookup: %s", arg.get_str().data());
}

static uint32_t *runcode(state &cs, uint32_t *code, any_value &result) {
    result.set_none();
    RunDepthRef level{cs}; /* incr and decr on scope exit */
    int numargs = 0;
    valarray<any_value, MAX_ARGUMENTS + MAX_RESULTS> args{cs};
    auto &chook = cs.get_call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        uint32_t op = *code++;
        switch (op & 0xFF) {
            case BC_INST_START:
            case BC_INST_OFFSET:
                continue;

            case BC_INST_NULL | BC_RET_NULL:
                result.set_none();
                continue;
            case BC_INST_NULL | BC_RET_STRING:
                result.set_str("");
                continue;
            case BC_INST_NULL | BC_RET_INT:
                result.set_int(0);
                continue;
            case BC_INST_NULL | BC_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case BC_INST_FALSE | BC_RET_STRING:
                result.set_str("0");
                continue;
            case BC_INST_FALSE | BC_RET_NULL:
            case BC_INST_FALSE | BC_RET_INT:
                result.set_int(0);
                continue;
            case BC_INST_FALSE | BC_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case BC_INST_TRUE | BC_RET_STRING:
                result.set_str("1");
                continue;
            case BC_INST_TRUE | BC_RET_NULL:
            case BC_INST_TRUE | BC_RET_INT:
                result.set_int(1);
                continue;
            case BC_INST_TRUE | BC_RET_FLOAT:
                result.set_float(1.0f);
                continue;

            case BC_INST_NOT | BC_RET_STRING:
                --numargs;
                result.set_str(args[numargs].get_bool() ? "0" : "1");
                continue;
            case BC_INST_NOT | BC_RET_NULL:
            case BC_INST_NOT | BC_RET_INT:
                --numargs;
                result.set_int(!args[numargs].get_bool());
                continue;
            case BC_INST_NOT | BC_RET_FLOAT:
                --numargs;
                result.set_float(float_type(!args[numargs].get_bool()));
                continue;

            case BC_INST_POP:
                numargs -= 1;
                continue;
            case BC_INST_ENTER:
                code = runcode(cs, code, args[numargs++]);
                continue;
            case BC_INST_ENTER_RESULT:
                code = runcode(cs, code, result);
                continue;
            case BC_INST_EXIT | BC_RET_STRING:
            case BC_INST_EXIT | BC_RET_INT:
            case BC_INST_EXIT | BC_RET_FLOAT:
                force_arg(result, op & BC_INST_RET_MASK);
            /* fallthrough */
            case BC_INST_EXIT | BC_RET_NULL:
                return code;
            case BC_INST_RESULT_ARG | BC_RET_STRING:
            case BC_INST_RESULT_ARG | BC_RET_INT:
            case BC_INST_RESULT_ARG | BC_RET_FLOAT:
                force_arg(result, op & BC_INST_RET_MASK);
            /* fallthrough */
            case BC_INST_RESULT_ARG | BC_RET_NULL:
                args[numargs++] = std::move(result);
                continue;
            case BC_INST_PRINT:
                cs.print_var(*static_cast<global_var *>(cs.p_state->identmap[op >> 8]));
                continue;

            case BC_INST_LOCAL: {
                int numlocals = op >> 8, offset = numargs - numlocals;
                valarray<ident_stack, MAX_ARGUMENTS> locals{cs};
                for (int i = 0; i < numlocals; ++i) {
                    push_alias(cs, args[offset + i].get_ident(), locals[i]);
                }
                call_with_cleanup([&]() {
                    code = runcode(cs, code, result);
                }, [&]() {
                    for (int i = offset; i < numargs; i++) {
                        pop_alias(args[i].get_ident());
                    }
                });
                return code;
            }

            case BC_INST_DO_ARGS | BC_RET_NULL:
            case BC_INST_DO_ARGS | BC_RET_STRING:
            case BC_INST_DO_ARGS | BC_RET_INT:
            case BC_INST_DO_ARGS | BC_RET_FLOAT:
                call_with_args(cs, [&]() {
                    cs.run(args[--numargs].get_code(), result);
                    force_arg(result, op & BC_INST_RET_MASK);
                });
                continue;
            /* fallthrough */
            case BC_INST_DO | BC_RET_NULL:
            case BC_INST_DO | BC_RET_STRING:
            case BC_INST_DO | BC_RET_INT:
            case BC_INST_DO | BC_RET_FLOAT:
                cs.run(args[--numargs].get_code(), result);
                force_arg(result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_JUMP: {
                uint32_t len = op >> 8;
                code += len;
                continue;
            }
            case BC_INST_JUMP_B | BC_INST_FLAG_TRUE: {
                uint32_t len = op >> 8;
                if (args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_JUMP_B | BC_INST_FLAG_FALSE: {
                uint32_t len = op >> 8;
                if (!args[--numargs].get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_JUMP_RESULT | BC_INST_FLAG_TRUE: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == value_type::CODE) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_JUMP_RESULT | BC_INST_FLAG_FALSE: {
                uint32_t len = op >> 8;
                --numargs;
                if (args[numargs].get_type() == value_type::CODE) {
                    cs.run(args[numargs].get_code(), result);
                } else {
                    result = std::move(args[numargs]);
                }
                if (!result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_BREAK | BC_INST_FLAG_FALSE:
                if (cs.is_in_loop()) {
                    throw break_exception();
                } else {
                    throw error(cs, "no loop to break");
                }
                break;
            case BC_INST_BREAK | BC_INST_FLAG_TRUE:
                if (cs.is_in_loop()) {
                    throw continue_exception();
                } else {
                    throw error(cs, "no loop to continue");
                }
                break;

            case BC_INST_VAL | BC_RET_STRING: {
                uint32_t len = op >> 8;
                args[numargs++].set_str(std::string_view{
                    reinterpret_cast<char const *>(code), len
                });
                code += len / sizeof(uint32_t) + 1;
                continue;
            }
            case BC_INST_VAL_INT | BC_RET_STRING: {
                char s[4] = {
                    char((op >> 8) & 0xFF),
                    char((op >> 16) & 0xFF),
                    char((op >> 24) & 0xFF), '\0'
                };
                /* gotta cast or r.size() == potentially 3 */
                args[numargs++].set_str(static_cast<char const *>(s));
                continue;
            }
            case BC_INST_VAL | BC_RET_NULL:
            case BC_INST_VAL_INT | BC_RET_NULL:
                args[numargs++].set_none();
                continue;
            case BC_INST_VAL | BC_RET_INT:
                args[numargs++].set_int(
                    *reinterpret_cast<integer_type const *>(code)
                );
                code += bc_store_size<integer_type>;
                continue;
            case BC_INST_VAL_INT | BC_RET_INT:
                args[numargs++].set_int(integer_type(op) >> 8);
                continue;
            case BC_INST_VAL | BC_RET_FLOAT:
                args[numargs++].set_float(
                    *reinterpret_cast<float_type const *>(code)
                );
                code += bc_store_size<float_type>;
                continue;
            case BC_INST_VAL_INT | BC_RET_FLOAT:
                args[numargs++].set_float(float_type(integer_type(op) >> 8));
                continue;

            case BC_INST_DUP | BC_RET_NULL:
                args[numargs - 1].get_val(args[numargs]);
                numargs++;
                continue;
            case BC_INST_DUP | BC_RET_INT:
                args[numargs].set_int(args[numargs - 1].get_int());
                numargs++;
                continue;
            case BC_INST_DUP | BC_RET_FLOAT:
                args[numargs].set_float(args[numargs - 1].get_float());
                numargs++;
                continue;
            case BC_INST_DUP | BC_RET_STRING:
                args[numargs] = args[numargs - 1];
                args[numargs].force_str();
                numargs++;
                continue;

            case BC_INST_FORCE | BC_RET_STRING:
                args[numargs - 1].force_str();
                continue;
            case BC_INST_FORCE | BC_RET_INT:
                args[numargs - 1].force_int();
                continue;
            case BC_INST_FORCE | BC_RET_FLOAT:
                args[numargs - 1].force_float();
                continue;

            case BC_INST_RESULT | BC_RET_NULL:
                result = std::move(args[--numargs]);
                continue;
            case BC_INST_RESULT | BC_RET_STRING:
            case BC_INST_RESULT | BC_RET_INT:
            case BC_INST_RESULT | BC_RET_FLOAT:
                result = std::move(args[--numargs]);
                force_arg(result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_EMPTY | BC_RET_NULL:
                args[numargs++].set_code(
                    bcode_get_empty(state_get_internal(cs)->empty, VAL_NULL)
                );
                break;
            case BC_INST_EMPTY | BC_RET_STRING:
                args[numargs++].set_code(
                    bcode_get_empty(state_get_internal(cs)->empty, VAL_STRING)
                );
                break;
            case BC_INST_EMPTY | BC_RET_INT:
                args[numargs++].set_code(
                    bcode_get_empty(state_get_internal(cs)->empty, VAL_INT)
                );
                break;
            case BC_INST_EMPTY | BC_RET_FLOAT:
                args[numargs++].set_code(
                    bcode_get_empty(state_get_internal(cs)->empty, VAL_FLOAT)
                );
                break;
            case BC_INST_BLOCK: {
                uint32_t len = op >> 8;
                args[numargs++].set_code(
                    reinterpret_cast<bcode *>(code + 1)
                );
                code += len;
                continue;
            }
            case BC_INST_COMPILE: {
                any_value &arg = args[numargs - 1];
                codegen_state gs(cs);
                switch (arg.get_type()) {
                    case value_type::INT:
                        gs.code.reserve(8);
                        gs.code.push_back(BC_INST_START);
                        gs.gen_int(arg.get_int());
                        gs.code.push_back(BC_INST_RESULT);
                        gs.code.push_back(BC_INST_EXIT);
                        break;
                    case value_type::FLOAT:
                        gs.code.reserve(8);
                        gs.code.push_back(BC_INST_START);
                        gs.gen_float(arg.get_float());
                        gs.code.push_back(BC_INST_RESULT);
                        gs.code.push_back(BC_INST_EXIT);
                        break;
                    case value_type::STRING:
                        gs.code.reserve(64);
                        gs.gen_main(arg.get_str());
                        break;
                    default:
                        gs.code.reserve(8);
                        gs.code.push_back(BC_INST_START);
                        gs.gen_null();
                        gs.code.push_back(BC_INST_RESULT);
                        gs.code.push_back(BC_INST_EXIT);
                        break;
                }
                gs.done();
                uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
                memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
                arg.set_code(
                    reinterpret_cast<bcode *>(cbuf + 1)
                );
                continue;
            }
            case BC_INST_COND: {
                any_value &arg = args[numargs - 1];
                switch (arg.get_type()) {
                    case value_type::STRING: {
                        std::string_view s = arg.get_str();
                        if (!s.empty()) {
                            codegen_state gs(cs);
                            gs.code.reserve(64);
                            gs.gen_main(s);
                            gs.done();
                            uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
                            memcpy(
                                cbuf, gs.code.data(),
                                gs.code.size() * sizeof(uint32_t)
                            );
                            arg.set_code(reinterpret_cast<bcode *>(cbuf + 1));
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

            case BC_INST_IDENT:
                args[numargs++].set_ident(cs.p_state->identmap[op >> 8]);
                continue;
            case BC_INST_IDENT_ARG: {
                alias *a = static_cast<alias *>(
                    cs.p_state->identmap[op >> 8]
                );
                if (!ident_is_used_arg(a, cs)) {
                    any_value nv{cs};
                    static_cast<alias_impl *>(a)->push_arg(
                        nv, cs.p_callstack->argstack[a->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << a->get_index();
                }
                args[numargs++].set_ident(a);
                continue;
            }
            case BC_INST_IDENT_U: {
                any_value &arg = args[numargs - 1];
                ident *id = cs.p_state->identmap[ID_IDX_DUMMY];
                if (arg.get_type() == value_type::STRING) {
                    id = cs.new_ident(arg.get_str());
                }
                if ((id->get_index() < MAX_ARGUMENTS) && !ident_is_used_arg(id, cs)) {
                    any_value nv{cs};
                    static_cast<alias_impl *>(id)->push_arg(
                        nv, cs.p_callstack->argstack[id->get_index()], false
                    );
                    cs.p_callstack->usedargs |= 1 << id->get_index();
                }
                arg.set_ident(id);
                continue;
            }

            case BC_INST_LOOKUP_U | BC_RET_STRING: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg = static_cast<alias *>(id)->get_value();
                        arg.force_str();
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<string_var *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<integer_var *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<float_var *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_UNKNOWN:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP | BC_RET_STRING:
                args[numargs] = get_lookup_id(cs, op)->get_value();
                args[numargs++].force_str();
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_STRING: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs] = a->get_value();
                    args[numargs++].force_str();
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_INT: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_int(
                            static_cast<alias *>(id)->get_value().get_int()
                        );
                        continue;
                    case ID_SVAR:
                        arg.set_int(parse_int(
                            static_cast<string_var *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<integer_var *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_int(
                            integer_type(static_cast<float_var *>(id)->get_value())
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_int(0);
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP | BC_RET_INT:
                args[numargs++].set_int(
                    get_lookup_id(cs, op)->get_value().get_int()
                );
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_INT: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_int(0);
                } else {
                    args[numargs++].set_int(a->get_value().get_int());
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_FLOAT: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg.set_float(
                            static_cast<alias *>(id)->get_value().get_float()
                        );
                        continue;
                    case ID_SVAR:
                        arg.set_float(parse_float(
                            static_cast<string_var *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_float(float_type(
                            static_cast<integer_var *>(id)->get_value()
                        ));
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<float_var *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_float(float_type(0));
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP | BC_RET_FLOAT:
                args[numargs++].set_float(
                    get_lookup_id(cs, op)->get_value().get_float()
                );
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_FLOAT: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_float(float_type(0));
                } else {
                    args[numargs++].set_float(a->get_value().get_float());
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_NULL: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<alias *>(id)->get_value().get_val(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<string_var *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<integer_var *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<float_var *>(id)->get_value()
                        );
                        continue;
                    case ID_UNKNOWN:
                        arg.set_none();
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP | BC_RET_NULL:
                get_lookup_id(cs, op)->get_value().get_val(args[numargs++]);
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_NULL: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_none();
                } else {
                    a->get_value().get_val(args[numargs++]);
                }
                continue;
            }

            case BC_INST_LOOKUP_MU | BC_RET_STRING: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        arg = static_cast<alias *>(id)->get_value();
                        arg.force_str();
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<string_var *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<integer_var *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<float_var *>(id)->get_value());
                        arg.force_str();
                        continue;
                    case ID_UNKNOWN:
                        arg.set_str("");
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP_M | BC_RET_STRING:
                args[numargs] = get_lookup_id(cs, op)->get_value();
                args[numargs++].force_str();
                continue;
            case BC_INST_LOOKUP_MARG | BC_RET_STRING: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_str("");
                } else {
                    args[numargs] = a->get_value();
                    args[numargs++].force_str();
                }
                continue;
            }
            case BC_INST_LOOKUP_MU | BC_RET_NULL: {
                ident *id = nullptr;
                any_value &arg = args[numargs - 1];
                switch (get_lookupu_type(cs, arg, id, op)) {
                    case ID_ALIAS:
                        static_cast<alias *>(id)->get_cval(arg);
                        continue;
                    case ID_SVAR:
                        arg.set_str(static_cast<string_var *>(id)->get_value());
                        continue;
                    case ID_IVAR:
                        arg.set_int(static_cast<integer_var *>(id)->get_value());
                        continue;
                    case ID_FVAR:
                        arg.set_float(static_cast<float_var *>(id)->get_value());
                        continue;
                    case ID_UNKNOWN:
                        arg.set_none();
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP_M | BC_RET_NULL:
                get_lookup_id(cs, op)->get_cval(args[numargs++]);
                continue;
            case BC_INST_LOOKUP_MARG | BC_RET_NULL: {
                alias *a = get_lookuparg_id(cs, op);
                if (!a) {
                    args[numargs++].set_none();
                } else {
                    a->get_cval(args[numargs++]);
                }
                continue;
            }

            case BC_INST_SVAR | BC_RET_STRING:
            case BC_INST_SVAR | BC_RET_NULL:
                args[numargs++].set_str(static_cast<string_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_SVAR | BC_RET_INT:
                args[numargs++].set_int(parse_int(static_cast<string_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case BC_INST_SVAR | BC_RET_FLOAT:
                args[numargs++].set_float(parse_float(static_cast<string_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case BC_INST_SVAR1:
                cs.set_var_str_checked(
                    static_cast<string_var *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_str()
                );
                continue;

            case BC_INST_IVAR | BC_RET_INT:
            case BC_INST_IVAR | BC_RET_NULL:
                args[numargs++].set_int(static_cast<integer_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_IVAR | BC_RET_STRING:
                args[numargs].set_int(static_cast<integer_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                args[numargs++].force_str();
                continue;
            case BC_INST_IVAR | BC_RET_FLOAT:
                args[numargs++].set_float(float_type(static_cast<integer_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case BC_INST_IVAR1:
                cs.set_var_int_checked(
                    static_cast<integer_var *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_int()
                );
                continue;
            case BC_INST_IVAR2:
                numargs -= 2;
                cs.set_var_int_checked(
                    static_cast<integer_var *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                );
                continue;
            case BC_INST_IVAR3:
                numargs -= 3;
                cs.set_var_int_checked(
                    static_cast<integer_var *>(cs.p_state->identmap[op >> 8]),
                    (args[numargs].get_int() << 16)
                        | (args[numargs + 1].get_int() << 8)
                        | (args[numargs + 2].get_int()));
                continue;

            case BC_INST_FVAR | BC_RET_FLOAT:
            case BC_INST_FVAR | BC_RET_NULL:
                args[numargs++].set_float(static_cast<float_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_FVAR | BC_RET_STRING:
                args[numargs].set_int(static_cast<float_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value());
                args[numargs++].force_str();
                continue;
            case BC_INST_FVAR | BC_RET_INT:
                args[numargs++].set_int(int(static_cast<float_var *>(
                    cs.p_state->identmap[op >> 8]
                )->get_value()));
                continue;
            case BC_INST_FVAR1:
                cs.set_var_float_checked(
                    static_cast<float_var *>(cs.p_state->identmap[op >> 8]),
                    args[--numargs].get_float()
                );
                continue;

            case BC_INST_COM | BC_RET_NULL:
            case BC_INST_COM | BC_RET_STRING:
            case BC_INST_COM | BC_RET_FLOAT:
            case BC_INST_COM | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    cs.p_state->identmap[op >> 8]
                );
                int offset = numargs - id->get_num_args();
                result.force_none();
                id->call(cs, std::span<any_value>{
                    &args[0] + offset, std::size_t(id->get_num_args())
                }, result);
                force_arg(result, op & BC_INST_RET_MASK);
                numargs = offset;
                continue;
            }

            case BC_INST_COM_V | BC_RET_NULL:
            case BC_INST_COM_V | BC_RET_STRING:
            case BC_INST_COM_V | BC_RET_FLOAT:
            case BC_INST_COM_V | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    cs.p_state->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F,
                            offset = numargs - callargs;
                result.force_none();
                id->call(cs, std::span{&args[offset], callargs}, result);
                force_arg(result, op & BC_INST_RET_MASK);
                numargs = offset;
                continue;
            }
            case BC_INST_COM_C | BC_RET_NULL:
            case BC_INST_COM_C | BC_RET_STRING:
            case BC_INST_COM_C | BC_RET_FLOAT:
            case BC_INST_COM_C | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    cs.p_state->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F,
                            offset = numargs - callargs;
                result.force_none();
                {
                    any_value tv{cs};
                    tv.set_str(concat_values(cs, std::span{
                        &args[offset], callargs
                    }, " "));
                    id->call(cs, std::span<any_value>{&tv, 1}, result);
                }
                force_arg(result, op & BC_INST_RET_MASK);
                numargs = offset;
                continue;
            }

            case BC_INST_CONC | BC_RET_NULL:
            case BC_INST_CONC | BC_RET_STRING:
            case BC_INST_CONC | BC_RET_FLOAT:
            case BC_INST_CONC | BC_RET_INT:
            case BC_INST_CONC_W | BC_RET_NULL:
            case BC_INST_CONC_W | BC_RET_STRING:
            case BC_INST_CONC_W | BC_RET_FLOAT:
            case BC_INST_CONC_W | BC_RET_INT: {
                std::size_t numconc = op >> 8;
                auto buf = concat_values(
                    cs, std::span{&args[numargs - numconc], numconc},
                    ((op & BC_INST_OP_MASK) == BC_INST_CONC) ? " " : ""
                );
                numargs = numargs - numconc;
                args[numargs].set_str(buf);
                force_arg(args[numargs], op & BC_INST_RET_MASK);
                numargs++;
                continue;
            }

            case BC_INST_CONC_M | BC_RET_NULL:
            case BC_INST_CONC_M | BC_RET_STRING:
            case BC_INST_CONC_M | BC_RET_FLOAT:
            case BC_INST_CONC_M | BC_RET_INT: {
                std::size_t numconc = op >> 8;
                auto buf = concat_values(
                    cs, std::span{&args[numargs - numconc], numconc}
                );
                numargs = numargs - numconc;
                result.set_str(buf);
                force_arg(result, op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_ALIAS:
                static_cast<alias_impl *>(
                    cs.p_state->identmap[op >> 8]
                )->set_alias(cs, args[--numargs]);
                continue;
            case BC_INST_ALIAS_ARG:
                static_cast<alias_impl *>(
                    cs.p_state->identmap[op >> 8]
                )->set_arg(cs, args[--numargs]);
                continue;
            case BC_INST_ALIAS_U:
                numargs -= 2;
                cs.set_alias(
                    args[numargs].get_str(), std::move(args[numargs + 1])
                );
                continue;

            case BC_INST_CALL | BC_RET_NULL:
            case BC_INST_CALL | BC_RET_STRING:
            case BC_INST_CALL | BC_RET_FLOAT:
            case BC_INST_CALL | BC_RET_INT: {
                result.force_none();
                ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
                    force_arg(result, op & BC_INST_RET_MASK);
                    throw error(
                        cs, "unknown command: %s", id->get_name().data()
                    );
                }
                call_alias(
                    cs, static_cast<alias *>(id), &args[0], result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }
            case BC_INST_CALL_ARG | BC_RET_NULL:
            case BC_INST_CALL_ARG | BC_RET_STRING:
            case BC_INST_CALL_ARG | BC_RET_FLOAT:
            case BC_INST_CALL_ARG | BC_RET_INT: {
                result.force_none();
                ident *id = cs.p_state->identmap[op >> 13];
                int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
                if (!ident_is_used_arg(id, cs)) {
                    numargs = offset;
                    force_arg(result, op & BC_INST_RET_MASK);
                    continue;
                }
                call_alias(
                    cs, static_cast<alias *>(id), &args[0], result, callargs,
                    numargs, offset, 0, op
                );
                continue;
            }

            case BC_INST_CALL_U | BC_RET_NULL:
            case BC_INST_CALL_U | BC_RET_STRING:
            case BC_INST_CALL_U | BC_RET_FLOAT:
            case BC_INST_CALL_U | BC_RET_INT: {
                int callargs = op >> 8, offset = numargs - callargs;
                any_value &idarg = args[offset - 1];
                if (idarg.get_type() != value_type::STRING) {
litval:
                    result = std::move(idarg);
                    force_arg(result, op & BC_INST_RET_MASK);
                    numargs = offset - 1;
                    continue;
                }
                auto idn = idarg.get_str();
                ident *id = cs.get_ident(idn);
                if (!id) {
noid:
                    if (!is_valid_name(idn)) {
                        goto litval;
                    }
                    result.force_none();
                    force_arg(result, op & BC_INST_RET_MASK);
                    std::string_view ids{idn};
                    throw error(
                        cs, "unknown command: %s", ids.data()
                    );
                }
                result.force_none();
                switch (id->get_raw_type()) {
                    default:
                        if (!ident_has_cb(id)) {
                            numargs = offset - 1;
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND:
                        callcommand(
                            cs, static_cast<command_impl *>(id),
                            &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        numargs = offset - 1;
                        continue;
                    case ID_LOCAL: {
                        valarray<ident_stack, MAX_ARGUMENTS> locals{cs};
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            push_alias(cs, cs.force_ident(
                                args[offset + j]
                            ), locals[j]);
                        }
                        call_with_cleanup([&]() {
                            code = runcode(cs, code, result);
                        }, [&]() {
                            for (size_t j = 0; j < size_t(callargs); ++j) {
                                pop_alias(args[offset + j].get_ident());
                            }
                        });
                        return code;
                    }
                    case ID_IVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<global_var *>(id));
                        } else {
                            cs.set_var_int_checked(
                                static_cast<integer_var *>(id),
                                std::span{&args[offset], std::size_t(callargs)}
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & BC_INST_RET_MASK);
                        continue;
                    case ID_FVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<global_var *>(id));
                        } else {
                            cs.set_var_float_checked(
                                static_cast<float_var *>(id),
                                args[offset].force_float()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & BC_INST_RET_MASK);
                        continue;
                    case ID_SVAR:
                        if (callargs <= 0) {
                            cs.print_var(*static_cast<global_var *>(id));
                        } else {
                            cs.set_var_str_checked(
                                static_cast<string_var *>(id),
                                args[offset].force_str()
                            );
                        }
                        numargs = offset - 1;
                        force_arg(result, op & BC_INST_RET_MASK);
                        continue;
                    case ID_ALIAS: {
                        alias *a = static_cast<alias *>(id);
                        if (
                            (a->get_index() < MAX_ARGUMENTS) &&
                            !ident_is_used_arg(a, cs)
                        ) {
                            numargs = offset - 1;
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                        if (a->get_value().get_type() == value_type::NONE) {
                            goto noid;
                        }
                        call_alias(
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

void state::run(bcode *code, any_value &ret) {
    runcode(*this, reinterpret_cast<uint32_t *>(code), ret);
}

static void do_run(
    state &cs, std::string_view file, std::string_view code,
    any_value &ret
) {
    codegen_state gs(cs);
    gs.src_name = file;
    gs.code.reserve(64);
    gs.gen_main(code, VAL_ANY);
    gs.done();
    uint32_t *cbuf = bcode_alloc(gs.cs, gs.code.size());
    memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(uint32_t));
    bcode_incr(cbuf);
    runcode(cs, cbuf + 1, ret);
    bcode_decr(cbuf);
}

void state::run(std::string_view code, any_value &ret) {
    do_run(*this, std::string_view{}, code, ret);
}

void state::run(
    std::string_view code, any_value &ret, std::string_view source
) {
    do_run(*this, source, code, ret);
}

void state::run(ident *id, std::span<any_value> args, any_value &ret) {
    int nargs = int(args.size());
    ret.set_none();
    RunDepthRef level{*this}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!ident_has_cb(id)) {
                    break;
                }
            /* fallthrough */
            case ident_type::COMMAND:
                if (nargs < static_cast<command_impl *>(id)->get_num_args()) {
                    valarray<any_value, MAX_ARGUMENTS> buf{*this};
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        buf[i] = args[i];
                    }
                    callcommand(
                        *this, static_cast<command_impl *>(id), &buf[0], ret,
                        nargs, false
                    );
                } else {
                    callcommand(
                        *this, static_cast<command_impl *>(id), &args[0],
                        ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            case ident_type::IVAR:
                if (args.empty()) {
                    print_var(*static_cast<global_var *>(id));
                } else {
                    set_var_int_checked(static_cast<integer_var *>(id), args);
                }
                break;
            case ident_type::FVAR:
                if (args.empty()) {
                    print_var(*static_cast<global_var *>(id));
                } else {
                    set_var_float_checked(
                        static_cast<float_var *>(id), args[0].force_float()
                    );
                }
                break;
            case ident_type::SVAR:
                if (args.empty()) {
                    print_var(*static_cast<global_var *>(id));
                } else {
                    set_var_str_checked(
                        static_cast<string_var *>(id), args[0].force_str()
                    );
                }
                break;
            case ident_type::ALIAS: {
                alias *a = static_cast<alias *>(id);
                if (
                    (a->get_index() < MAX_ARGUMENTS) && !ident_is_used_arg(a, *this)
                ) {
                    break;
                }
                if (a->get_value().get_type() == value_type::NONE) {
                    break;
                }
                call_alias(
                    *this, a, &args[0], ret, nargs, nargs, 0, 0, BC_RET_NULL
                );
                break;
            }
        }
    }
}

any_value state::run(bcode *code) {
    any_value ret{*this};
    run(code, ret);
    return ret;
}

any_value state::run(std::string_view code) {
    any_value ret{*this};
    run(code, ret);
    return ret;
}

any_value state::run(std::string_view code, std::string_view source) {
    any_value ret{*this};
    run(code, ret, source);
    return ret;
}

any_value state::run(ident *id, std::span<any_value> args) {
    any_value ret{*this};
    run(id, args, ret);
    return ret;
}

loop_state state::run_loop(bcode *code, any_value &ret) {
    ++p_inloop;
    try {
        run(code, ret);
    } catch (break_exception) {
        --p_inloop;
        return loop_state::BREAK;
    } catch (continue_exception) {
        --p_inloop;
        return loop_state::CONTINUE;
    } catch (...) {
        --p_inloop;
        throw;
    }
    return loop_state::NORMAL;
}

loop_state state::run_loop(bcode *code) {
    any_value ret{*this};
    return run_loop(code, ret);
}

} /* namespace cubescript */
