#include <cubescript/cubescript.hh>
#include "cs_vm.hh"

#include <iterator>

namespace cscript {

bool cs_check_num(std::string_view s) {
    if (isdigit(s[0])) {
        return true;
    }
    switch (s[0]) {
        case '+':
        case '-':
            return isdigit(s[1]) || ((s[1] == '.') && isdigit(s[2]));
        case '.':
            return isdigit(s[1]) != 0;
        default:
            return false;
    }
}

LIBCUBESCRIPT_EXPORT void cs_state::clear_override(cs_ident &id) {
    if (!(id.get_flags() & CS_IDF_OVERRIDDEN)) {
        return;
    }
    switch (id.get_type()) {
        case cs_ident_type::ALIAS: {
            cs_alias_impl &a = static_cast<cs_alias_impl &>(id);
            a.clean_code();
            a.get_value().set_str("");
            break;
        }
        case cs_ident_type::IVAR: {
            cs_ivar_impl &iv = static_cast<cs_ivar_impl &>(id);
            iv.set_value(iv.p_overrideval);
            iv.changed(*this);
            break;
        }
        case cs_ident_type::FVAR: {
            cs_fvar_impl &fv = static_cast<cs_fvar_impl &>(id);
            fv.set_value(fv.p_overrideval);
            fv.changed(*this);
            break;
        }
        case cs_ident_type::SVAR: {
            cs_svar_impl &sv = static_cast<cs_svar_impl &>(id);
            sv.set_value(sv.p_overrideval);
            sv.changed(*this);
            break;
        }
        default:
            break;
    }
    id.p_impl->p_flags &= ~CS_IDF_OVERRIDDEN;
}

LIBCUBESCRIPT_EXPORT void cs_state::clear_overrides() {
    for (auto &p: p_state->idents) {
        clear_override(*(p.second));
    }
}

LIBCUBESCRIPT_EXPORT cs_ident *cs_state::add_ident(
    cs_ident *id, cs_ident_impl *impl
) {
    if (!id) {
        return nullptr;
    }
    id->p_impl = impl;
    p_state->idents[id->get_name()] = id;
    static_cast<cs_ident_impl *>(impl)->p_index = p_state->identmap.size();
    p_state->identmap.push_back(id);
    return p_state->identmap.back();
}

LIBCUBESCRIPT_EXPORT cs_ident *cs_state::new_ident(std::string_view name, int flags) {
    cs_ident *id = get_ident(name);
    if (!id) {
        if (cs_check_num(name)) {
            throw cs_error(
                *this, "number %s is not a valid identifier name", name.data()
            );
        }
        auto *inst = p_state->create<cs_alias_impl>(
            *this, cs_strref{p_state, name}, flags
        );
        id = add_ident(inst, inst);
    }
    return id;
}

LIBCUBESCRIPT_EXPORT cs_ident *cs_state::force_ident(cs_value &v) {
    switch (v.get_type()) {
        case cs_value_type::IDENT:
            return v.get_ident();
        case cs_value_type::STRING: {
            cs_ident *id = new_ident(v.get_str());
            v.set_ident(id);
            return id;
        }
        default:
            break;
    }
    v.set_ident(p_state->identmap[DummyIdx]);
    return p_state->identmap[DummyIdx];
}

LIBCUBESCRIPT_EXPORT cs_ident *cs_state::get_ident(std::string_view name) {
    auto id = p_state->idents.find(name);
    if (id != p_state->idents.end()) {
        return id->second;
    }
    return nullptr;
}

LIBCUBESCRIPT_EXPORT cs_alias *cs_state::get_alias(std::string_view name) {
    auto id = get_ident(name);
    if (!id || !id->is_alias()) {
        return nullptr;
    }
    return static_cast<cs_alias *>(id);
}

LIBCUBESCRIPT_EXPORT bool cs_state::have_ident(std::string_view name) {
    return p_state->idents.find(name) != p_state->idents.end();
}

LIBCUBESCRIPT_EXPORT std::span<cs_ident *> cs_state::get_idents() {
    return std::span<cs_ident *>{
        p_state->identmap.data(),
        p_state->identmap.size()
    };
}

LIBCUBESCRIPT_EXPORT std::span<cs_ident const *> cs_state::get_idents() const {
    auto ptr = const_cast<cs_ident const **>(p_state->identmap.data());
    return std::span<cs_ident const *>{ptr, p_state->identmap.size()};
}

LIBCUBESCRIPT_EXPORT cs_ivar *cs_state::new_ivar(
    std::string_view n, cs_int m, cs_int x, cs_int v, cs_var_cb f, int flags
) {
    auto *iv = p_state->create<cs_ivar_impl>(
        cs_strref{p_state, n}, m, x, v, std::move(f), flags
    );
    add_ident(iv, iv);
    return iv;
}

LIBCUBESCRIPT_EXPORT cs_fvar *cs_state::new_fvar(
    std::string_view n, cs_float m, cs_float x, cs_float v, cs_var_cb f, int flags
) {
    auto *fv = p_state->create<cs_fvar_impl>(
        cs_strref{p_state, n}, m, x, v, std::move(f), flags
    );
    add_ident(fv, fv);
    return fv;
}

LIBCUBESCRIPT_EXPORT cs_svar *cs_state::new_svar(
    std::string_view n, std::string_view v, cs_var_cb f, int flags
) {
    auto *sv = p_state->create<cs_svar_impl>(
        cs_strref{p_state, n}, cs_strref{p_state, v},
        cs_strref{p_state, ""}, std::move(f), flags
    );
    add_ident(sv, sv);
    return sv;
}

LIBCUBESCRIPT_EXPORT void cs_state::reset_var(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (!id) {
        throw cs_error(*this, "variable %s does not exist", name.data());
    }
    if (id->get_flags() & CS_IDF_READONLY) {
        throw cs_error(*this, "variable %s is read only", name.data());
    }
    clear_override(*id);
}

LIBCUBESCRIPT_EXPORT void cs_state::touch_var(std::string_view name) {
    cs_ident *id = get_ident(name);
    if (id && id->is_var()) {
        static_cast<cs_var_impl *>(id->p_impl)->changed(*this);
    }
}

LIBCUBESCRIPT_EXPORT void cs_state::set_alias(std::string_view name, cs_value v) {
    cs_ident *id = get_ident(name);
    if (id) {
        switch (id->get_type()) {
            case cs_ident_type::ALIAS: {
                cs_alias_impl *a = static_cast<cs_alias_impl *>(id);
                if (a->get_index() < MaxArguments) {
                    a->set_arg(*this, v);
                } else {
                    a->set_alias(*this, v);
                }
                return;
            }
            case cs_ident_type::IVAR:
                set_var_int_checked(static_cast<cs_ivar *>(id), v.get_int());
                break;
            case cs_ident_type::FVAR:
                set_var_float_checked(static_cast<cs_fvar *>(id), v.get_float());
                break;
            case cs_ident_type::SVAR:
                set_var_str_checked(static_cast<cs_svar *>(id), v.get_str());
                break;
            default:
                throw cs_error(
                    *this, "cannot redefine builtin %s with an alias",
                    id->get_name().data()
                );
        }
    } else if (cs_check_num(name)) {
        throw cs_error(*this, "cannot alias number %s", name.data());
    } else {
        auto *a = p_state->create<cs_alias_impl>(
            *this, cs_strref{p_state, name}, std::move(v), identflags
        );
        add_ident(a, a);
    }
}

LIBCUBESCRIPT_EXPORT void cs_state::print_var(cs_var const &v) const {
    if (p_state->varprintf) {
        p_state->varprintf(*this, v);
    }
}

LIBCUBESCRIPT_EXPORT cs_command *cs_state::new_command(
    std::string_view name, std::string_view args, cs_command_cb func
) {
    int nargs = 0;
    for (auto fmt = args.begin(); fmt != args.end(); ++fmt) {
        switch (*fmt) {
            case 'i':
            case 'b':
            case 'f':
            case 'F':
            case 't':
            case 'T':
            case 'E':
            case 'N':
            case 's':
            case 'e':
            case 'r':
            case '$':
                if (nargs < MaxArguments) {
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
                if (nargs < MaxArguments) {
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
    auto *cmd = p_state->create<cs_command_impl>(
        cs_strref{p_state, name}, cs_strref{p_state, args}, nargs,
        std::move(func)
    );
    add_ident(cmd, cmd);
    return cmd;
}

} /* namespace cscript */
