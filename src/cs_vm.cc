#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_std.hh"
#include "cs_parser.hh"
#include "cs_error.hh"

#include <cstdio>
#include <cmath>
#include <limits>

namespace cubescript {

static inline void push_alias(thread_state &ts, ident &id, ident_stack &st) {
    if (id.type() != ident_type::ALIAS) {
        return;
    }
    if (!static_cast<alias &>(id).is_arg()) {
        auto *aimp = static_cast<alias_impl *>(&id);
        auto ast = ts.get_astack(aimp);
        ast.push(st);
        ast.flags &= ~IDENT_FLAG_UNKNOWN;
    }
}

static inline void pop_alias(thread_state &ts, ident &id) {
    if (id.type() != ident_type::ALIAS) {
        return;
    }
    if (!static_cast<alias &>(id).is_arg()) {
        ts.get_astack(static_cast<alias *>(&id)).pop();
    }
}

static inline void force_arg(state &cs, any_value &v, int type) {
    switch (type) {
        case BC_RET_STRING:
            v.force_string(cs);
            break;
        case BC_RET_INT:
            v.force_integer();
            break;
        case BC_RET_FLOAT:
            v.force_float();
            break;
    }
}

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup
) {
    int i = -1, fakeargs = 0, numargs = int(nargs);
    bool rep = false;
    auto fmt = id->args();
    auto set_fake = [&i, &fakeargs, &rep, args, numargs]() {
        if (++i >= numargs) {
            if (rep) {
                return false;
            }
            args[i].set_none();
            ++fakeargs;
            return false;
        }
        return true;
    };
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case 'i':
                if (set_fake()) {
                    args[i].force_integer();
                }
                break;
            case 'f':
                if (set_fake()) {
                    args[i].force_float();
                }
                break;
            case 's':
                if (set_fake()) {
                    args[i].force_string(*ts.pstate);
                }
                break;
            case 'a':
                set_fake();
                break;
            case 'c':
                if (set_fake()) {
                    if (args[i].type() == value_type::STRING) {
                        auto str = args[i].get_string(*ts.pstate);
                        if (str.empty()) {
                            args[i].set_integer(0);
                        } else {
                            args[i].force_code(*ts.pstate);
                        }
                    }
                }
                break;
            case 'b':
                if (set_fake()) {
                    args[i].force_code(*ts.pstate);
                }
                break;
            case 'v':
                if (set_fake()) {
                    args[i].force_ident(*ts.pstate);
                }
                break;
            case '$':
                i += 1;
                args[i].set_ident(*self);
                break;
            case '#':
                i += 1;
                args[i].set_integer(integer_type(lookup ? -1 : i - fakeargs));
                break;
            case '.':
                i = std::max(i + 1, numargs);
                id->call(ts, span_type<any_value>{args, std::size_t(i)}, res);
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
    id->call(ts, span_type<any_value>{args, std::size_t(i)}, res);
    res.force_plain();
}

bool exec_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs,
    std::size_t offset, std::size_t skip, std::uint32_t op, bool ncheck
) {
    auto &aast = ts.get_astack(a);
    if (ncheck) {
        if (aast.node->val_s.type() == value_type::NONE) {
            return false;
        }
    } else if (aast.flags & IDENT_FLAG_UNKNOWN) {
        throw error_p::make(
            *ts.pstate, "unknown command: %s", a->name().data()
        );
    }
    /* excess arguments get ignored (make error maybe?) */
    callargs = std::min(callargs, MAX_ARGUMENTS);
    builtin_var *anargs = ts.istate->ivar_numargs;
    argset uargs{};
    std::size_t noff = ts.idstack.size();
    for(std::size_t i = 0; i < callargs; i++) {
        auto &ast = ts.get_astack(
            static_cast<alias *>(ts.istate->identmap[i])
        );
        auto &st = ts.idstack.emplace_back();
        ast.push(st);
        st.val_s = std::move(args[offset + i]);
        uargs[i] = true;
    }
    auto oldargs = anargs->value();
    auto oldflags = ts.ident_flags;
    ts.ident_flags = aast.flags;
    any_value cv;
    cv.set_integer(integer_type(callargs));
    anargs->set_raw_value(*ts.pstate, std::move(cv));
    ident_link aliaslink = {a, ts.callstack, uargs};
    ts.callstack = &aliaslink;
    if (!aast.node->code) {
        gen_state gs{ts};
        gs.gen_main(aast.node->val_s.get_string(*ts.pstate));
        aast.node->code = gs.steal_ref();
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
        ts.idstack.resize(noff);
        force_arg(*ts.pstate, result, op & BC_INST_RET_MASK);
        anargs->set_raw_value(*ts.pstate, std::move(oldargs));
        nargs = offset - skip;
    };
    try {
        vm_exec(ts, bcode_p{coderef}.get()->raw(), result);
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    return true;
}

call_depth_guard::call_depth_guard(thread_state &ts): tsp(&ts) {
    if (ts.max_call_depth && (ts.call_depth >= ts.max_call_depth)) {
        throw error{*ts.pstate, "exceeded recursion limit"};
    }
    ++ts.call_depth;
}

call_depth_guard::~call_depth_guard() { --tsp->call_depth; }

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
            throw error_p::make(
                *ts.pstate, "unknown alias lookup: %s", id->name().data()
            );
        }
    }
    return static_cast<alias *>(id);
}

std::uint32_t *vm_exec(
    thread_state &ts, std::uint32_t *code, any_value &result
) {
    result.set_none();
    auto &cs = *ts.pstate;
    call_depth_guard level{ts}; /* incr and decr on scope exit */
    stack_guard guard{ts}; /* resize back to original */
    auto &args = ts.vmstack;
    auto &chook = cs.call_hook();
    if (chook) {
        chook(cs);
    }
    for (;;) {
        std::uint32_t op = *code++;
        switch (op & BC_INST_OP_MASK) {
            case BC_INST_START:
            case BC_INST_OFFSET:
                continue;

            case BC_INST_NULL:
                result.set_none();
                force_arg(cs, result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_FALSE:
                result.set_integer(0);
                force_arg(cs, result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_TRUE:
                result.set_integer(1);
                force_arg(cs, result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_NOT:
                result.set_integer(!args.back().get_bool());
                force_arg(cs, result, op & BC_INST_RET_MASK);
                args.pop_back();
                continue;

            case BC_INST_POP:
                args.pop_back();
                continue;
            case BC_INST_ENTER:
                code = vm_exec(ts, code, args.emplace_back());
                continue;
            case BC_INST_ENTER_RESULT:
                code = vm_exec(ts, code, result);
                continue;

            case BC_INST_EXIT:
                force_arg(cs, result, op & BC_INST_RET_MASK);
                return code;

            case BC_INST_RESULT:
                result = std::move(args.back());
                args.pop_back();
                force_arg(cs, result, op & BC_INST_RET_MASK);
                continue;

            case BC_INST_RESULT_ARG:
                force_arg(cs, result, op & BC_INST_RET_MASK);
                args.emplace_back(std::move(result));
                continue;

            case BC_INST_FORCE:
                force_arg(cs, args.back(), op & BC_INST_RET_MASK);
                continue;

            case BC_INST_DUP: {
                auto &v = args.back();
                auto &nv = args.emplace_back();
                nv = v;
                force_arg(cs, nv, op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_VAL:
                switch (op & BC_INST_RET_MASK) {
                    case BC_RET_STRING: {
                        std::uint32_t len = op >> 8;
                        args.emplace_back().set_string(std::string_view{
                            reinterpret_cast<char const *>(code), len
                        }, cs);
                        code += len / sizeof(std::uint32_t) + 1;
                        continue;
                    }
                    case BC_RET_INT:
                        args.emplace_back().set_integer(
                            *reinterpret_cast<integer_type const *>(code)
                        );
                        code += bc_store_size<integer_type>;
                        continue;
                    case BC_RET_FLOAT:
                        args.emplace_back().set_float(
                            *reinterpret_cast<float_type const *>(code)
                        );
                        code += bc_store_size<float_type>;
                        continue;
                    default:
                        break;
                }
                args.emplace_back().set_none();
                continue;

            case BC_INST_VAL_INT:
                switch (op & BC_INST_RET_MASK) {
                    case BC_RET_STRING: {
                        char s[4] = {
                            char((op >> 8) & 0xFF),
                            char((op >> 16) & 0xFF),
                            char((op >> 24) & 0xFF), '\0'
                        };
                        /* gotta cast or r.size() == potentially 3 */
                        args.emplace_back().set_string(
                            static_cast<char const *>(s), cs
                        );
                        continue;
                    }
                    case BC_RET_INT:
                        args.emplace_back().set_integer(integer_type(op) >> 8);
                        continue;
                    case BC_RET_FLOAT:
                        args.emplace_back().set_float(
                            float_type(integer_type(op) >> 8)
                        );
                        continue;
                    default:
                        break;
                }
                args.emplace_back().set_none();
                continue;

            case BC_INST_LOCAL: {
                std::size_t numlocals = op >> 8;
                std::size_t offset = args.size() - numlocals;
                std::size_t idstsz = ts.idstack.size();
                for (std::size_t i = 0; i < numlocals; ++i) {
                    push_alias(
                        ts, args[offset + i].get_ident(cs),
                        ts.idstack.emplace_back()
                    );
                }
                auto cleanup = [&]() {
                    for (std::size_t i = offset; i < args.size(); ++i) {
                        pop_alias(ts, args[i].get_ident(cs));
                    }
                    ts.idstack.resize(idstsz);
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

            case BC_INST_DO_ARGS:
                call_with_args(ts, [&]() {
                    auto v = std::move(args.back());
                    args.pop_back();
                    result = v.get_code().call(cs);
                    force_arg(cs, result, op & BC_INST_RET_MASK);
                });
                continue;

            case BC_INST_DO: {
                auto v = std::move(args.back());
                args.pop_back();
                result = v.get_code().call(cs);
                force_arg(cs, result, op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_JUMP: {
                std::uint32_t len = op >> 8;
                code += len;
                continue;
            }

            case BC_INST_JUMP_B: {
                std::uint32_t len = op >> 8;
                /* BC_INST_FLAG_TRUE/FALSE */
                if (args.back().get_bool() == !!(op & BC_INST_RET_MASK)) {
                    code += len;
                }
                args.pop_back();
                continue;
            }

            case BC_INST_JUMP_RESULT: {
                std::uint32_t len = op >> 8;
                auto v = std::move(args.back());
                args.pop_back();
                if (v.type() == value_type::CODE) {
                    result = v.get_code().call(cs);
                } else {
                    result = std::move(v);
                }
                /* BC_INST_FLAG_TRUE/FALSE */
                if (result.get_bool() == !!(op & BC_INST_RET_MASK)) {
                    code += len;
                }
                continue;
            }

            case BC_INST_BREAK:
                if (ts.loop_level) {
                    if (op & BC_INST_RET_MASK) {
                        throw continue_exception{};
                    } else {
                        throw break_exception{};
                    }
                } else {
                    if (op & BC_INST_RET_MASK) {
                        throw error{cs, "no loop to continue"};
                    } else {
                        throw error{cs, "no loop to break"};
                    }
                }
                break;

            case BC_INST_BLOCK: {
                std::uint32_t len = op >> 8;
                args.emplace_back().set_code(bcode_p::make_ref(
                    reinterpret_cast<bcode *>(code + 1)
                ));
                code += len;
                continue;
            }

            case BC_INST_EMPTY:
                args.emplace_back().set_code(bcode_p::make_ref(
                    bcode_get_empty(ts.istate->empty, op & BC_INST_RET_MASK)
                ));
                break;

            case BC_INST_COMPILE: {
                any_value &arg = args.back();
                gen_state gs{ts};
                switch (arg.type()) {
                    case value_type::INTEGER:
                        gs.gen_main_integer(arg.get_integer());
                        break;
                    case value_type::FLOAT:
                        gs.gen_main_float(arg.get_float());
                        break;
                    case value_type::STRING:
                        gs.gen_main(arg.get_string(cs));
                        break;
                    default:
                        gs.gen_main_null();
                        break;
                }
                arg.set_code(gs.steal_ref());
                continue;
            }

            case BC_INST_COND: {
                any_value &arg = args.back();
                switch (arg.type()) {
                    case value_type::STRING: {
                        std::string_view s = arg.get_string(cs);
                        if (!s.empty()) {
                            gen_state gs{ts};
                            gs.gen_main(s);
                            arg.set_code(gs.steal_ref());
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
                    ts.get_astack(a).push(ts.idstack.emplace_back());
                    ts.callstack->usedargs[a->index()] = true;
                }
                args.emplace_back().set_ident(*a);
                continue;
            }
            case BC_INST_IDENT_U: {
                any_value &arg = args.back();
                ident *id = ts.istate->id_dummy;
                if (arg.type() == value_type::STRING) {
                    id = &ts.istate->new_ident(
                        cs, arg.get_string(cs), IDENT_FLAG_UNKNOWN
                    );
                }
                alias *a = static_cast<alias *>(id);
                if (a->is_arg() && !ident_is_used_arg(id, ts)) {
                    ts.get_astack(a).push(ts.idstack.emplace_back());
                    ts.callstack->usedargs[id->index()] = true;
                }
                arg.set_ident(*id);
                continue;
            }

            case BC_INST_LOOKUP_U:
                args.back() = cs.lookup_value(args.back().get_string(cs));
                force_arg(cs, args.back(), op & BC_INST_RET_MASK);
                continue;

            case BC_INST_LOOKUP: {
                alias_stack *ast;
                auto &v = args.emplace_back();
                if (get_lookup_id(ts, op, ast)) {
                    v = ast->node->val_s;
                }
                force_arg(cs, args.back(), op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_CONC:
            case BC_INST_CONC_W: {
                std::size_t numconc = op >> 8;
                auto buf = concat_values(
                    cs, span_type<any_value>{
                        &args[args.size() - numconc], numconc
                    }, ((op & BC_INST_OP_MASK) == BC_INST_CONC) ? " " : ""
                );
                args.resize(args.size() - numconc);
                args.emplace_back().set_string(buf);
                force_arg(cs, args.back(), op & BC_INST_RET_MASK);
                continue;
            }

            case BC_INST_VAR:
                args.emplace_back() = static_cast<builtin_var *>(
                    ts.istate->identmap[op >> 8]
                )->value();
                force_arg(cs, args.back(), op & BC_INST_RET_MASK);
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
                cs.assign_value(args.back().get_string(cs), std::move(v));
                args.pop_back();
                continue;
            }

            case BC_INST_CALL: {
                result.force_none();
                ident *id = ts.istate->identmap[op >> 8];
                std::size_t callargs = *code++;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                auto *imp = static_cast<alias_impl *>(id);
                if (imp->is_arg()) {
                    if (!ident_is_used_arg(id, ts)) {
                        args.resize(offset);
                        force_arg(cs, result, op & BC_INST_RET_MASK);
                        continue;
                    }
                }
                exec_alias(
                    ts, imp, &args[0], result, callargs,
                    nnargs, offset, 0, op
                );
                args.resize(nnargs);
                continue;
            }

            case BC_INST_CALL_U: {
                std::size_t callargs = op >> 8;
                std::size_t nnargs = args.size();
                std::size_t offset = nnargs - callargs;
                any_value &idarg = args[offset - 1];
                if (idarg.type() != value_type::STRING) {
litval:
                    result = std::move(idarg);
                    force_arg(cs, result, op & BC_INST_RET_MASK);
                    args.resize(offset - 1);
                    continue;
                }
                auto idn = idarg.get_string(cs);
                auto id = cs.get_ident(idn);
                if (!id) {
noid:
                    if (!is_valid_name(idn)) {
                        goto litval;
                    }
                    result.force_none();
                    force_arg(cs, result, op & BC_INST_RET_MASK);
                    std::string_view ids{idn};
                    throw error_p::make(
                        cs, "unknown command: %s", ids.data()
                    );
                }
                result.force_none();
                switch (ident_p{id->get()}.impl().p_type) {
                    default:
                        if (!ident_is_callable(&id->get())) {
                            args.resize(offset - 1);
                            force_arg(cs, result, op & BC_INST_RET_MASK);
                            continue;
                        }
                    /* fallthrough */
                    case ID_COMMAND: {
                        auto *cimp = static_cast<command_impl *>(&id->get());
                        args.resize(offset + std::max(
                            std::size_t(cimp->arg_count()), callargs
                        ));
                        exec_command(
                            ts, cimp, cimp, &args[offset], result, callargs
                        );
                        force_arg(cs, result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1);
                        continue;
                    }
                    case ID_LOCAL: {
                        std::size_t idstsz = ts.idstack.size();
                        for (size_t j = 0; j < size_t(callargs); ++j) {
                            push_alias(
                                ts, args[offset + j].force_ident(cs),
                                ts.idstack.emplace_back()
                            );
                        }
                        auto cleanup = [&]() {
                            for (size_t j = 0; j < size_t(callargs); ++j) {
                                pop_alias(ts, args[offset + j].get_ident(cs));
                            }
                            ts.idstack.resize(idstsz);
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
                    case ID_VAR: {
                        auto *hid = static_cast<var_impl &>(
                            id->get()
                        ).get_setter(ts);
                        auto *cimp = static_cast<command_impl *>(hid);
                        /* the $ argument */
                        args.insert(offset, any_value{});
                        args.resize(offset + std::max(
                            std::size_t(cimp->arg_count()), callargs
                        ));
                        exec_command(
                            ts, cimp, &id->get(), &args[offset],
                            result, callargs
                        );
                        force_arg(cs, result, op & BC_INST_RET_MASK);
                        args.resize(offset - 1);
                        continue;
                    }
                    case ID_ALIAS: {
                        alias *a = static_cast<alias *>(&id->get());
                        if (a->is_arg() && !ident_is_used_arg(a, ts)) {
                            args.resize(offset - 1);
                            force_arg(cs, result, op & BC_INST_RET_MASK);
                            continue;
                        }
                        if (!exec_alias(
                            ts, a, &args[0], result, callargs, nnargs,
                            offset, 1, op, true
                        )) {
                            goto noid;
                        }
                        args.resize(nnargs);
                        continue;
                    }
                }
            }

            case BC_INST_COM: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 8]
                );
                std::size_t offset = args.size() - id->arg_count();
                result.force_none();
                id->call(ts, span_type<any_value>{
                    &args[offset], std::size_t(id->arg_count())
                }, result);
                force_arg(cs, result, op & BC_INST_RET_MASK);
                args.resize(offset);
                continue;
            }

            case BC_INST_COM_V: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->identmap[op >> 8]
                );
                std::size_t callargs = *code++;
                std::size_t offset = args.size() - callargs;
                result.force_none();
                id->call(
                    ts, span_type<any_value>{&args[offset], callargs}, result
                );
                force_arg(cs, result, op & BC_INST_RET_MASK);
                args.resize(offset);
                continue;
            }
        }
    }
    return code;
}

} /* namespace cubescript */
