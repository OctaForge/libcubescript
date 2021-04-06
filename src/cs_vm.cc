#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_std.hh"
#include "cs_parser.hh"

#include <cstdio>
#include <cmath>
#include <limits>

namespace cubescript {

static inline void push_alias(thread_state &ts, ident *id, ident_stack &st) {
    if (id->is_alias() && !static_cast<alias *>(id)->is_arg()) {
        auto *aimp = static_cast<alias_impl *>(id);
        auto ast = ts.get_astack(aimp);
        ast.push(st);
        ast.flags &= ~IDENT_FLAG_UNKNOWN;
    }
}

static inline void pop_alias(thread_state &ts, ident *id) {
    if (id->is_alias() && !static_cast<alias *>(id)->is_arg()) {
        ts.get_astack(static_cast<alias *>(id)).pop();
    }
}

static inline void force_arg(any_value &v, int type) {
    switch (type) {
        case BC_RET_STRING:
            if (v.get_type() != value_type::STRING) {
                v.force_string();
            }
            break;
        case BC_RET_INT:
            if (v.get_type() != value_type::INTEGER) {
                v.force_integer();
            }
            break;
        case BC_RET_FLOAT:
            if (v.get_type() != value_type::FLOAT) {
                v.force_float();
            }
            break;
    }
}

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup
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
                    args[i].set_integer(0);
                    fakeargs++;
                } else {
                    args[i].force_integer();
                }
                break;
            case 'b':
                if (++i >= numargs) {
                    if (rep) {
                        break;
                    }
                    args[i].set_integer(
                        std::numeric_limits<integer_type>::min()
                    );
                    fakeargs++;
                } else {
                    args[i].force_integer();
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
                    args[i].set_string("");
                    fakeargs++;
                } else {
                    args[i].force_string();
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
                    auto str = args[i].get_string();
                    if (str.empty()) {
                        args[i].set_integer(0);
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
                    args[i].set_ident(ts.istate->id_dummy);
                    fakeargs++;
                } else {
                    args[i].force_ident(*ts.pstate);
                }
                break;
            case '$':
                i += 1;
                args[i].set_ident(self);
                break;
            case 'N':
                i += 1;
                args[i].set_integer(integer_type(lookup ? -1 : i - fakeargs));
                break;
            case 'C': {
                i = std::max(i + 1, numargs);
                any_value tv{*ts.pstate};
                tv.set_string(concat_values(
                    *ts.pstate, std::span{args, std::size_t(i)}, " "
                ));
                static_cast<command_impl *>(id)->call(
                    ts, std::span<any_value>(&tv, &tv + 1), res
                );
                return;
            }
            case 'V':
                i = std::max(i + 1, numargs);
                static_cast<command_impl *>(id)->call(
                    ts, std::span{args, std::size_t(i)}, res
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
        ts, std::span<any_value>{args, std::size_t(i)}, res
    );
    res.force_plain();
}

bool exec_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs,
    std::size_t offset, std::size_t skip, std::uint32_t op, bool ncheck
) {
    auto &aast = ts.get_astack(a);
    if (ncheck) {
        if (aast.node->val_s.get_type() == value_type::NONE) {
            return false;
        }
    } else if (aast.flags & IDENT_FLAG_UNKNOWN) {
        throw error {
            *ts.pstate, "unknown command: %s", a->get_name().data()
        };
    }
    /* excess arguments get ignored (make error maybe?) */
    callargs = std::min(callargs, MAX_ARGUMENTS);
    integer_var *anargs = ts.istate->ivar_numargs;
    argset uargs{};
    std::size_t noff = ts.idstack.size();
    for(std::size_t i = 0; i < callargs; i++) {
        auto &ast = ts.get_astack(
            static_cast<alias *>(ts.istate->identmap[i])
        );
        auto &st = ts.idstack.emplace_back(*ts.pstate);
        ast.push(st);
        st.val_s = std::move(args[offset + i]);
        uargs[i] = true;
    }
    auto oldargs = anargs->get_value();
    auto oldflags = ts.ident_flags;
    ts.ident_flags = aast.flags;
    anargs->set_raw_value(integer_type(callargs));
    ident_link aliaslink = {a, ts.callstack, uargs};
    ts.callstack = &aliaslink;
    if (!aast.node->code) {
        codegen_state gs{ts};
        gs.code.reserve(64);
        gs.gen_main(aast.node->val_s.get_string());
        /* i wish i could steal the memory somehow */
        uint32_t *code = bcode_alloc(ts.istate, gs.code.size());
        memcpy(code, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        aast.node->code = bcode_ref{reinterpret_cast<bcode *>(code + 1)};
    }
    bcode_ref coderef = aast.node->code;
    auto cleanup = [&]() {
        ts.callstack = aliaslink.next;
        ts.ident_flags = oldflags;
        auto amask = aliaslink.usedargs;
        for (std::size_t i = 0; i < callargs; i++) {
            ts.get_astack(
                static_cast<alias *>(ts.istate->identmap[i])
            ).pop();
            amask[i] = false;
        }
        for (; amask.any(); ++callargs) {
            if (amask[callargs]) {
                ts.get_astack(
                    static_cast<alias *>(ts.istate->identmap[callargs])
                ).pop();
                amask[callargs] = false;
            }
        }
        ts.idstack.resize(noff, ident_stack{*ts.pstate});
        force_arg(result, op & BC_INST_RET_MASK);
        anargs->set_raw_value(integer_type(oldargs));
        nargs = offset - skip;
    };
    try {
        bcode *p = coderef;
        vm_exec(ts, p->get_raw(), result);
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    return true;
}

run_depth_guard::run_depth_guard(thread_state &ts): tsp(&ts) {
    if (ts.max_run_depth && (ts.run_depth >= ts.max_run_depth)) {
        throw error{*ts.pstate, "exceeded recursion limit"};
    }
    ++ts.run_depth;
}

run_depth_guard::~run_depth_guard() { --tsp->run_depth; }

static inline alias *get_lookup_id(
    thread_state &ts, std::uint32_t op, alias_stack *&ast
) {
    ident *id = ts.istate->identmap[op >> 8];
    auto *a = static_cast<alias_impl *>(id);

    if (a->is_arg()) {
        if (!ident_is_used_arg(id, ts)) {
            return nullptr;
        }
        ast = &ts.get_astack(static_cast<alias *>(id));
    } else {
        ast = &ts.get_astack(static_cast<alias *>(id));
        if (ast->flags & IDENT_FLAG_UNKNOWN) {
            throw error{
                *ts.pstate, "unknown alias lookup: %s", id->get_name().data()
            };
        }
    }
    return static_cast<alias *>(id);
}

static inline int get_lookupu_type(
    thread_state &ts, any_value &arg, ident *&id, std::uint32_t op,
    alias_stack *&ast
) {
    if (arg.get_type() != value_type::STRING) {
        return -2; /* default case */
    }
    id = ts.pstate->get_ident(arg.get_string());
    if (id) {
        switch(id->get_type()) {
            case ident_type::ALIAS: {
                auto *a = static_cast<alias_impl *>(id);
                ast = &ts.get_astack(static_cast<alias *>(id));
                if (ast->flags & IDENT_FLAG_UNKNOWN) {
                    break;
                }
                if (a->is_arg() && !ident_is_used_arg(id, ts)) {
                    return ID_UNKNOWN;
                }
                return ID_ALIAS;
            }
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
                exec_command(ts, cimpl, cimpl, &args[osz], arg, 0, true);
                force_arg(arg, op & BC_INST_RET_MASK);
                return -2; /* ignore */
            }
            default:
                return ID_UNKNOWN;
        }
    }
    throw error{*ts.pstate, "unknown alias lookup: %s", arg.get_string().data()};
}

std::uint32_t *vm_exec(
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
                result.set_string("");
                continue;
            case BC_INST_NULL | BC_RET_INT:
                result.set_integer(0);
                continue;
            case BC_INST_NULL | BC_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case BC_INST_FALSE | BC_RET_STRING:
                result.set_string("0");
                continue;
            case BC_INST_FALSE | BC_RET_NULL:
            case BC_INST_FALSE | BC_RET_INT:
                result.set_integer(0);
                continue;
            case BC_INST_FALSE | BC_RET_FLOAT:
                result.set_float(0.0f);
                continue;

            case BC_INST_TRUE | BC_RET_STRING:
                result.set_string("1");
                continue;
            case BC_INST_TRUE | BC_RET_NULL:
            case BC_INST_TRUE | BC_RET_INT:
                result.set_integer(1);
                continue;
            case BC_INST_TRUE | BC_RET_FLOAT:
                result.set_float(1.0f);
                continue;

            case BC_INST_NOT | BC_RET_STRING:
                result.set_string(args.back().get_bool() ? "0" : "1");
                args.pop_back();
                continue;
            case BC_INST_NOT | BC_RET_NULL:
            case BC_INST_NOT | BC_RET_INT:
                result.set_integer(!args.back().get_bool());
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
                code = vm_exec(ts, code, args.emplace_back(cs));
                continue;
            case BC_INST_ENTER_RESULT:
                code = vm_exec(ts, code, result);
                continue;
            case BC_INST_EXIT | BC_RET_STRING:
            case BC_INST_EXIT | BC_RET_INT:
            case BC_INST_EXIT | BC_RET_FLOAT:
                force_arg(result, op & BC_INST_RET_MASK);
            /* fallthrough */
            case BC_INST_EXIT | BC_RET_NULL:
                return code;

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

            case BC_INST_RESULT_ARG | BC_RET_STRING:
            case BC_INST_RESULT_ARG | BC_RET_INT:
            case BC_INST_RESULT_ARG | BC_RET_FLOAT:
                force_arg(result, op & BC_INST_RET_MASK);
            /* fallthrough */
            case BC_INST_RESULT_ARG | BC_RET_NULL:
                args.emplace_back(std::move(result));
                continue;

            case BC_INST_FORCE | BC_RET_STRING:
                args.back().force_string();
                continue;
            case BC_INST_FORCE | BC_RET_INT:
                args.back().force_integer();
                continue;
            case BC_INST_FORCE | BC_RET_FLOAT:
                args.back().force_float();
                continue;

            case BC_INST_DUP | BC_RET_NULL: {
                auto &v = args.back();
                args.emplace_back(cs) = v.get_plain();
                continue;
            }
            case BC_INST_DUP | BC_RET_INT: {
                auto &v = args.back();
                args.emplace_back(cs).set_integer(v.get_integer());
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
                nv.force_string();
                continue;
            }

            case BC_INST_VAL | BC_RET_STRING: {
                std::uint32_t len = op >> 8;
                args.emplace_back(cs).set_string(std::string_view{
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
                args.emplace_back(cs).set_string(static_cast<char const *>(s));
                continue;
            }
            case BC_INST_VAL | BC_RET_NULL:
            case BC_INST_VAL_INT | BC_RET_NULL:
                args.emplace_back(cs).set_none();
                continue;
            case BC_INST_VAL | BC_RET_INT:
                args.emplace_back(cs).set_integer(
                    *reinterpret_cast<integer_type const *>(code)
                );
                code += bc_store_size<integer_type>;
                continue;
            case BC_INST_VAL_INT | BC_RET_INT:
                args.emplace_back(cs).set_integer(integer_type(op) >> 8);
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

            case BC_INST_LOCAL: {
                std::size_t numlocals = op >> 8;
                std::size_t offset = args.size() - numlocals;
                std::size_t idstsz = ts.idstack.size();
                for (std::size_t i = 0; i < numlocals; ++i) {
                    push_alias(
                        ts, args[offset + i].get_ident(),
                        ts.idstack.emplace_back(*ts.pstate)
                    );
                }
                auto cleanup = [&]() {
                    for (std::size_t i = offset; i < args.size(); ++i) {
                        pop_alias(ts, args[i].get_ident());
                    }
                    ts.idstack.resize(idstsz, ident_stack{*ts.pstate});
                };
                try {
                    code = vm_exec(ts, code, result);
                } catch (...) {
                    cleanup();
                    throw;
                }
                cleanup();
                return code;
            }

            case BC_INST_DO_ARGS | BC_RET_NULL:
            case BC_INST_DO_ARGS | BC_RET_STRING:
            case BC_INST_DO_ARGS | BC_RET_INT:
            case BC_INST_DO_ARGS | BC_RET_FLOAT:
                call_with_args(ts, [&]() {
                    auto v = std::move(args.back());
                    args.pop_back();
                    result = cs.run(v.get_code());
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
                result = cs.run(v.get_code());
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
                    result = cs.run(v.get_code());
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
                    result = cs.run(v.get_code());
                } else {
                    result = std::move(v);
                }
                if (!result.get_bool()) {
                    code += len;
                }
                continue;
            }
            case BC_INST_BREAK | BC_INST_FLAG_FALSE:
                if (ts.loop_level) {
                    throw break_exception();
                } else {
                    throw error{cs, "no loop to break"};
                }
                break;
            case BC_INST_BREAK | BC_INST_FLAG_TRUE:
                if (ts.loop_level) {
                    throw continue_exception();
                } else {
                    throw error{cs, "no loop to continue"};
                }
                break;

            case BC_INST_BLOCK: {
                std::uint32_t len = op >> 8;
                args.emplace_back(cs).set_code(
                    reinterpret_cast<bcode *>(code + 1)
                );
                code += len;
                continue;
            }

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

            case BC_INST_COMPILE: {
                any_value &arg = args.back();
                codegen_state gs{ts};
                switch (arg.get_type()) {
                    case value_type::INTEGER:
                        gs.code.reserve(8);
                        gs.code.push_back(BC_INST_START);
                        gs.gen_int(arg.get_integer());
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
                        gs.gen_main(arg.get_string());
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
                        std::string_view s = arg.get_string();
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

            case BC_INST_IDENT: {
                alias *a = static_cast<alias *>(
                    ts.istate->identmap[op >> 8]
                );
                if (a->is_arg() && !ident_is_used_arg(a, ts)) {
                    ts.get_astack(a).push(ts.idstack.emplace_back(*ts.pstate));
                    ts.callstack->usedargs[a->get_index()] = true;
                }
                args.emplace_back(cs).set_ident(a);
                continue;
            }
            case BC_INST_IDENT_U: {
                any_value &arg = args.back();
                ident *id = ts.istate->id_dummy;
                if (arg.get_type() == value_type::STRING) {
                    id = &ts.istate->new_ident(
                        cs, arg.get_string(), IDENT_FLAG_UNKNOWN
                    );
                }
                alias *a = static_cast<alias *>(id);
                if (a->is_arg() && !ident_is_used_arg(id, ts)) {
                    ts.get_astack(a).push(ts.idstack.emplace_back(*ts.pstate));
                    ts.callstack->usedargs[id->get_index()] = true;
                }
                arg.set_ident(id);
                continue;
            }

            case BC_INST_LOOKUP_U | BC_RET_STRING: {
                ident *id = nullptr;
                alias_stack *ast;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op, ast)) {
                    case ID_ALIAS:
                        arg = ast->node->val_s;
                        arg.force_string();
                        continue;
                    case ID_SVAR:
                        arg.set_string(
                            static_cast<string_var *>(id)->get_value()
                        );
                        continue;
                    case ID_IVAR:
                        arg.set_integer(
                            static_cast<integer_var *>(id)->get_value()
                         );
                        arg.force_string();
                        continue;
                    case ID_FVAR:
                        arg.set_float(
                            static_cast<float_var *>(id)->get_value()
                        );
                        arg.force_string();
                        continue;
                    case ID_UNKNOWN:
                        arg.set_string("");
                        continue;
                    default:
                        continue;
                }
            }

            case BC_INST_LOOKUP | BC_RET_STRING: {
                alias_stack *ast;
                alias *a = get_lookup_id(ts, op, ast);
                if (!a) {
                    args.emplace_back(cs).set_string("");
                } else {
                    auto &v = args.emplace_back(cs);
                    v = ast->node->val_s;
                    v.force_string();
                }
                continue;
            }

            case BC_INST_LOOKUP_U | BC_RET_INT: {
                ident *id = nullptr;
                alias_stack *ast;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op, ast)) {
                    case ID_ALIAS:
                        arg.set_integer(ast->node->val_s.get_integer());
                        continue;
                    case ID_SVAR:
                        arg.set_integer(parse_int(
                            static_cast<string_var *>(id)->get_value()
                        ));
                        continue;
                    case ID_IVAR:
                        arg.set_integer(
                            static_cast<integer_var *>(id)->get_value()
                        );
                        continue;
                    case ID_FVAR:
                        arg.set_integer(integer_type(
                            static_cast<float_var *>(id)->get_value()
                        ));
                        continue;
                    case ID_UNKNOWN:
                        arg.set_integer(0);
                        continue;
                    default:
                        continue;
                }
            }
            case BC_INST_LOOKUP | BC_RET_INT: {
                alias_stack *ast;
                alias *a = get_lookup_id(ts, op, ast);
                if (!a) {
                    args.emplace_back(cs).set_integer(0);
                } else {
                    args.emplace_back(cs).set_integer(
                        ast->node->val_s.get_integer()
                    );
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_FLOAT: {
                ident *id = nullptr;
                alias_stack *ast;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op, ast)) {
                    case ID_ALIAS:
                        arg.set_float(ast->node->val_s.get_float());
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
            case BC_INST_LOOKUP | BC_RET_FLOAT: {
                alias_stack *ast;
                alias *a = get_lookup_id(ts, op, ast);
                if (!a) {
                    args.emplace_back(cs).set_float(float_type(0));
                } else {
                    args.emplace_back(cs).set_float(
                        ast->node->val_s.get_float()
                    );
                }
                continue;
            }
            case BC_INST_LOOKUP_U | BC_RET_NULL: {
                ident *id = nullptr;
                alias_stack *ast;
                any_value &arg = args.back();
                switch (get_lookupu_type(ts, arg, id, op, ast)) {
                    case ID_ALIAS:
                        arg = ast->node->val_s.get_plain();
                        continue;
                    case ID_SVAR:
                        arg.set_string(
                            static_cast<string_var *>(id)->get_value()
                        );
                        continue;
                    case ID_IVAR:
                        arg.set_integer(
                            static_cast<integer_var *>(id)->get_value()
                        );
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
            case BC_INST_LOOKUP | BC_RET_NULL: {
                alias_stack *ast;
                alias *a = get_lookup_id(ts, op, ast);
                if (!a) {
                    args.emplace_back(cs).set_none();
                } else {
                    args.emplace_back(cs) = ast->node->val_s.get_plain();
                }
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
                args.emplace_back(cs).set_string(buf);
                force_arg(args.back(), op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_SVAR | BC_RET_STRING:
            case BC_INST_SVAR | BC_RET_NULL:
                args.emplace_back(cs).set_string(static_cast<string_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_SVAR | BC_RET_INT:
                args.emplace_back(cs).set_integer(parse_int(
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

            case BC_INST_IVAR | BC_RET_INT:
            case BC_INST_IVAR | BC_RET_NULL:
                args.emplace_back(cs).set_integer(static_cast<integer_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_IVAR | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v.set_integer(static_cast<integer_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                v.force_string();
                continue;
            }
            case BC_INST_IVAR | BC_RET_FLOAT:
                args.emplace_back(cs).set_float(float_type(
                    static_cast<integer_var *>(
                        ts.istate->identmap[op >> 8]
                    )->get_value()
                ));
                continue;

            case BC_INST_FVAR | BC_RET_FLOAT:
            case BC_INST_FVAR | BC_RET_NULL:
                args.emplace_back(cs).set_float(static_cast<float_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                continue;
            case BC_INST_FVAR | BC_RET_STRING: {
                auto &v = args.emplace_back(cs);
                v.set_float(static_cast<float_var *>(
                    ts.istate->identmap[op >> 8]
                )->get_value());
                v.force_string();
                continue;
            }
            case BC_INST_FVAR | BC_RET_INT:
                args.emplace_back(cs).set_integer(
                    integer_type(std::floor(static_cast<float_var *>(
                        ts.istate->identmap[op >> 8]
                    )->get_value()))
                );
                continue;

            case BC_INST_ALIAS: {
                auto *a = static_cast<alias *>(
                    ts.istate->identmap[op >> 8]
                );
                auto &ast = ts.get_astack(a);
                if (a->is_arg()) {
                    ast.set_arg(a, ts, args.back());
                } else {
                    ast.set_alias(a, ts, args.back());
                }
                args.pop_back();
                continue;
            }
            case BC_INST_ALIAS_U: {
                auto v = std::move(args.back());
                args.pop_back();
                cs.set_alias(args.back().get_string(), std::move(v));
                args.pop_back();
                continue;
            }

            case BC_INST_CALL | BC_RET_NULL:
            case BC_INST_CALL | BC_RET_STRING:
            case BC_INST_CALL | BC_RET_FLOAT:
            case BC_INST_CALL | BC_RET_INT: {
                result.force_none();
                ident *id = ts.istate->identmap[op >> 8];
                std::size_t callargs = *code++;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                auto *imp = static_cast<alias_impl *>(id);
                if (imp->is_arg()) {
                    if (!ident_is_used_arg(id, ts)) {
                        args.resize(offset, any_value{cs});
                        force_arg(result, op & BC_INST_RET_MASK);
                        continue;
                    }
                }
                exec_alias(
                    ts, imp, &args[0], result, callargs,
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
                auto idn = idarg.get_string();
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
                switch (ident_p{*id}.impl().p_type) {
                    default:
                        if (!ident_is_callable(id)) {
                            args.resize(offset - 1, any_value{cs});
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND: {
                        auto *cimp = static_cast<command_impl *>(id);
                        args.resize(offset + std::max(
                            std::size_t(cimp->get_num_args()), callargs
                        ), any_value{cs});
                        exec_command(
                            ts, cimp, cimp, &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1, any_value{cs});
                        continue;
                    }
                    case ID_LOCAL: {
                        std::size_t idstsz = ts.idstack.size();
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            push_alias(
                                ts, &args[offset + j].force_ident(cs),
                                ts.idstack.emplace_back(*ts.pstate)
                            );
                        }
                        auto cleanup = [&]() {
                            for (size_t j = 0; j < size_t(callargs); ++j) {
                                pop_alias(ts, args[offset + j].get_ident());
                            }
                            ts.idstack.resize(idstsz, ident_stack{*ts.pstate});
                        };
                        try {
                            code = vm_exec(ts, code, result);
                        } catch (...) {
                            cleanup();
                            throw;
                        }
                        cleanup();
                        return code;
                    }
                    case ID_IVAR: {
                        auto *hid = ts.istate->cmd_ivar;
                        auto *cimp = static_cast<command_impl *>(hid);
                        /* the $ argument */
                        args.insert(offset, any_value{cs});
                        args.resize(offset + std::max(
                            std::size_t(cimp->get_num_args()), callargs
                        ), any_value{cs});
                        exec_command(
                            ts, cimp, id, &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1, any_value{cs});
                        continue;
                    }
                    case ID_FVAR: {
                        auto *hid = ts.istate->cmd_fvar;
                        auto *cimp = static_cast<command_impl *>(hid);
                        /* the $ argument */
                        args.insert(offset, any_value{cs});
                        args.resize(offset + std::max(
                            std::size_t(cimp->get_num_args()), callargs
                        ), any_value{cs});
                        exec_command(
                            ts, cimp, id, &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1, any_value{cs});
                        continue;
                    }
                    case ID_SVAR: {
                        auto *hid = ts.istate->cmd_svar;
                        auto *cimp = static_cast<command_impl *>(hid);
                        /* the $ argument */
                        args.insert(offset, any_value{cs});
                        args.resize(offset + std::max(
                            std::size_t(cimp->get_num_args()), callargs
                        ), any_value{cs});
                        exec_command(
                            ts, cimp, id, &args[offset], result, callargs
                        );
                        force_arg(result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1, any_value{cs});
                        continue;
                    }
                    case ID_ALIAS: {
                        alias *a = static_cast<alias *>(id);
                        if (a->is_arg() && !ident_is_used_arg(a, ts)) {
                            args.resize(offset - 1, any_value{cs});
                            force_arg(result, op & BC_INST_RET_MASK);
                            continue;
                        }
                        if (!exec_alias(
                            ts, a, &args[0], result, callargs, nnargs,
                            offset, 1, op, true
                        )) {
                            goto noid;
                        }
                        args.resize(nnargs, any_value{cs});
                        continue;
                    }
                }
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
                id->call(ts, std::span<any_value>{
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
                    ts.istate->identmap[op >> 8]
                );
                std::size_t callargs = *code++;
                std::size_t offset = args.size() - callargs;
                result.force_none();
                id->call(ts, std::span{&args[offset], callargs}, result);
                force_arg(result, op & BC_INST_RET_MASK);
                args.resize(offset, any_value{cs});
                continue;
            }
            case BC_INST_COM_C | BC_RET_NULL:
            case BC_INST_COM_C | BC_RET_STRING:
            case BC_INST_COM_C | BC_RET_FLOAT:
            case BC_INST_COM_C | BC_RET_INT: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 8]
                );
                std::size_t callargs = *code++;
                std::size_t offset = args.size() - callargs;
                result.force_none();
                {
                    any_value tv{cs};
                    tv.set_string(concat_values(cs, std::span{
                        &args[offset], callargs
                    }, " "));
                    id->call(ts, std::span<any_value>{&tv, 1}, result);
                }
                force_arg(result, op & BC_INST_RET_MASK);
                args.resize(offset, any_value{cs});
                continue;
            }
        }
    }
    return code;
}

} /* namespace cubescript */
