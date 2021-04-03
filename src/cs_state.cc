#include <memory>

#include "cs_bcode.hh"
#include "cs_state.hh"
#include "cs_thread.hh"
#include "cs_strman.hh"
#include "cs_gen.hh"
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
        destroy(p.second->p_impl);
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
    id->p_impl = impl;
    idents[id->get_name()] = id;
    impl->p_index = int(identmap.size());
    identmap.push_back(id);
    return identmap.back();
}

ident *internal_state::new_ident(state &cs, std::string_view name, int flags) {
    ident *id = get_ident(name);
    if (!id) {
        if (!is_valid_name(name)) {
            throw error{
                cs, "number %s is not a valid identifier name", name.data()
            };
        }
        auto *inst = create<alias_impl>(
            cs, string_ref{this, name}, flags
        );
        id = add_ident(inst, inst);
    }
    return id;
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

    ident *id = statep->new_ident(*this, "//dummy", IDENT_FLAG_UNKNOWN);
    if (id->get_index() != ID_IDX_DUMMY) {
        throw internal_error{"invalid dummy index"};
    }

    id = new_ivar("numargs", 0, true);
    if (id->get_index() != ID_IDX_NUMARGS) {
        throw internal_error{"invalid numargs index"};
    }

    id = new_ivar("dbgalias", 4);
    if (id->get_index() != ID_IDX_DBGALIAS) {
        throw internal_error{"invalid dbgalias index"};
    }

    p = new_command("do", "e", [](auto &cs, auto args, auto &res) {
        cs.run(args[0].get_code(), res);
    });
    static_cast<command_impl *>(p)->p_type = ID_DO;

    p = new_command("doargs", "e", [](auto &cs, auto args, auto &res) {
        call_with_args(*cs.thread_pointer(), [&cs, &res, &args]() {
            cs.run(args[0].get_code(), res);
        });
    });
    static_cast<command_impl *>(p)->p_type = ID_DOARGS;

    p = new_command("if", "tee", [](auto &cs, auto args, auto &res) {
        cs.run((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    });
    static_cast<command_impl *>(p)->p_type = ID_IF;

    p = new_command("result", "t", [](auto &, auto args, auto &res) {
        res = std::move(args[0]);
    });
    static_cast<command_impl *>(p)->p_type = ID_RESULT;

    p = new_command("!", "t", [](auto &, auto args, auto &res) {
        res.set_int(!args[0].get_bool());
    });
    static_cast<command_impl *>(p)->p_type = ID_NOT;

    p = new_command("&&", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(1);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                auto code = args[i].get_code();
                if (code) {
                    cs.run(code, res);
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

    p = new_command("||", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(0);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                auto code = args[i].get_code();
                if (code) {
                    cs.run(code, res);
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

    p = new_command("local", "", nullptr);
    static_cast<command_impl *>(p)->p_type = ID_LOCAL;

    p = new_command("break", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw break_exception{};
        } else {
            throw error{cs, "no loop to break"};
        }
    });
    static_cast<command_impl *>(p)->p_type = ID_BREAK;

    p = new_command("continue", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
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
}

state::state(internal_state *s) {
    p_tstate = s->create<thread_state>(s);
    p_tstate->istate = s;
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

LIBCUBESCRIPT_EXPORT integer_var *state::new_ivar(
    std::string_view n, integer_type v, bool read_only
) {
    auto *iv = p_tstate->istate->create<ivar_impl>(
        string_ref{p_tstate->istate, n}, v,
        read_only ? IDENT_FLAG_READONLY : 0
    );
    p_tstate->istate->add_ident(iv, iv);
    return iv;
}

LIBCUBESCRIPT_EXPORT float_var *state::new_fvar(
    std::string_view n, float_type v, bool read_only
) {
    auto *fv = p_tstate->istate->create<fvar_impl>(
        string_ref{p_tstate->istate, n}, v,
        read_only ? IDENT_FLAG_READONLY : 0
    );
    p_tstate->istate->add_ident(fv, fv);
    return fv;
}

LIBCUBESCRIPT_EXPORT string_var *state::new_svar(
    std::string_view n, std::string_view v, bool read_only
) {
    auto *sv = p_tstate->istate->create<svar_impl>(
        string_ref{p_tstate->istate, n}, string_ref{p_tstate->istate, v},
        read_only ? IDENT_FLAG_READONLY : 0
    );
    p_tstate->istate->add_ident(sv, sv);
    return sv;
}

LIBCUBESCRIPT_EXPORT void state::set_alias(
    std::string_view name, any_value v
) {
    ident *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case ident_type::ALIAS: {
                alias *a = static_cast<alias *>(id);
                auto &ast = p_tstate->get_astack(a);
                if (a->is_arg()) {
                    ast.set_arg(a, *p_tstate, v);
                } else {
                    ast.set_alias(a, *p_tstate, v);
                }
                return;
            }
            case ident_type::IVAR:
            case ident_type::FVAR:
            case ident_type::SVAR: {
                any_value ret{*this};
                run(id, std::span<any_value>{&v, 1}, ret);
                break;
            }
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
            *this, string_ref{p_tstate->istate, name}, std::move(v), 0
        );
        p_tstate->istate->add_ident(a, a);
    }
}

LIBCUBESCRIPT_EXPORT command *state::new_command(
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
                    return nullptr;
                }
                if ((args.end() - fmt) != 2) {
                    return nullptr;
                }
                if ((fmt[1] != 'C') && (fmt[1] != 'V')) {
                    return nullptr;
                }
                nargs -= nrep;
                break;
            }
            case 'C':
            case 'V':
                if ((fmt + 1) != args.end()) {
                    return nullptr;
                }
                break;
            default:
                return nullptr;
        }
    }
    auto *cmd = p_tstate->istate->create<command_impl>(
        string_ref{p_tstate->istate, name},
        string_ref{p_tstate->istate, args},
        nargs, std::move(func)
    );
    p_tstate->istate->add_ident(cmd, cmd);
    return cmd;
}

LIBCUBESCRIPT_EXPORT std::optional<string_ref>
state::get_alias_val(std::string_view name) {
    alias *a = get_alias(name);
    if (!a) {
        return std::nullopt;
    }
    if (a->is_arg() && !ident_is_used_arg(a, *p_tstate)) {
        return std::nullopt;
    }
    return p_tstate->get_astack(a).node->val_s.get_str();
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

LIBCUBESCRIPT_EXPORT void state::run(bcode_ref const &code, any_value &ret) {
    bcode *p = code;
    vm_exec(*p_tstate, reinterpret_cast<std::uint32_t *>(p), ret);
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
    bcode_ref cref{reinterpret_cast<bcode *>(cbuf + 1)};
    bcode *p = cref;
    vm_exec(ts, p->get_raw(), ret);
}

LIBCUBESCRIPT_EXPORT void state::run(std::string_view code, any_value &ret) {
    do_run(*p_tstate, std::string_view{}, code, ret);
}

LIBCUBESCRIPT_EXPORT void state::run(
    std::string_view code, any_value &ret, std::string_view source
) {
    do_run(*p_tstate, source, code, ret);
}

LIBCUBESCRIPT_EXPORT void state::run(
    ident *id, std::span<any_value> args, any_value &ret
) {
    std::size_t nargs = args.size();
    ret.set_none();
    run_depth_guard level{*p_tstate}; /* incr and decr on scope exit */
    if (id) {
        switch (id->get_type()) {
            default:
                if (!ident_is_callable(id)) {
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
                    exec_command(
                        *p_tstate, cimpl, id, &targs[osz], ret, nargs, false
                    );
                } else {
                    exec_command(
                        *p_tstate, cimpl, id, &args[0], ret, nargs, false
                    );
                }
                nargs = 0;
                break;
            }
            case ident_type::IVAR: {
                auto *hid = get_ident("//ivar");
                if (!hid || !hid->is_command()) {
                    throw error{*p_tstate, "invalid ivar handler"};
                }
                auto *cimp = static_cast<command_impl *>(hid);
                auto &targs = p_tstate->vmstack;
                auto osz = targs.size();
                auto anargs = std::size_t(cimp->get_num_args());
                targs.resize(
                    osz + std::max(args.size(), anargs + 1), any_value{*this}
                );
                for (std::size_t i = 0; i < nargs; ++i) {
                    targs[osz + i + 1] = args[i];
                }
                exec_command(
                    *p_tstate, cimp, id, &targs[osz], ret, nargs + 1, false
                );
                break;
            }
            case ident_type::FVAR: {
                auto *hid = get_ident("//fvar");
                if (!hid || !hid->is_command()) {
                    throw error{*p_tstate, "invalid fvar handler"};
                }
                auto *cimp = static_cast<command_impl *>(hid);
                auto &targs = p_tstate->vmstack;
                auto osz = targs.size();
                auto anargs = std::size_t(cimp->get_num_args());
                targs.resize(
                    osz + std::max(args.size(), anargs + 1), any_value{*this}
                );
                for (std::size_t i = 0; i < nargs; ++i) {
                    targs[osz + i + 1] = args[i];
                }
                exec_command(
                    *p_tstate, cimp, id, &targs[osz], ret, nargs + 1, false
                );
                break;
            }
            case ident_type::SVAR: {
                auto *hid = get_ident("//svar");
                if (!hid || !hid->is_command()) {
                    throw error{*p_tstate, "invalid svar handler"};
                }
                auto *cimp = static_cast<command_impl *>(hid);
                auto &targs = p_tstate->vmstack;
                auto osz = targs.size();
                auto anargs = std::size_t(cimp->get_num_args());
                targs.resize(
                    osz + std::max(args.size(), anargs + 1), any_value{*this}
                );
                for (std::size_t i = 0; i < nargs; ++i) {
                    targs[osz + i + 1] = args[i];
                }
                exec_command(
                    *p_tstate, cimp, id, &targs[osz], ret, nargs + 1, false
                );
                break;
            }
            case ident_type::ALIAS: {
                alias *a = static_cast<alias *>(id);
                if (a->is_arg() && !ident_is_used_arg(a, *p_tstate)) {
                    break;
                }
                exec_alias(
                    *p_tstate, a, &args[0], ret, nargs, nargs, 0, 0,
                    BC_RET_NULL, true
                );
                break;
            }
        }
    }
}

LIBCUBESCRIPT_EXPORT any_value state::run(bcode_ref const &code) {
    any_value ret{*this};
    run(code, ret);
    return ret;
}

LIBCUBESCRIPT_EXPORT any_value state::run(std::string_view code) {
    any_value ret{*this};
    run(code, ret);
    return ret;
}

LIBCUBESCRIPT_EXPORT any_value state::run(
    std::string_view code, std::string_view source
) {
    any_value ret{*this};
    run(code, ret, source);
    return ret;
}

LIBCUBESCRIPT_EXPORT any_value state::run(
    ident *id, std::span<any_value> args
) {
    any_value ret{*this};
    run(id, args, ret);
    return ret;
}

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(
    bcode_ref const &code, any_value &ret
) {
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

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(bcode_ref const &code) {
    any_value ret{*this};
    return run_loop(code, ret);
}

LIBCUBESCRIPT_EXPORT bool state::is_in_loop() const {
    return !!p_tstate->loop_level;
}

} /* namespace cubescript */
