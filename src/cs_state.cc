#include <memory>
#include <cstdio>
#include <cmath>

#include "cs_bcode.hh"
#include "cs_state.hh"
#include "cs_thread.hh"
#include "cs_strman.hh"
#include "cs_vm.hh" // break/continue, call_with_args
#include "cs_parser.hh"

namespace cubescript {

internal_state::internal_state(alloc_func af, void *data):
    allocf{af}, aptr{data},
    idents{allocator_type{this}},
    identmap{allocator_type{this}},
    strman{create<string_pool>(this)},
    empty{bcode_init_empty(this)}
{}

internal_state::~internal_state() {
    for (auto &p: idents) {
        destroy(&ident_p{*p.second}.impl());
    }
    bcode_free_empty(this, empty);
    destroy(strman);
}

void *internal_state::alloc(void *ptr, size_t os, size_t ns) {
    void *p = allocf(aptr, ptr, os, ns);
    if (!p && ns) {
        throw std::bad_alloc{};
    }
    return p;
}

static void *default_alloc(void *, void *p, size_t, size_t ns) {
    if (!ns) {
        std::free(p);
        return nullptr;
    }
    return std::realloc(p, ns);
}

ident *internal_state::add_ident(ident *id, ident_impl *impl) {
    if (!id) {
        return nullptr;
    }
    ident_p{*id}.impl(impl);
    idents[id->get_name()] = id;
    impl->p_index = int(identmap.size());
    identmap.push_back(id);
    return identmap.back();
}

ident &internal_state::new_ident(state &cs, std::string_view name, int flags) {
    ident *id = get_ident(name);
    if (!id) {
        if (!is_valid_name(name)) {
            throw error{
                cs, "'%s' is not a valid identifier name", name.data()
            };
        }
        auto *inst = create<alias_impl>(
            cs, string_ref{cs, name}, flags
        );
        id = add_ident(inst, inst);
    }
    return *id;
}

ident *internal_state::get_ident(std::string_view name) const {
    auto id = idents.find(name);
    if (id == idents.end()) {
        return nullptr;
    }
    return id->second;
}

void init_lib_base(state &cs);
void init_lib_math(state &cs);
void init_lib_string(state &cs);
void init_lib_list(state &cs);

/* public interfaces */

state::state(): state{default_alloc, nullptr} {}

state::state(alloc_func func, void *data) {
    command *p;

    if (!func) {
        func = default_alloc;
    }
    /* allocator is not set up yet, use func directly */
    auto *statep = static_cast<internal_state *>(
        func(data, nullptr, 0, sizeof(internal_state))
    );
    /* allocator will be set up in the constructor */
    new (statep) internal_state{func, data};

    try {
        p_tstate = statep->create<thread_state>(statep);
    } catch (...) {
        statep->destroy(statep);
        throw;
    }

    p_tstate->pstate = this;
    p_tstate->istate = statep;
    p_tstate->owner = true;

    for (std::size_t i = 0; i < MAX_ARGUMENTS; ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "arg%zu", i + 1);
        statep->new_ident(
            *this, static_cast<char const *>(buf), IDENT_FLAG_ARG
        );
    }

    statep->id_dummy = &statep->new_ident(*this, "//dummy", IDENT_FLAG_UNKNOWN);

    statep->ivar_numargs  = &new_ivar("numargs", 0, true);
    statep->ivar_dbgalias = &new_ivar("dbgalias", 4);

    /* default handlers for variables */

    statep->cmd_ivar = &new_command("//ivar_builtin", "$iN", [](
        auto &cs, auto args, auto &
    ) {
        auto *iv = args[0].get_ident()->get_ivar();
        if (args[2].get_integer() <= 1) {
            std::printf("%s = ", iv->get_name().data());
            std::printf(INTEGER_FORMAT, iv->get_value());
            std::printf("\n");
        } else {
            iv->set_value(cs, args[1].get_integer());
        }
    });

    statep->cmd_fvar = &new_command("//fvar_builtin", "$fN", [](
        auto &cs, auto args, auto &
    ) {
        auto *fv = args[0].get_ident()->get_fvar();
        if (args[2].get_integer() <= 1) {
            auto val = fv->get_value();
            std::printf("%s = ", fv->get_name().data());
            if (std::floor(val) == val) {
                std::printf(ROUND_FLOAT_FORMAT, val);
            } else {
                std::printf(FLOAT_FORMAT, val);
            }
            std::printf("\n");
        } else {
            fv->set_value(cs, args[1].get_float());
        }
    });

    statep->cmd_svar = &new_command("//svar_builtin", "$sN", [](
        auto &cs, auto args, auto &
    ) {
        auto *sv = args[0].get_ident()->get_svar();
        if (args[2].get_integer() <= 1) {
            auto val = sv->get_value();
            if (val.view().find('"') == std::string_view::npos) {
                std::printf("%s = \"%s\"\n", sv->get_name().data(), val.data());
            } else {
                std::printf("%s = [%s]\n", sv->get_name().data(), val.data());
            }
        } else {
            sv->set_value(cs, args[1].get_string(cs));
        }
    });

    statep->cmd_var_changed = nullptr;

    /* builtins */

    p = &new_command("do", "e", [](auto &cs, auto args, auto &res) {
        res = cs.run(args[0].get_code());
    });
    static_cast<command_impl *>(p)->p_type = ID_DO;

    p = &new_command("doargs", "e", [](auto &cs, auto args, auto &res) {
        call_with_args(*cs.p_tstate, [&cs, &res, &args]() {
            res = cs.run(args[0].get_code());
        });
    });
    static_cast<command_impl *>(p)->p_type = ID_DOARGS;

    p = &new_command("if", "tee", [](auto &cs, auto args, auto &res) {
        res = cs.run((args[0].get_bool() ? args[1] : args[2]).get_code());
    });
    static_cast<command_impl *>(p)->p_type = ID_IF;

    p = &new_command("result", "t", [](auto &, auto args, auto &res) {
        res = std::move(args[0]);
    });
    static_cast<command_impl *>(p)->p_type = ID_RESULT;

    p = &new_command("!", "t", [](auto &, auto args, auto &res) {
        res.set_integer(!args[0].get_bool());
    });
    static_cast<command_impl *>(p)->p_type = ID_NOT;

    p = &new_command("&&", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_integer(1);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                auto code = args[i].get_code();
                if (code) {
                    res = cs.run(code);
                } else {
                    res = std::move(args[i]);
                }
                if (!res.get_bool()) {
                    break;
                }
            }
        }
    });
    static_cast<command_impl *>(p)->p_type = ID_AND;

    p = &new_command("||", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_integer(0);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                auto code = args[i].get_code();
                if (code) {
                    res = cs.run(code);
                } else {
                    res = std::move(args[i]);
                }
                if (res.get_bool()) {
                    break;
                }
            }
        }
    });
    static_cast<command_impl *>(p)->p_type = ID_OR;

    p = &new_command("local", "", nullptr);
    static_cast<command_impl *>(p)->p_type = ID_LOCAL;

    p = &new_command("break", "", [](auto &cs, auto, auto &) {
        if (cs.p_tstate->loop_level) {
            throw break_exception{};
        } else {
            throw error{cs, "no loop to break"};
        }
    });
    static_cast<command_impl *>(p)->p_type = ID_BREAK;

    p = &new_command("continue", "", [](auto &cs, auto, auto &) {
        if (cs.p_tstate->loop_level) {
            throw continue_exception{};
        } else {
            throw error{cs, "no loop to continue"};
        }
    });
    static_cast<command_impl *>(p)->p_type = ID_CONTINUE;

    init_lib_base(*this);
}

LIBCUBESCRIPT_EXPORT state::~state() {
    destroy();
}

LIBCUBESCRIPT_EXPORT void state::destroy() {
    if (!p_tstate || !p_tstate->owner) {
        return;
    }
    auto *sp = p_tstate->istate;
    sp->destroy(p_tstate);
    sp->destroy(sp);
    p_tstate = nullptr;
}

state::state(void *is) {
    auto *s = static_cast<internal_state *>(is);
    p_tstate = s->create<thread_state>(s);
    p_tstate->pstate = this;
    p_tstate->istate = s;
    p_tstate->owner = false;
}

LIBCUBESCRIPT_EXPORT state state::new_thread() {
    return state{p_tstate->istate};
}

LIBCUBESCRIPT_EXPORT hook_func state::set_call_hook(hook_func func) {
    return p_tstate->set_hook(std::move(func));
}

LIBCUBESCRIPT_EXPORT hook_func const &state::get_call_hook() const {
    return p_tstate->get_hook();
}

LIBCUBESCRIPT_EXPORT hook_func &state::get_call_hook() {
    return p_tstate->get_hook();
}

LIBCUBESCRIPT_EXPORT void *state::alloc(void *ptr, size_t os, size_t ns) {
    return p_tstate->istate->alloc(ptr, os, ns);
}

LIBCUBESCRIPT_EXPORT ident *state::get_ident(std::string_view name) {
    return p_tstate->istate->get_ident(name);
}

LIBCUBESCRIPT_EXPORT alias *state::get_alias(std::string_view name) {
    auto id = get_ident(name);
    if (!id || !id->is_alias()) {
        return nullptr;
    }
    return static_cast<alias *>(id);
}

LIBCUBESCRIPT_EXPORT bool state::have_ident(std::string_view name) {
    return p_tstate->istate->idents.find(name) != p_tstate->istate->idents.end();
}

LIBCUBESCRIPT_EXPORT std::span<ident *> state::get_idents() {
    return std::span<ident *>{
        p_tstate->istate->identmap.data(),
        p_tstate->istate->identmap.size()
    };
}

LIBCUBESCRIPT_EXPORT std::span<ident const *> state::get_idents() const {
    auto ptr = const_cast<ident const **>(p_tstate->istate->identmap.data());
    return std::span<ident const *>{ptr, p_tstate->istate->identmap.size()};
}

LIBCUBESCRIPT_EXPORT void state::clear_override(ident &id) {
    if (!id.is_overridden(*this)) {
        return;
    }
    switch (id.get_type()) {
        case ident_type::ALIAS: {
            auto &ast = p_tstate->get_astack(static_cast<alias *>(&id));
            ast.node->val_s.set_string("", *this);
            ast.node->code = bcode_ref{};
            ast.flags &= ~IDENT_FLAG_OVERRIDDEN;
            return;
        }
        case ident_type::IVAR: {
            ivar_impl &iv = static_cast<ivar_impl &>(id);
            iv.set_raw_value(iv.p_override);
            var_changed(*p_tstate, &id);
            static_cast<ivar_impl *>(
                static_cast<integer_var *>(&iv)
            )->p_flags &= ~IDENT_FLAG_OVERRIDDEN;
            return;
        }
        case ident_type::FVAR: {
            fvar_impl &fv = static_cast<fvar_impl &>(id);
            fv.set_raw_value(fv.p_override);
            var_changed(*p_tstate, &id);
            static_cast<fvar_impl *>(
                static_cast<float_var *>(&fv)
            )->p_flags &= ~IDENT_FLAG_OVERRIDDEN;
            return;
        }
        case ident_type::SVAR: {
            svar_impl &sv = static_cast<svar_impl &>(id);
            sv.set_raw_value(sv.p_override);
            var_changed(*p_tstate, &id);
            static_cast<svar_impl *>(
                static_cast<string_var *>(&sv)
            )->p_flags &= ~IDENT_FLAG_OVERRIDDEN;
            return;
        }
        default:
            break;
    }
}

LIBCUBESCRIPT_EXPORT void state::clear_overrides() {
    for (auto &p: p_tstate->istate->idents) {
        clear_override(*(p.second));
    }
}

inline int var_flags(bool read_only, var_type vtp) {
    int ret = 0;
    if (read_only) {
        ret |= IDENT_FLAG_READONLY;
    }
    switch (vtp) {
        case var_type::PERSISTENT:
            ret |= IDENT_FLAG_PERSIST;
            break;
        case var_type::OVERRIDABLE:
            ret |= IDENT_FLAG_OVERRIDE;
            break;
        default:
            break;
    }
    return ret;
}

static void var_name_check(
    state &cs, ident *id, std::string_view n
) {
    if (id) {
        throw error{
            cs, "redefinition of ident '%.*s'", int(n.size()), n.data()
        };
    } else if (!is_valid_name(n)) {
        throw error{
            cs, "'%.*s' is not a valid variable name",
            int(n.size()), n.data()
        };
    }
}

LIBCUBESCRIPT_EXPORT integer_var &state::new_ivar(
    std::string_view n, integer_type v, bool read_only, var_type vtp
) {
    auto *iv = p_tstate->istate->create<ivar_impl>(
        string_ref{*this, n}, v, var_flags(read_only, vtp)
    );
    try {
        var_name_check(*this, p_tstate->istate->get_ident(n), n);
    } catch (...) {
        p_tstate->istate->destroy(iv);
        throw;
    }
    p_tstate->istate->add_ident(iv, iv);
    return *iv;
}

LIBCUBESCRIPT_EXPORT float_var &state::new_fvar(
    std::string_view n, float_type v, bool read_only, var_type vtp
) {
    auto *fv = p_tstate->istate->create<fvar_impl>(
        string_ref{*this, n}, v, var_flags(read_only, vtp)
    );
    try {
        var_name_check(*this, p_tstate->istate->get_ident(n), n);
    } catch (...) {
        p_tstate->istate->destroy(fv);
        throw;
    }
    p_tstate->istate->add_ident(fv, fv);
    return *fv;
}

LIBCUBESCRIPT_EXPORT string_var &state::new_svar(
    std::string_view n, std::string_view v, bool read_only, var_type vtp
) {
    auto *sv = p_tstate->istate->create<svar_impl>(
        string_ref{*this, n}, string_ref{*this, v}, var_flags(read_only, vtp)
    );
    try {
        var_name_check(*this, p_tstate->istate->get_ident(n), n);
    } catch (...) {
        p_tstate->istate->destroy(sv);
        throw;
    }
    p_tstate->istate->add_ident(sv, sv);
    return *sv;
}

LIBCUBESCRIPT_EXPORT ident &state::new_ident(std::string_view n) {
    return p_tstate->istate->new_ident(*this, n, IDENT_FLAG_UNKNOWN);
}

LIBCUBESCRIPT_EXPORT void state::reset_var(std::string_view name) {
    ident *id = get_ident(name);
    if (!id) {
        throw error{*this, "variable '%s' does not exist", name.data()};
    }
    auto *var = id->get_var();
    if (var && var->is_read_only()) {
        throw error{*this, "variable '%s' is read only", name.data()};
    }
    clear_override(*id);
}

LIBCUBESCRIPT_EXPORT void state::touch_var(std::string_view name) {
    ident *id = get_ident(name);
    if (id && id->is_var()) {
        var_changed(*p_tstate, id);
    }
}

LIBCUBESCRIPT_EXPORT void state::set_alias(
    std::string_view name, any_value v
) {
    ident *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case ident_type::ALIAS: {
                static_cast<alias *>(id)->set_value(*this, std::move(v));
                return;
            }
            case ident_type::IVAR:
            case ident_type::FVAR:
            case ident_type::SVAR:
                run(*id, std::span<any_value>{&v, 1});
                break;
            default:
                throw error{
                    *this, "cannot redefine builtin %s with an alias",
                    id->get_name().data()
                };
        }
    } else if (!is_valid_name(name)) {
        throw error{*this, "cannot alias invalid name '%s'", name.data()};
    } else {
        auto *a = p_tstate->istate->create<alias_impl>(
            *this, string_ref{*this, name}, std::move(v),
            p_tstate->ident_flags
        );
        p_tstate->istate->add_ident(a, a);
    }
}

static char const *allowed_builtins[] = {
    "//ivar", "//fvar", "//svar", "//var_changed",
    "//ivar_builtin", "//fvar_builtin", "//svar_builtin",
    nullptr
};

LIBCUBESCRIPT_EXPORT command &state::new_command(
    std::string_view name, std::string_view args, command_func func
) {
    int nargs = 0;
    for (auto fmt = args.begin(); fmt != args.end(); ++fmt) {
        switch (*fmt) {
            case 'i':
            case 'b':
            case 'f':
            case 'F':
            case 't':
            case 'E':
            case 'N':
            case 's':
            case 'e':
            case 'r':
            case '$':
                ++nargs;
                break;
            case '1':
            case '2':
            case '3':
            case '4': {
                int nrep = (*fmt - '0');
                if (nargs < nrep) {
                    throw error{
                        *this, "not enough arguments to repeat"
                    };
                }
                if ((args.end() - fmt) != 2) {
                    throw error{
                        *this, "malformed argument list"
                    };
                }
                if ((fmt[1] != 'C') && (fmt[1] != 'V')) {
                    throw error{
                        *this, "repetition without variadic arguments"
                    };
                }
                nargs -= nrep;
                break;
            }
            case 'C':
            case 'V':
                if ((fmt + 1) != args.end()) {
                    throw error{
                        *this, "unterminated variadic argument list"
                    };
                }
                break;
            default:
                throw error{
                    *this, "invalid argument type: %c", *fmt
                };
        }
    }
    auto &is = *p_tstate->istate;
    auto *cmd = is.create<command_impl>(
        string_ref{*this, name}, string_ref{*this, args},
        nargs, std::move(func)
    );
    /* we can set these builtins */
    command **bptrs[] = {
        &is.cmd_ivar, &is.cmd_fvar, &is.cmd_svar, &is.cmd_var_changed
    };
    auto nbptrs = sizeof(bptrs) / sizeof(*bptrs);
    /* provided a builtin */
    if ((name.size() >= 2) && (name[0] == '/') && (name[1] == '/')) {
        /* sanitize */
        for (auto **p = allowed_builtins; *p; ++p) {
            if (!name.compare(*p)) {
                /* if it's one of the settable ones, maybe set it */
                if (std::size_t(p - allowed_builtins) < nbptrs) {
                    if (!is.get_ident(name)) {
                        /* only set if it does not exist already */
                        *bptrs[p - allowed_builtins] = cmd;
                        goto do_add;
                    }
                }
                /* this will ensure we're not redefining them */
                goto valid;
            }
        }
        /* we haven't found one matching the list, so error */
        is.destroy(cmd);
        throw error{
            *this, "forbidden builtin command: %.*s",
            int(name.size()), name.data()
        };
    }
valid:
    if (is.get_ident(name)) {
        is.destroy(cmd);
        throw error{
            *this, "redefinition of ident '%.*s'",
            int(name.size()), name.data()
        };
    }
do_add:
    is.add_ident(cmd, cmd);
    return *cmd;
}

LIBCUBESCRIPT_EXPORT void state::init_libs(int libs) {
    if (libs & LIB_MATH) {
        init_lib_math(*this);
    }
    if (libs & LIB_STRING) {
        init_lib_string(*this);
    }
    if (libs & LIB_LIST) {
        init_lib_list(*this);
    }
}

LIBCUBESCRIPT_EXPORT any_value state::run(bcode_ref const &code) {
    any_value ret{};
    vm_exec(*p_tstate, bcode_p{code}.get()->get_raw(), ret);
    return ret;
}

static any_value do_run(
    thread_state &ts, std::string_view file, std::string_view code
) {
    any_value ret{};
    gen_state gs{ts};
    gs.gen_main(code, file);
    auto cref = gs.steal_ref();
    vm_exec(ts, bcode_p{cref}.get()->get_raw(), ret);
    return ret;
}

LIBCUBESCRIPT_EXPORT any_value state::run(std::string_view code) {
    return do_run(*p_tstate, std::string_view{}, code);
}

LIBCUBESCRIPT_EXPORT any_value state::run(
    std::string_view code, std::string_view source
) {
    return do_run(*p_tstate, source, code);
}

LIBCUBESCRIPT_EXPORT any_value state::run(
    ident &id, std::span<any_value> args
) {
    any_value ret{};
    std::size_t nargs = args.size();
    run_depth_guard level{*p_tstate}; /* incr and decr on scope exit */
    switch (id.get_type()) {
        default:
            if (!ident_is_callable(&id)) {
                break;
            }
        /* fallthrough */
        case ident_type::COMMAND: {
            auto &cimpl = static_cast<command_impl &>(id);
            if (nargs < std::size_t(cimpl.get_num_args())) {
                stack_guard s{*p_tstate}; /* restore after call */
                auto &targs = p_tstate->vmstack;
                auto osz = targs.size();
                targs.resize(osz + cimpl.get_num_args());
                for (std::size_t i = 0; i < nargs; ++i) {
                    targs[osz + i] = args[i];
                }
                exec_command(
                    *p_tstate, &cimpl, &id, &targs[osz], ret, nargs, false
                );
            } else {
                exec_command(
                    *p_tstate, &cimpl, &id, &args[0], ret, nargs, false
                );
            }
            nargs = 0;
            break;
        }
        case ident_type::IVAR: {
            auto *hid = p_tstate->istate->cmd_ivar;
            auto *cimp = static_cast<command_impl *>(hid);
            auto &targs = p_tstate->vmstack;
            auto osz = targs.size();
            auto anargs = std::size_t(cimp->get_num_args());
            targs.resize(
                osz + std::max(args.size(), anargs + 1)
            );
            for (std::size_t i = 0; i < nargs; ++i) {
                targs[osz + i + 1] = args[i];
            }
            exec_command(
                *p_tstate, cimp, &id, &targs[osz], ret, nargs + 1, false
            );
            break;
        }
        case ident_type::FVAR: {
            auto *hid = p_tstate->istate->cmd_fvar;
            auto *cimp = static_cast<command_impl *>(hid);
            auto &targs = p_tstate->vmstack;
            auto osz = targs.size();
            auto anargs = std::size_t(cimp->get_num_args());
            targs.resize(
                osz + std::max(args.size(), anargs + 1)
            );
            for (std::size_t i = 0; i < nargs; ++i) {
                targs[osz + i + 1] = args[i];
            }
            exec_command(
                *p_tstate, cimp, &id, &targs[osz], ret, nargs + 1, false
            );
            break;
        }
        case ident_type::SVAR: {
            auto *hid = p_tstate->istate->cmd_svar;
            auto *cimp = static_cast<command_impl *>(hid);
            auto &targs = p_tstate->vmstack;
            auto osz = targs.size();
            auto anargs = std::size_t(cimp->get_num_args());
            targs.resize(
                osz + std::max(args.size(), anargs + 1)
            );
            for (std::size_t i = 0; i < nargs; ++i) {
                targs[osz + i + 1] = args[i];
            }
            exec_command(
                *p_tstate, cimp, &id, &targs[osz], ret, nargs + 1, false
            );
            break;
        }
        case ident_type::ALIAS: {
            alias &a = static_cast<alias &>(id);
            if (a.is_arg() && !ident_is_used_arg(&a, *p_tstate)) {
                break;
            }
            exec_alias(
                *p_tstate, &a, &args[0], ret, nargs, nargs, 0, 0,
                BC_RET_NULL, true
            );
            break;
        }
    }
    return ret;
}

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(
    bcode_ref const &code, any_value &ret
) {
    ++p_tstate->loop_level;
    try {
        ret = run(code);
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

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(bcode_ref const &code) {
    any_value ret{};
    return run_loop(code, ret);
}

LIBCUBESCRIPT_EXPORT bool state::get_override_mode() const {
    return (p_tstate->ident_flags & IDENT_FLAG_OVERRIDDEN);
}

LIBCUBESCRIPT_EXPORT bool state::set_override_mode(bool v) {
    bool was = get_override_mode();
    if (v) {
        p_tstate->ident_flags |= IDENT_FLAG_OVERRIDDEN;
    } else {
        p_tstate->ident_flags &= ~IDENT_FLAG_OVERRIDDEN;
    }
    return was;
}

LIBCUBESCRIPT_EXPORT bool state::get_persist_mode() const {
    return (p_tstate->ident_flags & IDENT_FLAG_PERSIST);
}

LIBCUBESCRIPT_EXPORT bool state::set_persist_mode(bool v) {
    bool was = get_persist_mode();
    if (v) {
        p_tstate->ident_flags |= IDENT_FLAG_PERSIST;
    } else {
        p_tstate->ident_flags &= ~IDENT_FLAG_PERSIST;
    }
    return was;
}

LIBCUBESCRIPT_EXPORT std::size_t state::get_max_run_depth() const {
    return p_tstate->max_run_depth;
}

LIBCUBESCRIPT_EXPORT std::size_t state::set_max_run_depth(std::size_t v) {
    auto old = p_tstate->max_run_depth;
    p_tstate->max_run_depth = v;
    return old;
}

} /* namespace cubescript */
