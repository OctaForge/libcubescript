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

stack_state::stack_state(thread_state &ts, node *nd, bool gap):
    p_state{ts}, p_node{nd}, p_gap{gap}
{}
stack_state::stack_state(stack_state &&st):
    p_state{st.p_state}, p_node{st.p_node}, p_gap{st.p_gap}
{
    st.p_node = nullptr;
    st.p_gap = false;
}

stack_state::~stack_state() {
    size_t len = 0;
    for (node const *nd = p_node; nd; nd = nd->next) {
        ++len;
    }
    p_state.istate->destroy_array(p_node, len);
}

stack_state &stack_state::operator=(stack_state &&st) {
    p_node = st.p_node;
    p_gap = st.p_gap;
    st.p_node = nullptr;
    st.p_gap = false;
    return *this;
}

stack_state::node const *stack_state::get() const {
    return p_node;
}

bool stack_state::gap() const {
    return p_gap;
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
    thread_state &ts, command_impl *id, any_value *args, any_value &res,
    std::size_t nargs, bool lookup = false
) {
    int i = -1, fakeargs = 0, numargs = int(nargs);
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
                } else if (args[i].get_type() == value_type::STRING) {
                    auto str = std::string_view{args[i].get_str()};
                    if (str.empty()) {
                        args[i].set_int(0);
                    } else {
                        args[i].force_code(*ts.pstate);
                    }
                }
                break;
            case 'e':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_code(
                        bcode_get_empty(ts.istate->empty, VAL_NULL)
                    );
                    fakeargs++;
                } else {
                    args[i].force_code(*ts.pstate);
                }
                break;
            case 'r':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_ident(ts.istate->identmap[ID_IDX_DUMMY]);
                    fakeargs++;
                } else {
                    args[i].force_ident(*ts.pstate);
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
                any_value tv{*ts.pstate};
                tv.set_str(concat_values(
                    *ts.pstate, std::span{args, std::size_t(i)}, " "
                ));
                static_cast<command_impl *>(id)->call(
                    *ts.pstate, std::span<any_value>(&tv, &tv + 1), res
                );
                return;
            }
            case 'V':
                i = std::max(i + 1, numargs);
                static_cast<command_impl *>(id)->call(
                    *ts.pstate, std::span{args, std::size_t(i)}, res
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
        *ts.pstate, std::span<any_value>{args, std::size_t(i)}, res
    );
}

static std::uint32_t *runcode(
    thread_state &ts, std::uint32_t *code, any_value &result
);

static inline void call_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs,
    std::size_t offset, std::size_t skip, std::uint32_t op
) {
    integer_var *anargs = static_cast<integer_var *>(
        ts.istate->identmap[ID_IDX_NUMARGS]
    );
    valarray<ident_stack, MAX_ARGUMENTS> argstack{*ts.pstate};
    for(std::size_t i = 0; i < callargs; i++) {
        static_cast<alias_impl *>(ts.istate->identmap[i])->push_arg(
            args[offset + i], argstack[i], false
        );
    }
    auto oldargs = anargs->get_value();
    anargs->set_value(callargs);
    int oldflags = ts.pstate->identflags;
    ts.pstate->identflags |= a->get_flags()&IDENT_FLAG_OVERRIDDEN;
    ident_link aliaslink = {
        a, ts.callstack, (1<<callargs)-1, &argstack[0]
    };
    ts.callstack = &aliaslink;
    std::uint32_t *codep = static_cast<
        alias_impl *
    >(a)->compile_code(ts)->get_raw();
    bcode_incr(codep);
    call_with_cleanup([&]() {
        runcode(ts, codep+1, result);
    }, [&]() {
        bcode_decr(codep);
        ts.callstack = aliaslink.next;
        ts.pstate->identflags = oldflags;
        for (std::size_t i = 0; i < callargs; i++) {
            static_cast<alias_impl *>(ts.istate->identmap[i])->pop_arg();
        }
        int argmask = aliaslink.usedargs & int(~0U << callargs);
        for (; argmask; ++callargs) {
            if (argmask & (1 << callargs)) {
                static_cast<alias_impl *>(
                    ts.istate->identmap[callargs]
                )->pop_arg();
                argmask &= ~(1 << callargs);
            }
        }
        force_arg(result, op & BC_INST_RET_MASK);
        anargs->set_value(integer_type(oldargs));
        nargs = offset - skip;
    });
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

struct run_depth_guard {
    run_depth_guard() = delete;
    run_depth_guard(thread_state &ts) {
        if (rundepth >= MaxRunDepth) {
            throw error{ts, "exceeded recursion limit"};
        }
        ++rundepth;
    }
    run_depth_guard(run_depth_guard const &) = delete;
    run_depth_guard(run_depth_guard &&) = delete;
    ~run_depth_guard() { --rundepth; }
};

static inline alias *get_lookup_id(thread_state &ts, std::uint32_t op) {
    ident *id = ts.istate->identmap[op >> 8];
    if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
        throw error{
            *ts.pstate, "unknown alias lookup: %s", id->get_name().data()
        };
    }
    return static_cast<alias *>(id);
}

static inline alias *get_lookuparg_id(thread_state &ts, std::uint32_t op) {
    ident *id = ts.istate->identmap[op >> 8];
    if (!ident_is_used_arg(id, ts)) {
        return nullptr;
    }
    return static_cast<alias *>(id);
}

struct stack_guard {
    thread_state *tsp;
    std::size_t oldtop;

    stack_guard() = delete;
    stack_guard(thread_state &ts):
        tsp{&ts}, oldtop{ts.vmstack.size()}
    {}

    ~stack_guard() {
        tsp->vmstack.resize(oldtop, any_value{*tsp->pstate});
    }

    stack_guard(stack_guard const &) = delete;
    stack_guard(stack_guard &&) = delete;
};

static inline int get_lookupu_type(
    thread_state &ts, any_value &arg, ident *&id, std::uint32_t op
) {
    if (arg.get_type() != value_type::STRING) {
        return -2; /* default case */
    }
    id = ts.pstate->get_ident(arg.get_str());
    if (id) {
        switch(id->get_type()) {
            case ident_type::ALIAS:
                if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
                    break;
                }
                if (
                    (id->get_index() < MAX_ARGUMENTS) &&
                    !ident_is_used_arg(id, ts)
                ) {
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
                /* make sure value stack gets restored */
                stack_guard s{ts};
                auto *cimpl = static_cast<command_impl *>(id);
                auto &args = ts.vmstack;
                auto osz = args.size();
                /* pad with as many empty values as we need */
                args.resize(osz + cimpl->get_num_args(), any_value{*ts.pstate});
                arg.set_none();
                callcommand(ts, cimpl, &args[osz], arg, 0, true);
                force_arg(arg, op & BC_INST_RET_MASK);
                return -2; /* ignore */
            }
            default:
                return ID_UNKNOWN;
        }
    }
    throw error{*ts.pstate, "unknown alias lookup: %s", arg.get_str().data()};
}

static std::uint32_t *runcode(
    thread_state &ts, std::uint32_t *code, any_value &result
) {
    result.set_none();
    auto &cs = *ts.pstate;
    run_depth_guard level{ts}; /* incr and decr on scope exit */
    stack_guard guard{ts}; /* resize back to original */
    auto &args = ts.vmstack;
    auto &chook = cs.get_call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        std::uint32_t op = *code++;
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
                result.set_str(args.back().get_bool() ? "0" : "1");
                args.pop_back();
                continue;
            case BC_INST_NOT | BC_RET_NULL:
            case BC_INST_NOT | BC_RET_INT:
                result.set_int(!args.back().get_bool());
                args.pop_back();
                continue;
            case BC_INST_NOT | BC_RET_FLOAT:
                result.set_float(float_type(!args.back().get_bool()));
                args.pop_back();
                continue;

            case BC_INST_POP:
                args.pop_back();
                continue;
            case BC_INST_ENTER:
                code = runcode(ts, code, args.emplace_back(cs));
                continue;
            case BC_INST_ENTER_RESULT:
                code = runcode(ts, code, result);
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
                args.emplace_back(std::move(result));
                continue;
            case BC_INST_PRINT:
                cs.print_var(*static_cast<global_var *>(ts.istate->identmap[op >> 8]));
                continue;

            case BC_INST_LOCAL: {
                std::size_t numlocals = op >> 8;
                std::size_t offset = args.size() - numlocals;
                valarray<ident_stack, MAX_ARGUMENTS> locals{cs};
                for (std::size_t i = 0; i < numlocals; ++i) {
                    push_alias(cs, args[offset + i].get_ident(), locals[i]);
                }
                call_with_cleanup([&]() {
                    code = runcode(ts, code, result);
                }, [&]() {
                    for (std::size_t i = offset; i < args.size(); ++i) {
                        pop_alias(args[i].get_ident());
                    }
                });
                return code;
            }

            case BC_INST_DO_ARGS | BC_RET_NULL:
            case BC_INST_DO_ARGS | BC_RET_STRING:
            case BC_INST_DO_ARGS | BC_RET_INT:
            case BC_INST_DO_ARGS | BC_RET_FLOAT:
                call_with_args(ts, [&]() {
                    auto v = std::move(args.back());
                    args.pop_back();
                    cs.run(v.get_code(), result);
                    force_arg(result, op & BC_INST_RET_MASK);
                });
                continue;
            /* fallthrough */
            case BC_INST_DO | BC_RET_NULL:
            case BC_INST_DO | BC_RET_STRING:
            case BC_INST_DO | BC_RET_INT:
            case BC_INST_DO | BC_RET_FLOAT: {
                auto v = std::move(args.back());
                args.pop_back();
                cs.run(v.get_code(), result);
                force_arg(result, op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_JUMP: {
                std::uint32_t len = op >> 8;
                code += len;
                continue;
            }
            case BC_INST_JUMP_B | BC_INST_FLAG_TRUE: {
                std::uint32_t len = op >> 8;
                if (args.back().get_bool()) {
                    code += len;
                }
                args.pop_back();
                continue;
            }
            case BC_INST_JUMP_B | BC_INST_FLAG_FALSE: {
                std::uint32_t len = op >> 8;
                if (!args.back().get_bool()) {
                    code += len;
                }
                args.pop_back();
                continue;
            }
            case BC_INST_JUMP_RESULT | BC_INST_FLAG_TRUE: {
                std::uint32_t len = op >> 8;
                auto v = std::move(args.back());
                args.pop_back();
                if (v.get_type() == value_type::CODE) {
                    cs.run(v.get_code(), result);
                } else {
                    result = std::move(v);
                }
                if (result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_JUMP_RESULT | BC_INST_FLAG_FALSE: {
                std::uint32_t len = op >> 8;
                auto v = std::move(args.back());
                args.pop_back();
                if (v.get_type() == value_type::CODE) {
                    cs.run(v.get_code(), result);
                } else {
                    result = std::move(v);
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
                    throw error{cs, "no loop to break"};
                }
                break;
            case BC_INST_BREAK | BC_INST_FLAG_TRUE:
                if (cs.is_in_loop()) {
                    throw continue_exception();
                } else {
                    throw error{cs, "no loop to continue"};
                }
                break;

            case BC_INST_VAL | BC_RET_STRING: {
                std::uint32_t len = op >> 8;
                args.emplace_back(cs).set_str(std::string_view{
                    reinterpret_cast<char const *>(code), len
                });
                code += len / sizeof(std::uint32_t) + 1;
                continue;
            }
            case BC_INST_VAL_INT | BC_RET_STRING: {
                char s[4] = {
                    char((op >> 8) & 0xFF),
                    char((op >> 16) & 0xFF),
                    char((op >> 24) & 0xFF), '\0'
                };
                /* gotta cast or r.size() == potentially 3 */
                args.emplace_back(cs).set_str(static_cast<char const *>(s));
                continue;
            }
            case BC_INST_VAL | BC_RET_NULL:
            case BC_INST_VAL_INT | BC_RET_NULL:
                args.emplace_back(cs).set_none();
                continue;
            case BC_INST_VAL | BC_RET_INT:
                args.emplace_back(cs).set_int(
                    *reinterpret_cast<integer_type const *>(code)
                );
                code += bc_store_size<integer_type>;
                continue;
            case BC_INST_VAL_INT | BC_RET_INT:
                args.emplace_back(cs).set_int(integer_type(op) >> 8);
                continue;
            case BC_INST_VAL | BC_RET_FLOAT:
                args.emplace_back(cs).set_float(
                    *reinterpret_cast<float_type const *>(code)
                );
                code += bc_store_size<float_type>;
                continue;
            case BC_INST_VAL_INT | BC_RET_FLOAT:
                args.emplace_back(cs).set_float(
                    float_type(integer_type(op) >> 8)
                );
                continue;

            case BC_INST_DUP | BC_RET_NULL: {
                auto &v = args.back();
                v.get_val(args.emplace_back(cs));
                continue;
            }
            case BC_INST_DUP | BC_RET_INT: {
                auto &v = args.back();
                args.emplace_back(cs).set_int(v.get_int());
                continue;
            }
            case BC_INST_DUP | BC_RET_FLOAT: {
                auto &v = args.back();
                args.emplace_back(cs).set_float(v.get_float());
                continue;
            }
            case BC_INST_DUP | BC_RET_STRING: {
                auto &v = args.back();
                auto &nv = args.emplace_back(cs);
                nv = v;
                nv.force_str();
                continue;
            }

            case BC_INST_FORCE | BC_RET_STRING:
                args.back().force_str();
                continue;
            case BC_INST_FORCE | BC_RET_INT:
                args.back().force_int();
                continue;
            case BC_INST_FORCE | BC_RET_FLOAT:
                args.back().force_float();
                continue;

            case BC_INST_RESULT | BC_RET_NULL:
                result = std::move(args.back());
                args.pop_back();
                continue;
            case BC_INST_RESULT | BC_RET_STRING:
            case BC_INST_RESULT | BC_RET_INT:
            case BC_INST_RESULT | BC_RET_FLOAT:
                result = std::move(args.back());
                args.pop_back();
                force_arg(result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_EMPTY | BC_RET_NULL:
                args.emplace_back(cs).set_code(
                    bcode_get_empty(ts.istate->empty, VAL_NULL)
                );
                break;
            case BC_INST_EMPTY | BC_RET_STRING:
                args.emplace_back(cs).set_code(
                    bcode_get_empty(ts.istate->empty, VAL_STRING)
                );
                break;
            case BC_INST_EMPTY | BC_RET_INT:
                args.emplace_back(cs).set_code(
                    bcode_get_empty(ts.istate->empty, VAL_INT)
                );
                break;
            case BC_INST_EMPTY | BC_RET_FLOAT:
                args.emplace_back(cs).set_code(
                    bcode_get_empty(ts.istate->empty, VAL_FLOAT)
                );
                break;
            case BC_INST_BLOCK: {
                std::uint32_t len = op >> 8;
                args.emplace_back(cs).set_code(
                    reinterpret_cast<bcode *>(code + 1)
                );
                code += len;
                continue;
            }
            case BC_INST_COMPILE: {
                any_value &arg = args.back();
                codegen_state gs{ts};
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
                std::uint32_t *cbuf = bcode_alloc(ts.istate, gs.code.size());
                std::memcpy(
                    cbuf, gs.code.data(),
                    gs.code.size() * sizeof(std::uint32_t)
                );
                arg.set_code(
                    reinterpret_cast<bcode *>(cbuf + 1)
                );
                continue;
            }
            case BC_INST_COND: {
                any_value &arg = args.back();
                switch (arg.get_type()) {
                    case value_type::STRING: {
                        std::string_view s = arg.get_str();
                        if (!s.empty()) {
                            codegen_state gs{ts};
                            gs.code.reserve(64);
                            gs.gen_main(s);
                            gs.done();
                            std::uint32_t *cbuf = bcode_alloc(
                                ts.istate, gs.code.size()
                            );
                            std::memcpy(
                                cbuf, gs.code.data(),
                                gs.code.size() * sizeof(std::uint32_t)
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
                args.emplace_back(cs).set_ident(ts.istate->identmap[op >> 8]);
                continue;
            case BC_INST_IDENT_ARG: {
                alias *a = static_cast<alias *>(
                    ts.istate->identmap[op >> 8]
                );
                if (!ident_is_used_arg(a, ts)) {
                    any_value nv{cs};
                    static_cast<alias_impl *>(a)->push_arg(
                        nv, ts.callstack->argstack[a->get_index()],
                        false
                    );
                    ts.callstack->usedargs |= 1 << a->get_index();
                }
                args.emplace_back(cs).set_ident(a);
                continue;
            }
            case BC_INST_IDENT_U: {
                any_value &arg = args.back();
                ident *id = ts.istate->identmap[ID_IDX_DUMMY];
                if (arg.get_type() == value_type::STRING) {
                    id = cs.new_ident(arg.get_str());
                }
                if (
                    (id->get_index() < MAX_ARGUMENTS) &&
                    !ident_is_used_arg(id, ts)
                ) {
                    any_value nv{cs};
                    static_cast<alias_impl *>(id)->push_arg(
                        nv, ts.callstack->argstack[id->get_index()],
                        false
                    );
                    ts.callstack->usedargs |= 1 << id->get_index();
                }
                arg.set_ident(id);
                continue;
            }

            case BC_INST_LOOKUP_U | BC_RET_STRING: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
            case BC_INST_LOOKUP | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v = get_lookup_id(ts, op)->get_value();
                v.force_str();
                continue;
            }
            case BC_INST_LOOKUP_ARG | BC_RET_STRING: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_str("");
                } else {
                    auto &v = args.emplace_back(cs);
                    v = a->get_value();
                    v.force_str();
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_INT: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
                args.emplace_back(cs).set_int(
                    get_lookup_id(ts, op)->get_value().get_int()
                );
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_INT: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_int(0);
                } else {
                    args.emplace_back(cs).set_int(a->get_value().get_int());
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_FLOAT: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
                args.emplace_back(cs).set_float(
                    get_lookup_id(ts, op)->get_value().get_float()
                );
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_FLOAT: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_float(float_type(0));
                } else {
                    args.emplace_back(cs).set_float(a->get_value().get_float());
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_NULL: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
                get_lookup_id(ts, op)->get_value().get_val(
                    args.emplace_back(cs)
                );
                continue;
            case BC_INST_LOOKUP_ARG | BC_RET_NULL: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_none();
                } else {
                    a->get_value().get_val(args.emplace_back(cs));
                }
                continue;
            }

            case BC_INST_LOOKUP_MU | BC_RET_STRING: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
            case BC_INST_LOOKUP_M | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v = get_lookup_id(ts, op)->get_value();
                v.force_str();
                continue;
            }
            case BC_INST_LOOKUP_MARG | BC_RET_STRING: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_str("");
                } else {
                    auto &v = args.emplace_back(cs);
                    v = a->get_value();
                    v.force_str();
                }
                continue;
            }
            case BC_INST_LOOKUP_MU | BC_RET_NULL: {
                ident *id = nullptr;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op)) {
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
                get_lookup_id(ts, op)->get_cval(args.emplace_back(cs));
                continue;
            case BC_INST_LOOKUP_MARG | BC_RET_NULL: {
                alias *a = get_lookuparg_id(ts, op);
                if (!a) {
                    args.emplace_back(cs).set_none();
                } else {
                    a->get_cval(args.emplace_back(cs));
                }
                continue;
            }

            case BC_INST_SVAR | BC_RET_STRING:
            case BC_INST_SVAR | BC_RET_NULL:
                args.emplace_back(cs).set_str(static_cast<string_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_SVAR | BC_RET_INT:
                args.emplace_back(cs).set_int(parse_int(
                    static_cast<string_var *>(
                        ts.istate->identmap[op >> 8]
                    )->get_value()
                 ));
                continue;
            case BC_INST_SVAR | BC_RET_FLOAT:
                args.emplace_back(cs).set_float(parse_float(
                    static_cast<string_var *>(
                        ts.istate->identmap[op >> 8]
                    )->get_value()
                ));
                continue;
            case BC_INST_SVAR1: {
                auto v = std::move(args.back());
                args.pop_back();
                cs.set_var_str_checked(
                    static_cast<string_var *>(ts.istate->identmap[op >> 8]),
                    v.get_str()
                );
                continue;
            }

            case BC_INST_IVAR | BC_RET_INT:
            case BC_INST_IVAR | BC_RET_NULL:
                args.emplace_back(cs).set_int(static_cast<integer_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_IVAR | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v.set_int(static_cast<integer_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                v.force_str();
                continue;
            }
            case BC_INST_IVAR | BC_RET_FLOAT:
                args.emplace_back(cs).set_float(float_type(
                    static_cast<integer_var *>(
                        ts.istate->identmap[op >> 8]
                    )->get_value()
                ));
                continue;
            case BC_INST_IVAR1: {
                auto v = std::move(args.back());
                args.pop_back();
                cs.set_var_int_checked(
                    static_cast<integer_var *>(ts.istate->identmap[op >> 8]),
                    v.get_int()
                );
                continue;
            }
            case BC_INST_IVAR2: {
                auto v1 = std::move(args.back()); args.pop_back();
                auto v2 = std::move(args.back()); args.pop_back();
                cs.set_var_int_checked(
                    static_cast<integer_var *>(ts.istate->identmap[op >> 8]),
                    (v2.get_int() << 16) | (v1.get_int() << 8)
                );
                continue;
            }
            case BC_INST_IVAR3: {
                auto v1 = std::move(args.back()); args.pop_back();
                auto v2 = std::move(args.back()); args.pop_back();
                auto v3 = std::move(args.back()); args.pop_back();
                cs.set_var_int_checked(
                    static_cast<integer_var *>(ts.istate->identmap[op >> 8]),
                    (v3.get_int() << 16) | (v2.get_int() << 8) | (v1.get_int())
                );
                continue;
            }

            case BC_INST_FVAR | BC_RET_FLOAT:
            case BC_INST_FVAR | BC_RET_NULL:
                args.emplace_back(cs).set_float(static_cast<float_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_FVAR | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v.set_int(static_cast<float_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                v.force_str();
                continue;
            }
            case BC_INST_FVAR | BC_RET_INT:
                args.emplace_back(cs).set_int(int(static_cast<float_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value()));
                continue;
            case BC_INST_FVAR1: {
                auto &v = args.back();
                cs.set_var_float_checked(
                    static_cast<float_var *>(ts.istate->identmap[op >> 8]),
                    v.get_float()
                );
                args.pop_back();
                continue;
            }

            case BC_INST_COM | BC_RET_NULL:
            case BC_INST_COM | BC_RET_STRING:
            case BC_INST_COM | BC_RET_FLOAT:
            case BC_INST_COM | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 8]
                );
                std::size_t offset = args.size() - id->get_num_args();
                result.force_none();
                id->call(cs, std::span<any_value>{
                    &args[offset], std::size_t(id->get_num_args())
                }, result);
                force_arg(result, op & BC_INST_RET_MASK);
                args.resize(offset, any_value{cs});
                continue;
            }

            case BC_INST_COM_V | BC_RET_NULL:
            case BC_INST_COM_V | BC_RET_STRING:
            case BC_INST_COM_V | BC_RET_FLOAT:
            case BC_INST_COM_V | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F;
                std::size_t offset = args.size() - callargs;
                result.force_none();
                id->call(cs, std::span{&args[offset], callargs}, result);
                force_arg(result, op & BC_INST_RET_MASK);
                args.resize(offset, any_value{cs});
                continue;
            }
            case BC_INST_COM_C | BC_RET_NULL:
            case BC_INST_COM_C | BC_RET_STRING:
            case BC_INST_COM_C | BC_RET_FLOAT:
            case BC_INST_COM_C | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 13]
                );
                std::size_t callargs = (op >> 8) & 0x1F,
                            offset = args.size() - callargs;
                result.force_none();
                {
                    any_value tv{cs};
                    tv.set_str(concat_values(cs, std::span{
                        &args[offset], callargs
                    }, " "));
                    id->call(cs, std::span<any_value>{&tv, 1}, result);
                }
                force_arg(result, op & BC_INST_RET_MASK);
                args.resize(offset, any_value{cs});
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
                    cs, std::span{&args[args.size() - numconc], numconc},
                    ((op & BC_INST_OP_MASK) == BC_INST_CONC) ? " " : ""
                );
                args.resize(args.size() - numconc, any_value{cs});
                force_arg(args.emplace_back(cs), op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_CONC_M | BC_RET_NULL:
            case BC_INST_CONC_M | BC_RET_STRING:
            case BC_INST_CONC_M | BC_RET_FLOAT:
            case BC_INST_CONC_M | BC_RET_INT: {
                std::size_t numconc = op >> 8;
                auto buf = concat_values(
                    cs, std::span{&args[args.size() - numconc], numconc}
                );
                args.resize(args.size() - numconc, any_value{cs});
                result.set_str(buf);
                force_arg(result, op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_ALIAS:
                static_cast<alias_impl *>(
                    ts.istate->identmap[op >> 8]
                )->set_alias(ts, args.back());
                args.pop_back();
                continue;
            case BC_INST_ALIAS_ARG:
                static_cast<alias_impl *>(
                    ts.istate->identmap[op >> 8]
                )->set_arg(ts, args.back());
                args.pop_back();
                continue;
            case BC_INST_ALIAS_U: {
                auto v = std::move(args.back());
                args.pop_back();
                cs.set_alias(args.back().get_str(), std::move(v));
                args.pop_back();
                continue;
            }

            case BC_INST_CALL | BC_RET_NULL:
            case BC_INST_CALL | BC_RET_STRING:
            case BC_INST_CALL | BC_RET_FLOAT:
            case BC_INST_CALL | BC_RET_INT: {
                result.force_none();
                ident *id = ts.istate->identmap[op >> 13];
                std::size_t callargs = (op >> 8) & 0x1F;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                if (id->get_flags() & IDENT_FLAG_UNKNOWN) {
                    force_arg(result, op & BC_INST_RET_MASK);
                    throw error{
                        cs, "unknown command: %s", id->get_name().data()
                    };
                }
                call_alias(
                    ts, static_cast<alias *>(id), &args[0], result, callargs,
                    nnargs, offset, 0, op
                );
                args.resize(nnargs, any_value{cs});
                continue;
            }
            case BC_INST_CALL_ARG | BC_RET_NULL:
            case BC_INST_CALL_ARG | BC_RET_STRING:
            case BC_INST_CALL_ARG | BC_RET_FLOAT:
            case BC_INST_CALL_ARG | BC_RET_INT: {
                result.force_none();
                ident *id = ts.istate->identmap[op >> 13];
                std::size_t callargs = (op >> 8) & 0x1F;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                if (!ident_is_used_arg(id, ts)) {
                    args.resize(offset, any_value{cs});
                    force_arg(result, op & BC_INST_RET_MASK);
                    continue;
                }
                call_alias(
                    ts, static_cast<alias *>(id), &args[0], result, callargs,
                    nnargs, offset, 0, op
                );
                args.resize(nnargs, any_value{cs});
                continue;
            }

            case BC_INST_CALL_U | BC_RET_NULL:
            case BC_INST_CALL_U | BC_RET_STRING:
            case BC_INST_CALL_U | BC_RET_FLOAT:
            case BC_INST_CALL_U | BC_RET_INT: {
                std::size_t callargs = op >> 8;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                any_value &idarg = args[offset - 1];
                if (idarg.get_type() != value_type::STRING) {
litval:
                    result = std::move(idarg);
                    force_arg(result, op & BC_INST_RET_MASK);
                    args.resize(offset - 1, any_value{cs});
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
                    throw error{
                        cs, "unknown command: %s", ids.data()
                    };
                }
                result.force_none();
                switch (id->get_raw_type()) {
                    default:
                        if (!ident_has_cb(id)) {
                            args.resize(offset - 1, any_value{cs});
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND:
                        callcommand(
                            ts, static_cast<command_impl *>(id),
                            &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1, any_value{cs});
                        continue;
                    case ID_LOCAL: {
                        valarray<ident_stack, MAX_ARGUMENTS> locals{cs};
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            push_alias(
                                cs, args[offset + j].force_ident(cs), locals[j]
                            );
                        }
                        call_with_cleanup([&]() {
                            code = runcode(ts, code, result);
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
                        args.resize(offset - 1, any_value{cs});
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
                        args.resize(offset - 1, any_value{cs});
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
                        args.resize(offset - 1, any_value{cs});
                        force_arg(result, op & BC_INST_RET_MASK);
                        continue;
                    case ID_ALIAS: {
                        alias *a = static_cast<alias *>(id);
                        if (
                            (a->get_index() < MAX_ARGUMENTS) &&
                            !ident_is_used_arg(a, ts)
                        ) {
                            args.resize(offset - 1, any_value{cs});
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                        if (a->get_value().get_type() == value_type::NONE) {
                            goto noid;
                        }
                        call_alias(
                            ts, a, &args[0], result, callargs, nnargs,
                            offset, 1, op
                        );
                        args.resize(nnargs, any_value{cs});
                        continue;
                    }
                }
            }
        }
    }
    return code;
}

void state::run(bcode *code, any_value &ret) {
    runcode(*p_tstate, reinterpret_cast<std::uint32_t *>(code), ret);
}

static void do_run(
    thread_state &ts, std::string_view file, std::string_view code,
    any_value &ret
) {
    codegen_state gs{ts};
    gs.src_name = file;
    gs.code.reserve(64);
    gs.gen_main(code, VAL_ANY);
    gs.done();
    std::uint32_t *cbuf = bcode_alloc(ts.istate, gs.code.size());
    std::memcpy(cbuf, gs.code.data(), gs.code.size() * sizeof(std::uint32_t));
    bcode_incr(cbuf);
    call_with_cleanup([&ts, cbuf, &ret]() {
        runcode(ts, cbuf + 1, ret);
    }, [cbuf]() {
        bcode_decr(cbuf);
    });
}

void state::run(std::string_view code, any_value &ret) {
    do_run(*p_tstate, std::string_view{}, code, ret);
}

void state::run(
    std::string_view code, any_value &ret, std::string_view source
) {
    do_run(*p_tstate, source, code, ret);
}

void state::run(ident *id, std::span<any_value> args, any_value &ret) {
    std::size_t nargs = args.size();
    ret.set_none();
    run_depth_guard level{*p_tstate}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!ident_has_cb(id)) {
                    break;
                }
            /* fallthrough */
            case ident_type::COMMAND: {
                auto *cimpl = static_cast<command_impl *>(id);
                if (nargs < std::size_t(cimpl->get_num_args())) {
                    stack_guard s{*p_tstate}; /* restore after call */
                    auto &targs = p_tstate->vmstack;
                    auto osz = targs.size();
                    targs.resize(osz + cimpl->get_num_args(), any_value{*this});
                    for (std::size_t i = 0; i < nargs; ++i) {
                        targs[osz + i] = args[i];
                    }
                    callcommand(
                        *p_tstate, cimpl, &targs[osz], ret, nargs, false
                    );
                } else {
                    callcommand(*p_tstate, cimpl, &args[0], ret, nargs, false);
                }
                nargs = 0;
                break;
            }
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
                    (a->get_index() < MAX_ARGUMENTS) &&
                    !ident_is_used_arg(a, *p_tstate)
                ) {
                    break;
                }
                if (a->get_value().get_type() == value_type::NONE) {
                    break;
                }
                call_alias(
                    *p_tstate, a, &args[0], ret, nargs, nargs, 0, 0, BC_RET_NULL
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
    ++p_tstate->loop_level;
    try {
        run(code, ret);
    } catch (break_exception) {
        --p_tstate->loop_level;
        return loop_state::BREAK;
    } catch (continue_exception) {
        --p_tstate->loop_level;
        return loop_state::CONTINUE;
    } catch (...) {
        --p_tstate->loop_level;
        throw;
    }
    return loop_state::NORMAL;
}

loop_state state::run_loop(bcode *code) {
    any_value ret{*this};
    return run_loop(code, ret);
}

bool state::is_in_loop() const {
    return !!p_tstate->loop_level;
}

} /* namespace cubescript */
