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
    varprintf{},
    strman{create<string_pool>(this)},
    empty{bcode_init_empty(this)}
{}

internal_state::~internal_state() {
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

    for (int i = 0; i < MAX_ARGUMENTS; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident(static_cast<char const *>(buf), IDENT_FLAG_ARG);
    }

    ident *id = new_ident("//dummy");
    if (id->get_index() != ID_IDX_DUMMY) {
        throw internal_error{"invalid dummy index"};
    }

    id = new_ivar("numargs", MAX_ARGUMENTS, 0, 0);
    if (id->get_index() != ID_IDX_NUMARGS) {
        throw internal_error{"invalid numargs index"};
    }

    id = new_ivar("dbgalias", 0, 1000, 4);
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
                bcode *code = args[i].get_code();
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
                bcode *code = args[i].get_code();
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
    for (auto &p: sp->idents) {
        ident *i = p.second;
        alias *a = i->get_alias();
        if (a) {
            a->get_value().force_none();
            static_cast<alias_impl *>(a)->clean_code();
        }
        sp->destroy(i->p_impl);
    }
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

LIBCUBESCRIPT_EXPORT var_print_func state::set_var_printer(
    var_print_func func
) {
    auto fn = std::move(p_tstate->istate->varprintf);
    p_tstate->istate->varprintf = std::move(func);
    return fn;
}

LIBCUBESCRIPT_EXPORT var_print_func const &state::get_var_printer() const {
    return p_tstate->istate->varprintf;
}

LIBCUBESCRIPT_EXPORT void state::print_var(global_var const &v) const {
    if (p_tstate->istate->varprintf) {
        p_tstate->istate->varprintf(*this, v);
    }
}

LIBCUBESCRIPT_EXPORT void *state::alloc(void *ptr, size_t os, size_t ns) {
    return p_tstate->istate->alloc(ptr, os, ns);
}

LIBCUBESCRIPT_EXPORT ident *state::add_ident(
    ident *id, ident_impl *impl
) {
    if (!id) {
        return nullptr;
    }
    id->p_impl = impl;
    p_tstate->istate->idents[id->get_name()] = id;
    static_cast<ident_impl *>(impl)->p_index = p_tstate->istate->identmap.size();
    p_tstate->istate->identmap.push_back(id);
    return p_tstate->istate->identmap.back();
}

LIBCUBESCRIPT_EXPORT ident *state::new_ident(
    std::string_view name, int flags
) {
    ident *id = get_ident(name);
    if (!id) {
        if (!is_valid_name(name)) {
            throw error{
                *this, "number %s is not a valid identifier name", name.data()
            };
        }
        auto *inst = p_tstate->istate->create<alias_impl>(
            *this, string_ref{p_tstate->istate, name}, flags
        );
        id = add_ident(inst, inst);
    }
    return id;
}

LIBCUBESCRIPT_EXPORT ident *state::get_ident(std::string_view name) {
    auto id = p_tstate->istate->idents.find(name);
    if (id != p_tstate->istate->idents.end()) {
        return id->second;
    }
    return nullptr;
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
    std::string_view n, integer_type m, integer_type x, integer_type v,
    var_cb_func f, int flags
) {
    auto *iv = p_tstate->istate->create<ivar_impl>(
        string_ref{p_tstate->istate, n}, m, x, v, std::move(f), flags
    );
    add_ident(iv, iv);
    return iv;
}

LIBCUBESCRIPT_EXPORT float_var *state::new_fvar(
    std::string_view n, float_type m, float_type x, float_type v,
    var_cb_func f, int flags
) {
    auto *fv = p_tstate->istate->create<fvar_impl>(
        string_ref{p_tstate->istate, n}, m, x, v, std::move(f), flags
    );
    add_ident(fv, fv);
    return fv;
}

LIBCUBESCRIPT_EXPORT string_var *state::new_svar(
    std::string_view n, std::string_view v, var_cb_func f, int flags
) {
    auto *sv = p_tstate->istate->create<svar_impl>(
        string_ref{p_tstate->istate, n}, string_ref{p_tstate->istate, v},
        string_ref{p_tstate->istate, ""}, std::move(f), flags
    );
    add_ident(sv, sv);
    return sv;
}

LIBCUBESCRIPT_EXPORT void state::reset_var(std::string_view name) {
    ident *id = get_ident(name);
    if (!id) {
        throw error{*this, "variable %s does not exist", name.data()};
    }
    if (id->get_flags() & IDENT_FLAG_READONLY) {
        throw error{*this, "variable %s is read only", name.data()};
    }
    clear_override(*id);
}

LIBCUBESCRIPT_EXPORT void state::touch_var(std::string_view name) {
    ident *id = get_ident(name);
    if (id && id->is_var()) {
        static_cast<var_impl *>(id->p_impl)->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT void state::set_alias(
    std::string_view name, any_value v
) {
    ident *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case ident_type::ALIAS: {
                alias_impl *a = static_cast<alias_impl *>(id);
                if (a->get_flags() & IDENT_FLAG_ARG) {
                    a->set_arg(*p_tstate, v);
                } else {
                    a->set_alias(*p_tstate, v);
                }
                return;
            }
            case ident_type::IVAR:
                set_var_int_checked(static_cast<integer_var *>(id), v.get_int());
                break;
            case ident_type::FVAR:
                set_var_float_checked(static_cast<float_var *>(id), v.get_float());
                break;
            case ident_type::SVAR:
                set_var_str_checked(static_cast<string_var *>(id), v.get_str());
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
            *this, string_ref{p_tstate->istate, name}, std::move(v), identflags
        );
        add_ident(a, a);
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
                if (nargs < MAX_ARGUMENTS) {
                    ++nargs;
                }
                break;
            case '1':
            case '2':
            case '3':
            case '4':
                if (nargs < (*fmt - '0')) {
                    return nullptr;
                }
                if ((args.end() - fmt) != 2) {
                    return nullptr;
                }
                if ((fmt[1] != 'C') && (fmt[1] != 'V')) {
                    return nullptr;
                }
                if (nargs < MAX_ARGUMENTS) {
                    fmt -= *fmt - '0' + 1;
                }
                break;
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
    add_ident(cmd, cmd);
    return cmd;
}

LIBCUBESCRIPT_EXPORT void state::clear_override(ident &id) {
    if (!(id.get_flags() & IDENT_FLAG_OVERRIDDEN)) {
        return;
    }
    switch (id.get_type()) {
        case ident_type::ALIAS: {
            alias_impl &a = static_cast<alias_impl &>(id);
            a.clean_code();
            a.get_value().set_str("");
            break;
        }
        case ident_type::IVAR: {
            ivar_impl &iv = static_cast<ivar_impl &>(id);
            iv.set_value(iv.p_overrideval);
            iv.changed(*this);
            break;
        }
        case ident_type::FVAR: {
            fvar_impl &fv = static_cast<fvar_impl &>(id);
            fv.set_value(fv.p_overrideval);
            fv.changed(*this);
            break;
        }
        case ident_type::SVAR: {
            svar_impl &sv = static_cast<svar_impl &>(id);
            sv.set_value(sv.p_overrideval);
            sv.changed(*this);
            break;
        }
        default:
            break;
    }
    id.p_impl->p_flags &= ~IDENT_FLAG_OVERRIDDEN;
}

LIBCUBESCRIPT_EXPORT void state::clear_overrides() {
    for (auto &p: p_tstate->istate->idents) {
        clear_override(*(p.second));
    }
}

template<typename SF>
inline void override_var(state &cs, global_var *v, int &vflags, SF sf) {
    if ((cs.identflags & IDENT_FLAG_OVERRIDDEN) || (vflags & IDENT_FLAG_OVERRIDE)) {
        if (vflags & IDENT_FLAG_PERSIST) {
            throw error{
                cs, "cannot override persistent variable '%s'",
                v->get_name().data()
            };
        }
        if (!(vflags & IDENT_FLAG_OVERRIDDEN)) {
            sf();
            vflags |= IDENT_FLAG_OVERRIDDEN;
        }
    } else {
        if (vflags & IDENT_FLAG_OVERRIDDEN) {
            vflags &= ~IDENT_FLAG_OVERRIDDEN;
        }
    }
}

LIBCUBESCRIPT_EXPORT void state::set_var_int(
    std::string_view name, integer_type v, bool dofunc, bool doclamp
) {
    ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return;
    }
    ivar_impl *iv = static_cast<ivar_impl *>(id);
    override_var(
        *this, iv, iv->p_flags,
        [&iv]() { iv->p_overrideval = iv->get_value(); }
    );
    if (doclamp) {
        iv->set_value(std::clamp(v, iv->get_val_min(), iv->get_val_max()));
    } else {
        iv->set_value(v);
    }
    if (dofunc) {
        iv->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT void state::set_var_float(
    std::string_view name, float_type v, bool dofunc, bool doclamp
) {
    ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return;
    }
    fvar_impl *fv = static_cast<fvar_impl *>(id);
    override_var(
        *this, fv, fv->p_flags,
        [&fv]() { fv->p_overrideval = fv->get_value(); }
    );
    if (doclamp) {
        fv->set_value(std::clamp(v, fv->get_val_min(), fv->get_val_max()));
    } else {
        fv->set_value(v);
    }
    if (dofunc) {
        fv->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT void state::set_var_str(
    std::string_view name, std::string_view v, bool dofunc
) {
    ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return;
    }
    svar_impl *sv = static_cast<svar_impl *>(id);
    override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = sv->get_value(); }
    );
    sv->set_value(string_ref{p_tstate->istate, v});
    if (dofunc) {
        sv->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT std::optional<integer_type>
state::get_var_int(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<integer_var *>(id)->get_value();
}

LIBCUBESCRIPT_EXPORT std::optional<float_type>
state::get_var_float(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<float_var *>(id)->get_value();
}

LIBCUBESCRIPT_EXPORT std::optional<string_ref>
state::get_var_str(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return std::nullopt;
    }
    return string_ref{
        p_tstate->istate, static_cast<string_var *>(id)->get_value()
    };
}

LIBCUBESCRIPT_EXPORT std::optional<integer_type>
state::get_var_min_int(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<integer_var *>(id)->get_val_min();
}

LIBCUBESCRIPT_EXPORT std::optional<integer_type>
state::get_var_max_int(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<integer_var *>(id)->get_val_max();
}

LIBCUBESCRIPT_EXPORT std::optional<float_type>
state::get_var_min_float(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<float_var *>(id)->get_val_min();
}

LIBCUBESCRIPT_EXPORT std::optional<float_type>
state::get_var_max_float(std::string_view name) {
    ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<float_var *>(id)->get_val_max();
}

LIBCUBESCRIPT_EXPORT std::optional<string_ref>
state::get_alias_val(std::string_view name) {
    alias *a = get_alias(name);
    if (!a) {
        return std::nullopt;
    }
    if ((a->get_flags() & IDENT_FLAG_ARG) && !ident_is_used_arg(a, *p_tstate)) {
        return std::nullopt;
    }
    return a->get_value().get_str();
}

integer_type clamp_var(state &cs, integer_var *iv, integer_type v) {
    if (v < iv->get_val_min()) {
        v = iv->get_val_min();
    } else if (v > iv->get_val_max()) {
        v = iv->get_val_max();
    } else {
        return v;
    }
    throw error{
        cs,
        (iv->get_flags() & IDENT_FLAG_HEX)
            ? (
                (iv->get_val_min() <= 255)
                    ? "valid range for '%s' is %d..0x%X"
                    : "valid range for '%s' is 0x%X..0x%X"
            )
            : "valid range for '%s' is %d..%d",
        iv->get_name().data(), iv->get_val_min(), iv->get_val_max()
    };
}

LIBCUBESCRIPT_EXPORT void state::set_var_int_checked(
    integer_var *iv, integer_type v
) {
    if (iv->get_flags() & IDENT_FLAG_READONLY) {
        throw error{
            *this, "variable '%s' is read only", iv->get_name().data()
        };
    }
    ivar_impl *ivp = static_cast<ivar_impl *>(iv);
    override_var(
        *this, iv, ivp->p_flags,
        [&ivp]() { ivp->p_overrideval = ivp->p_storage; }
    );
    if ((v < iv->get_val_min()) || (v > iv->get_val_max())) {
        v = clamp_var(*this, iv, v);
    }
    iv->set_value(v);
    ivp->changed(*this);
}

LIBCUBESCRIPT_EXPORT void state::set_var_int_checked(
    integer_var *iv, std::span<any_value> args
) {
    integer_type v = args[0].force_int();
    if ((iv->get_flags() & IDENT_FLAG_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2) {
            v |= args[2].force_int();
        }
    }
    set_var_int_checked(iv, v);
}

float_type clamp_fvar(state &cs, float_var *fv, float_type v) {
    if (v < fv->get_val_min()) {
        v = fv->get_val_min();
    } else if (v > fv->get_val_max()) {
        v = fv->get_val_max();
    } else {
        return v;
    }
    any_value vmin{cs}, vmax{cs};
    vmin.set_float(fv->get_val_min());
    vmax.set_float(fv->get_val_max());
    throw error{
        cs, "valid range for '%s' is %s..%s", fv->get_name().data(),
        vmin.force_str(), vmax.force_str()
    };
    return v;
}

LIBCUBESCRIPT_EXPORT void state::set_var_float_checked(
    float_var *fv, float_type v
) {
    if (fv->get_flags() & IDENT_FLAG_READONLY) {
        throw error{
            *this, "variable '%s' is read only", fv->get_name().data()
        };
    }
    fvar_impl *fvp = static_cast<fvar_impl *>(fv);
    override_var(
        *this, fv, fvp->p_flags,
        [&fvp]() { fvp->p_overrideval = fvp->p_storage; }
    );
    if ((v < fv->get_val_min()) || (v > fv->get_val_max())) {
        v = clamp_fvar(*this, fv, v);
    }
    fv->set_value(v);
    fvp->changed(*this);
}

LIBCUBESCRIPT_EXPORT void state::set_var_str_checked(
    string_var *sv, std::string_view v
) {
    if (sv->get_flags() & IDENT_FLAG_READONLY) {
        throw error{
            *this, "variable '%s' is read only", sv->get_name().data()
        };
    }
    svar_impl *svp = static_cast<svar_impl *>(sv);
    override_var(
        *this, sv, svp->p_flags,
        [&svp]() { svp->p_overrideval = svp->p_storage; }
    );
    sv->set_value(string_ref{p_tstate->istate, v});
    svp->changed(*this);
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

LIBCUBESCRIPT_EXPORT void state::run(bcode *code, any_value &ret) {
    vm_exec(*p_tstate, reinterpret_cast<std::uint32_t *>(code), ret);
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
        vm_exec(ts, cbuf + 1, ret);
    }, [cbuf]() {
        bcode_decr(cbuf);
    });
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
                        *p_tstate, cimpl, &targs[osz], ret, nargs, false
                    );
                } else {
                    exec_command(
                        *p_tstate, cimpl, &args[0], ret, nargs, false
                    );
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
                    (a->get_flags() & IDENT_FLAG_ARG) &&
                    !ident_is_used_arg(a, *p_tstate)
                ) {
                    break;
                }
                if (a->get_value().get_type() == value_type::NONE) {
                    break;
                }
                exec_alias(
                    *p_tstate, a, &args[0], ret, nargs, nargs, 0, 0, BC_RET_NULL
                );
                break;
            }
        }
    }
}

LIBCUBESCRIPT_EXPORT any_value state::run(bcode *code) {
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

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(bcode *code, any_value &ret) {
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

LIBCUBESCRIPT_EXPORT loop_state state::run_loop(bcode *code) {
    any_value ret{*this};
    return run_loop(code, ret);
}

LIBCUBESCRIPT_EXPORT bool state::is_in_loop() const {
    return !!p_tstate->loop_level;
}

} /* namespace cubescript */
