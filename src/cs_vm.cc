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

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup
) {
    int i = -1, fakeargs = 0, numargs = int(nargs);
    bool rep = false;
    auto fmt = id->args();
    auto set_fake = [](
        int &idx, int &fargs, bool r, int argn, any_value *argp
    ) {
        if (++idx >= argn) {
            if (r) {
                return false;
            }
            argp[idx].set_none();
            ++fargs;
            return false;
        }
        return true;
    };
    for (auto it = fmt.begin(); it != fmt.end(); ++it) {
        switch (*it) {
            case 'i':
                if (set_fake(i, fakeargs, rep, numargs, args)) {
                    args[i].force_integer();
                }
                break;
            case 'f':
                if (set_fake(i, fakeargs, rep, numargs, args)) {
                    args[i].force_float();
                }
                break;
            case 's':
                if (set_fake(i, fakeargs, rep, numargs, args)) {
                    args[i].force_string(*ts.pstate);
                }
                break;
            case 'a':
                set_fake(i, fakeargs, rep, numargs, args);
                break;
            case 'c':
                if (set_fake(i, fakeargs, rep, numargs, args)) {
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
                if (set_fake(i, fakeargs, rep, numargs, args)) {
                    args[i].force_code(*ts.pstate);
                }
                break;
            case 'v':
                if (set_fake(i, fakeargs, rep, numargs, args)) {
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
                id->call_id(ts, span_type<any_value>{args, std::size_t(i)}, res);
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
    id->call_id(ts, span_type<any_value>{args, std::size_t(i)}, res);
    res.force_plain();
}

any_value exec_alias(
    thread_state &ts, alias *a, any_value *args,
    std::size_t callargs, alias_stack &astack
) {
    /* excess arguments get ignored (make error maybe?) */
    any_value ret;
    callargs = std::min(callargs, MAX_ARGUMENTS);
    builtin_var *anargs = ts.istate->ivar_numargs;
    argset uargs{};
    std::size_t noff = ts.idstack.size();
    for(std::size_t i = 0; i < callargs; i++) {
        auto &ast = ts.get_astack(
            static_cast<alias *>(ts.istate->argmap[i])
        );
        auto &st = ts.idstack.emplace_back();
        ast.push(st);
        st.val_s = std::move(args[i]);
        uargs[i] = true;
    }
    auto oldargs = anargs->value();
    auto oldflags = ts.ident_flags;
    ts.ident_flags = astack.flags;
    any_value cv;
    cv.set_integer(integer_type(callargs));
    anargs->set_raw_value(*ts.pstate, std::move(cv));
    auto &lev = ts.callstack.emplace_back(*a);
    lev.usedargs = std::move(uargs);
    if (!astack.node->code) {
        try {
            gen_state gs{ts};
            gs.gen_main(astack.node->val_s.get_string(*ts.pstate));
            astack.node->code = gs.steal_ref();
        } catch (...) {
            ts.callstack.pop_back();
            throw;
        }
    }
    bcode_ref coderef = astack.node->code;
    auto cleanup = [](
        auto &tss, std::size_t cargs, std::size_t nids, auto oflags
    ) {
        auto amask = tss.callstack.back().usedargs;
        tss.callstack.pop_back();
        tss.ident_flags = oflags;
        for (std::size_t i = 0; i < cargs; i++) {
            tss.get_astack(
                static_cast<alias *>(tss.istate->argmap[i])
            ).pop();
            amask[i] = false;
        }
        for (; amask.any(); ++cargs) {
            if (amask[cargs]) {
                tss.get_astack(
                    static_cast<alias *>(tss.istate->argmap[cargs])
                ).pop();
                amask[cargs] = false;
            }
        }
        tss.idstack.resize(nids);
    };
    try {
        vm_exec(ts, bcode_p{coderef}.get()->raw(), ret);
    } catch (...) {
        cleanup(ts, callargs, noff, oldflags);
        anargs->set_raw_value(*ts.pstate, std::move(oldargs));
        throw;
    }
    cleanup(ts, callargs, noff, oldflags);
    anargs->set_raw_value(*ts.pstate, std::move(oldargs));
    return ret;
}

any_value exec_code_with_args(thread_state &ts, bcode_ref const &body) {
    if (ts.callstack.empty()) {
        return body.call(*ts.pstate);
    }
    auto mask = ts.callstack.back().usedargs;
    std::size_t noff = ts.idstack.size();
    for (std::size_t i = 0; mask.any(); ++i) {
        if (mask[0]) {
            auto &ast = ts.get_astack(
                static_cast<alias *>(ts.istate->argmap[i])
            );
            auto &st = ts.idstack.emplace_back();
            st.next = ast.node;
            ast.node = ast.node->next;
        }
        mask >>= 1;
    }
    ident_level *prevstack = nullptr;
    if (ts.callstack.size() >= 2) {
        prevstack = &ts.callstack[ts.callstack.size() - 2];
    }
    auto &lev = ts.callstack.emplace_back(ts.callstack.back().id);
    if (!prevstack) {
        lev.usedargs.set();
    } else {
        lev.usedargs = prevstack->usedargs;
    }
    auto cleanup = [](auto &tss, ident_level *pstack, std::size_t offn) {
        auto mask2 = tss.callstack.back().usedargs;
        if (pstack) {
            pstack->usedargs = mask2;
        }
        tss.callstack.pop_back();
        for (std::size_t i = 0, nredo = 0; mask2.any(); ++i) {
            if (mask2[0]) {
                tss.get_astack(
                    static_cast<alias *>(tss.istate->argmap[i])
                ).node = tss.idstack[offn + nredo++].next;
            }
            mask2 >>= 1;
        }
    };
    any_value ret;
    try {
        ret = body.call(*ts.pstate);
    } catch (...) {
        cleanup(ts, prevstack, noff);
        ts.idstack.resize(noff);
        throw;
    }
    cleanup(ts, prevstack, noff);
    ts.idstack.resize(noff);
    return ret;
}

struct vm_guard {
    vm_guard(thread_state &s): ts{s}, oldtop{s.vmstack.size()} {
        if (s.max_call_depth && (s.call_depth >= s.max_call_depth)) {
            throw error{*s.pstate, "exceeded recursion limit"};
        }
        ++s.call_depth;
    }

    ~vm_guard() {
        --ts.call_depth;
        ts.vmstack.resize(oldtop);
    }

    thread_state &ts;
    std::size_t oldtop;
};

std::uint32_t *vm_exec(
    thread_state &ts, std::uint32_t *code, any_value &result
) {
    result.set_none();
    auto &cs = *ts.pstate;
    vm_guard scope{ts}; /* keep track of recursion depth + manage stack */
    auto &args = ts.vmstack;
    auto &chook = cs.call_hook();
    if (chook) {
        chook(cs);
    }
    auto force_val = [](state &s, any_value &v, int opn) {
        switch (opn & BC_INST_RET_MASK) {
            case BC_RET_STRING:
                v.force_string(s);
                break;
            case BC_RET_INT:
                v.force_integer();
                break;
            case BC_RET_FLOAT:
                v.force_float();
                break;
        }
    };
    for (;;) {
        std::uint32_t op = *code++;
        switch (op & BC_INST_OP_MASK) {
            case BC_INST_START:
            case BC_INST_OFFSET:
                continue;

            case BC_INST_NULL:
                result.set_none();
                goto use_result;

            case BC_INST_FALSE:
                result.set_integer(0);
                goto use_result;

            case BC_INST_TRUE:
                result.set_integer(1);
                goto use_result;

            case BC_INST_NOT:
                result.set_integer(!args.back().get_bool());
                args.pop_back();
                goto use_result;

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
                goto use_exit;

            case BC_INST_RESULT:
                result = std::move(args.back());
                args.pop_back();
                goto use_result;

            case BC_INST_RESULT_ARG:
                args.emplace_back(std::move(result));
                goto use_top;

            case BC_INST_FORCE:
                goto use_top;

            case BC_INST_DUP: {
                auto &v = args.back();
                args.emplace_back() = v;
                goto use_top;
            }

            case BC_INST_VAL:
                switch (op & BC_INST_RET_MASK) {
                    case BC_RET_STRING: {
                        auto len = op >> 8;
                        char const *str;
                        std::memcpy(&str, &code, sizeof(str));
                        std::string_view sv{str, len};
                        args.emplace_back().set_string(sv, cs);
                        code += len / sizeof(std::uint32_t) + 1;
                        continue;
                    }
                    case BC_RET_INT: {
                        integer_type i;
                        std::memcpy(&i, code, sizeof(i));
                        args.emplace_back().set_integer(i);
                        code += bc_store_size<integer_type>;
                        continue;
                    }
                    case BC_RET_FLOAT: {
                        float_type f;
                        std::memcpy(&f, code, sizeof(f));
                        args.emplace_back().set_float(f);
                        code += bc_store_size<float_type>;
                        continue;
                    }
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
                        args.emplace_back().set_string(s, cs);
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
                auto cleanup = [](
                    auto &css, std::size_t off, std::size_t isz, auto &av
                ) {
                    for (std::size_t i = off; i < av.size(); ++i) {
                        pop_alias(state_p{css}.ts(), av[i].get_ident(css));
                    }
                    state_p{css}.ts().idstack.resize(isz);
                };
                try {
                    code = vm_exec(ts, code, result);
                } catch (...) {
                    cleanup(cs, offset, idstsz, args);
                    throw;
                }
                cleanup(cs, offset, idstsz, args);
                return code;
            }

            case BC_INST_DO_ARGS: {
                auto v = std::move(args.back());
                args.pop_back();
                result = exec_code_with_args(ts, v.get_code());
                goto use_result;
            }

            case BC_INST_DO: {
                auto v = std::move(args.back());
                args.pop_back();
                result = v.get_code().call(cs);
                goto use_result;
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
                bcode *b;
                code += 1;
                std::memcpy(&b, &code, sizeof(b));
                args.emplace_back().set_code(bcode_p::make_ref(b));
                code += len - 1;
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
                    ts.istate->lookup_ident(op >> 8)
                );
                if (a->is_arg() && !ident_is_used_arg(a, ts)) {
                    ts.get_astack(a).push(ts.idstack.emplace_back());
                    ts.callstack.back().usedargs[a->index()] = true;
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
                    ts.callstack.back().usedargs[id->index()] = true;
                }
                arg.set_ident(*id);
                continue;
            }

            case BC_INST_LOOKUP_U:
                args.back() = cs.lookup_value(args.back().get_string(cs));
                goto use_top;

            case BC_INST_LOOKUP: {
                ident *id = ts.istate->lookup_ident(op >> 8);
                if (static_cast<alias *>(id)->is_arg()) {
                    auto &v = args.emplace_back();
                    if (ident_is_used_arg(id, ts)) {
                        v = ts.get_astack(static_cast<alias *>(id)).node->val_s;
                    }
                    goto use_top;
                }
                auto &ast = ts.get_astack(static_cast<alias *>(id));
                if (ast.flags & IDENT_FLAG_UNKNOWN) {
                    throw error_p::make(
                        *ts.pstate, "unknown alias lookup: %s",
                        id->name().data()
                    );
                }
                args.emplace_back() = ast.node->val_s;
                goto use_top;
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
                goto use_top;
            }

            case BC_INST_VAR:
                args.emplace_back() = static_cast<builtin_var *>(
                    ts.istate->lookup_ident(op >> 8)
                )->value();
                goto use_top;

            case BC_INST_ALIAS: {
                auto *a = static_cast<alias *>(
                    ts.istate->lookup_ident(op >> 8)
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
                ident *id = ts.istate->lookup_ident(op >> 8);
                std::size_t callargs = *code++;
                std::size_t offset = args.size() - callargs;
                auto *imp = static_cast<alias_impl *>(id);
                if (imp->is_arg()) {
                    if (!ident_is_used_arg(id, ts)) {
                        args.resize(offset);
                        goto use_result;
                    }
                }
                auto &ast = ts.get_astack(imp);
                if (ast.flags & IDENT_FLAG_UNKNOWN) {
                    throw error_p::make(
                        cs, "unknown command: %s", id->name().data()
                    );
                }
                result = exec_alias(ts, imp, &args[offset], callargs, ast);
                args.resize(offset);
                goto use_result;
            }

            case BC_INST_CALL_U: {
                std::size_t callargs = op >> 8;
                std::size_t offset = args.size() - callargs;
                any_value &idarg = args[offset - 1];
                if (idarg.type() != value_type::STRING) {
litval:
                    result = std::move(idarg);
                    args.resize(offset - 1);
                    goto use_result;
                }
                auto idn = idarg.get_string(cs);
                auto id = cs.get_ident(idn);
                if (!id) {
noid:
                    if (!is_valid_name(idn)) {
                        goto litval;
                    }
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
                            goto use_result;
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
                        args.resize(offset - 1);
                        goto use_result;
                    }
                    case ID_LOCAL: {
                        std::size_t idstsz = ts.idstack.size();
                        for (std::size_t j = 0; j < callargs; ++j) {
                            push_alias(
                                ts, args[offset + j].force_ident(cs),
                                ts.idstack.emplace_back()
                            );
                        }
                        try {
                            code = vm_exec(ts, code, result);
                        } catch (...) {
                            for (std::size_t j = 0; j < callargs; ++j) {
                                pop_alias(ts, args[offset + j].get_ident(cs));
                            }
                            ts.idstack.resize(idstsz);
                            throw;
                        }
                        for (std::size_t j = 0; j < callargs; ++j) {
                            pop_alias(ts, args[offset + j].get_ident(cs));
                        }
                        ts.idstack.resize(idstsz);
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
                        args.resize(offset - 1);
                        goto use_result;
                    }
                    case ID_ALIAS: {
                        alias *a = static_cast<alias *>(&id->get());
                        if (a->is_arg() && !ident_is_used_arg(a, ts)) {
                            args.resize(offset - 1);
                            goto use_result;
                        }
                        auto &ast = ts.get_astack(a);
                        if (ast.node->val_s.type() == value_type::NONE) {
                            goto noid;
                        }
                        result = exec_alias(
                            ts, a, &args[offset], callargs, ast
                        );
                        args.resize(offset - 1);
                        goto use_result;
                    }
                }
            }

            case BC_INST_COM: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->lookup_ident(op >> 8)
                );
                std::size_t offset = args.size() - id->arg_count();
                result.force_none();
                id->call_id(ts, span_type<any_value>{
                    &args[offset], std::size_t(id->arg_count())
                }, result);
                args.resize(offset);
                goto use_result;
            }

            case BC_INST_COM_V: {
                command_impl *id = static_cast<command_impl *>(
                    ts.istate->lookup_ident(op >> 8)
                );
                std::size_t callargs = *code++;
                std::size_t offset = args.size() - callargs;
                result.force_none();
                id->call_id(
                    ts, span_type<any_value>{&args[offset], callargs}, result
                );
                args.resize(offset);
                goto use_result;
            }
        }
        continue;
use_result:
        force_val(cs, result, op);
        continue;
use_top:
        force_val(cs, args.back(), op);
        continue;
use_exit:
        force_val(cs, result, op);
        break;
    }
    return code;
}

} /* namespace cubescript */
