#include <memory>

#include "cs_bcode.hh"
#include "cs_state.hh"
#include "cs_strman.hh"
#include "cs_vm.hh" // FIXME, only Max Arguments

namespace cscript {

cs_shared_state::cs_shared_state(cs_alloc_cb af, void *data):
    allocf{af}, aptr{data},
    idents{allocator_type{this}},
    identmap{allocator_type{this}},
    varprintf{},
    strman{create<cs_strman>(this)},
    empty{bcode_init_empty(this)}
{}

cs_shared_state::~cs_shared_state() {
     bcode_free_empty(this, empty);
     destroy(strman);
}

void *cs_shared_state::alloc(void *ptr, size_t os, size_t ns) {
    void *p = allocf(aptr, ptr, os, ns);
    if (!p && ns) {
        throw std::bad_alloc{};
    }
    return p;
}

static void *cs_default_alloc(void *, void *p, size_t, size_t ns) {
    if (!ns) {
        std::free(p);
        return nullptr;
    }
    return std::realloc(p, ns);
}

void cs_init_lib_base(cs_state &cs);
void cs_init_lib_math(cs_state &cs);
void cs_init_lib_string(cs_state &cs);
void cs_init_lib_list(cs_state &cs);

/* public interfaces */

cs_state::cs_state(): cs_state{cs_default_alloc, nullptr} {}

cs_state::cs_state(cs_alloc_cb func, void *data):
    p_state{nullptr}, p_callhook{}
{
    cs_command *p;

    if (!func) {
        func = cs_default_alloc;
    }
    /* allocator is not set up yet, use func directly */
    p_state = static_cast<cs_shared_state *>(
        func(data, nullptr, 0, sizeof(cs_shared_state))
    );
    /* allocator will be set up in the constructor */
    new (p_state) cs_shared_state{func, data};
    p_owner = true;

    /* will be used as message storage for errors */
    p_errbuf = p_state->create<cs_charbuf>(*this);

    for (int i = 0; i < MaxArguments; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident(static_cast<char const *>(buf), CS_IDF_ARG);
    }

    cs_ident *id = new_ident("//dummy");
    if (id->get_index() != DummyIdx) {
        throw cs_internal_error{"invalid dummy index"};
    }

    id = new_ivar("numargs", MaxArguments, 0, 0);
    if (id->get_index() != NumargsIdx) {
        throw cs_internal_error{"invalid numargs index"};
    }

    id = new_ivar("dbgalias", 0, 1000, 4);
    if (id->get_index() != DbgaliasIdx) {
        throw cs_internal_error{"invalid dbgalias index"};
    }

    p = new_command("do", "e", [](auto &cs, auto args, auto &res) {
        cs.run(args[0].get_code(), res);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_DO;

    p = new_command("doargs", "e", [](auto &cs, auto args, auto &res) {
        cs_do_args(cs, [&cs, &res, &args]() {
            cs.run(args[0].get_code(), res);
        });
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_DOARGS;

    p = new_command("if", "tee", [](auto &cs, auto args, auto &res) {
        cs.run((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_IF;

    p = new_command("result", "t", [](auto &, auto args, auto &res) {
        res = std::move(args[0]);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_RESULT;

    p = new_command("!", "t", [](auto &, auto args, auto &res) {
        res.set_int(!args[0].get_bool());
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_NOT;

    p = new_command("&&", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(1);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                cs_bcode *code = args[i].get_code();
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
    static_cast<cs_command_impl *>(p)->p_type = ID_AND;

    p = new_command("||", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(0);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                cs_bcode *code = args[i].get_code();
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
    static_cast<cs_command_impl *>(p)->p_type = ID_OR;

    p = new_command("local", "", nullptr);
    static_cast<cs_command_impl *>(p)->p_type = ID_LOCAL;

    p = new_command("break", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsBreakException();
        } else {
            throw cs_error(cs, "no loop to break");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_BREAK;

    p = new_command("continue", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsContinueException();
        } else {
            throw cs_error(cs, "no loop to continue");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_CONTINUE;

    cs_init_lib_base(*this);
}

LIBCUBESCRIPT_EXPORT cs_state::~cs_state() {
    destroy();
}

LIBCUBESCRIPT_EXPORT void cs_state::destroy() {
    if (!p_state || !p_owner) {
        return;
    }
    for (auto &p: p_state->idents) {
        cs_ident *i = p.second;
        cs_alias *a = i->get_alias();
        if (a) {
            a->get_value().force_none();
            static_cast<cs_alias_impl *>(a)->clean_code();
        }
        p_state->destroy(i->p_impl);
    }
    p_state->destroy(static_cast<cs_charbuf *>(p_errbuf));
    p_state->destroy(p_state);
}

cs_state::cs_state(cs_shared_state *s):
    p_state(s), p_owner(false)
{}

LIBCUBESCRIPT_EXPORT cs_state cs_state::new_thread() {
    return cs_state{p_state};
}

LIBCUBESCRIPT_EXPORT cs_hook_cb cs_state::set_call_hook(cs_hook_cb func) {
    auto hk = std::move(p_callhook);
    p_callhook = std::move(func);
    return hk;
}

LIBCUBESCRIPT_EXPORT cs_hook_cb const &cs_state::get_call_hook() const {
    return p_callhook;
}

LIBCUBESCRIPT_EXPORT cs_hook_cb &cs_state::get_call_hook() {
    return p_callhook;
}

LIBCUBESCRIPT_EXPORT cs_vprint_cb cs_state::set_var_printer(
    cs_vprint_cb func
) {
    auto fn = std::move(p_state->varprintf);
    p_state->varprintf = std::move(func);
    return fn;
}

LIBCUBESCRIPT_EXPORT cs_vprint_cb const &cs_state::get_var_printer() const {
    return p_state->varprintf;
}

LIBCUBESCRIPT_EXPORT void *cs_state::alloc(void *ptr, size_t os, size_t ns) {
    return p_state->alloc(ptr, os, ns);
}

template<typename SF>
inline void cs_override_var(cs_state &cs, cs_var *v, int &vflags, SF sf) {
    if ((cs.identflags & CS_IDF_OVERRIDDEN) || (vflags & CS_IDF_OVERRIDE)) {
        if (vflags & CS_IDF_PERSIST) {
            throw cs_error{
                cs, "cannot override persistent variable '%s'",
                v->get_name().data()
            };
        }
        if (!(vflags & CS_IDF_OVERRIDDEN)) {
            sf();
            vflags |= CS_IDF_OVERRIDDEN;
        }
    } else {
        if (vflags & CS_IDF_OVERRIDDEN) {
            vflags &= ~CS_IDF_OVERRIDDEN;
        }
    }
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_int(
    std::string_view name, cs_int v, bool dofunc, bool doclamp
) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return;
    }
    cs_ivar_impl *iv = static_cast<cs_ivar_impl *>(id);
    cs_override_var(
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

LIBCUBESCRIPT_EXPORT void cs_state::set_var_float(
    std::string_view name, cs_float v, bool dofunc, bool doclamp
) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return;
    }
    cs_fvar_impl *fv = static_cast<cs_fvar_impl *>(id);
    cs_override_var(
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

LIBCUBESCRIPT_EXPORT void cs_state::set_var_str(
    std::string_view name, std::string_view v, bool dofunc
) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return;
    }
    cs_svar_impl *sv = static_cast<cs_svar_impl *>(id);
    cs_override_var(
        *this, sv, sv->p_flags,
        [&sv]() { sv->p_overrideval = sv->get_value(); }
    );
    sv->set_value(cs_strref{p_state, v});
    if (dofunc) {
        sv->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT std::optional<cs_int>
cs_state::get_var_int(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<cs_ivar *>(id)->get_value();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_float>
cs_state::get_var_float(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<cs_fvar *>(id)->get_value();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_strref>
cs_state::get_var_str(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_svar()) {
        return std::nullopt;
    }
    return cs_strref{p_state, static_cast<cs_svar *>(id)->get_value()};
}

LIBCUBESCRIPT_EXPORT std::optional<cs_int>
cs_state::get_var_min_int(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<cs_ivar *>(id)->get_val_min();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_int>
cs_state::get_var_max_int(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_ivar()) {
        return std::nullopt;
    }
    return static_cast<cs_ivar *>(id)->get_val_max();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_float>
cs_state::get_var_min_float(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<cs_fvar *>(id)->get_val_min();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_float>
cs_state::get_var_max_float(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id || id->is_fvar()) {
        return std::nullopt;
    }
    return static_cast<cs_fvar *>(id)->get_val_max();
}

LIBCUBESCRIPT_EXPORT std::optional<cs_strref>
cs_state::get_alias_val(std::string_view name) {
    cs_alias *a = get_alias(name);
    if (!a) {
        return std::nullopt;
    }
    if ((a->get_index() < MaxArguments) && !ident_is_used_arg(a, *this)) {
        return std::nullopt;
    }
    return a->get_value().get_str();
}

cs_int cs_clamp_var(cs_state &cs, cs_ivar *iv, cs_int v) {
    if (v < iv->get_val_min()) {
        v = iv->get_val_min();
    } else if (v > iv->get_val_max()) {
        v = iv->get_val_max();
    } else {
        return v;
    }
    throw cs_error{
        cs,
        (iv->get_flags() & CS_IDF_HEX)
            ? (
                (iv->get_val_min() <= 255)
                    ? "valid range for '%s' is %d..0x%X"
                    : "valid range for '%s' is 0x%X..0x%X"
            )
            : "valid range for '%s' is %d..%d",
        iv->get_name().data(), iv->get_val_min(), iv->get_val_max()
    };
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_int_checked(cs_ivar *iv, cs_int v) {
    if (iv->get_flags() & CS_IDF_READONLY) {
        throw cs_error{
            *this, "variable '%s' is read only", iv->get_name().data()
        };
    }
    cs_ivar_impl *ivp = static_cast<cs_ivar_impl *>(iv);
    cs_override_var(
        *this, iv, ivp->p_flags,
        [&ivp]() { ivp->p_overrideval = ivp->p_storage; }
    );
    if ((v < iv->get_val_min()) || (v > iv->get_val_max())) {
        v = cs_clamp_var(*this, iv, v);
    }
    iv->set_value(v);
    ivp->changed(*this);
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_int_checked(
    cs_ivar *iv, std::span<cs_value> args
) {
    cs_int v = args[0].force_int();
    if ((iv->get_flags() & CS_IDF_HEX) && (args.size() > 1)) {
        v = (v << 16) | (args[1].force_int() << 8);
        if (args.size() > 2) {
            v |= args[2].force_int();
        }
    }
    set_var_int_checked(iv, v);
}

cs_float cs_clamp_fvar(cs_state &cs, cs_fvar *fv, cs_float v) {
    if (v < fv->get_val_min()) {
        v = fv->get_val_min();
    } else if (v > fv->get_val_max()) {
        v = fv->get_val_max();
    } else {
        return v;
    }
    cs_value vmin{cs}, vmax{cs};
    vmin.set_float(fv->get_val_min());
    vmax.set_float(fv->get_val_max());
    throw cs_error(
        cs, "valid range for '%s' is %s..%s", fv->get_name().data(),
        vmin.force_str(), vmax.force_str()
    );
    return v;
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_float_checked(
    cs_fvar *fv, cs_float v
) {
    if (fv->get_flags() & CS_IDF_READONLY) {
        throw cs_error{
            *this, "variable '%s' is read only", fv->get_name().data()
        };
    }
    cs_fvar_impl *fvp = static_cast<cs_fvar_impl *>(fv);
    cs_override_var(
        *this, fv, fvp->p_flags,
        [&fvp]() { fvp->p_overrideval = fvp->p_storage; }
    );
    if ((v < fv->get_val_min()) || (v > fv->get_val_max())) {
        v = cs_clamp_fvar(*this, fv, v);
    }
    fv->set_value(v);
    fvp->changed(*this);
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_str_checked(
    cs_svar *sv, std::string_view v
) {
    if (sv->get_flags() & CS_IDF_READONLY) {
        throw cs_error{
            *this, "variable '%s' is read only", sv->get_name().data()
        };
    }
    cs_svar_impl *svp = static_cast<cs_svar_impl *>(sv);
    cs_override_var(
        *this, sv, svp->p_flags,
        [&svp]() { svp->p_overrideval = svp->p_storage; }
    );
    sv->set_value(cs_strref{p_state, v});
    svp->changed(*this);
}

LIBCUBESCRIPT_EXPORT void cs_state::init_libs(int libs) {
    if (libs & CS_LIB_MATH) {
        cs_init_lib_math(*this);
    }
    if (libs & CS_LIB_STRING) {
        cs_init_lib_string(*this);
    }
    if (libs & CS_LIB_LIST) {
        cs_init_lib_list(*this);
    }
}

} /* namespace cscript */
