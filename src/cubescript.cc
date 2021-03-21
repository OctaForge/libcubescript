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

cs_ident_impl::cs_ident_impl(cs_ident_type tp, cs_strref nm, int fl):
    p_name(nm), p_type(int(tp)), p_flags(fl)
{}

cs_var_impl::cs_var_impl(cs_ident_type tp, cs_strref name, cs_var_cb f, int fl):
    cs_ident_impl(tp, name, fl), cb_var(std::move(f))
{}

cs_ivar_impl::cs_ivar_impl(
    cs_strref name, cs_int m, cs_int x, cs_int v, cs_var_cb f, int fl
):
    cs_var_impl(cs_ident_type::IVAR, name, std::move(f), fl | ((m > x) ? CS_IDF_READONLY : 0)),
    p_storage(v), p_minval(m), p_maxval(x), p_overrideval(0)
{}

cs_fvar_impl::cs_fvar_impl(
    cs_strref name, cs_float m, cs_float x, cs_float v, cs_var_cb f, int fl
):
    cs_var_impl(cs_ident_type::FVAR, name, std::move(f), fl | ((m > x) ? CS_IDF_READONLY : 0)),
    p_storage(v), p_minval(m), p_maxval(x), p_overrideval(0)
{}

cs_svar_impl::cs_svar_impl(cs_strref name, cs_strref v, cs_strref ov, cs_var_cb f, int fl):
    cs_var_impl(cs_ident_type::SVAR, name, std::move(f), fl),
    p_storage{v}, p_overrideval{ov}
{}

cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_strref a, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val{cs}
{
    p_val.set_str(a);
}
cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, std::string_view a, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val{cs}
{
    p_val.set_str(a);
}
cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_int a, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val{cs}
{
    p_val.set_int(a);
}
cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_float a, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val{cs}
{
    p_val.set_float(a);
}
cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val{cs}
{
    p_val.set_none();
}
/* FIXME: use cs rather than val's cs */
cs_alias_impl::cs_alias_impl(cs_state &, cs_strref name, cs_value v, int fl):
    cs_ident_impl(cs_ident_type::ALIAS, name, fl),
    p_acode(nullptr), p_astack(nullptr), p_val(v)
{}

cs_command_impl::cs_command_impl(
    cs_strref name, cs_strref args, int nargs, cs_command_cb f
):
    cs_ident_impl(cs_ident_type::COMMAND, name, 0),
    p_cargs(args), p_cb_cftv(std::move(f)), p_numargs(nargs)
{}

bool cs_ident::is_alias() const {
    return get_type() == cs_ident_type::ALIAS;
}

cs_alias *cs_ident::get_alias() {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<cs_alias *>(this);
}

cs_alias const *cs_ident::get_alias() const {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<cs_alias const *>(this);
}

bool cs_ident::is_command() const {
    return get_type() == cs_ident_type::COMMAND;
}

cs_command *cs_ident::get_command() {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<cs_command_impl *>(this);
}

cs_command const *cs_ident::get_command() const {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<cs_command_impl const *>(this);
}

bool cs_ident::is_special() const {
    return get_type() == cs_ident_type::SPECIAL;
}

bool cs_ident::is_var() const {
    cs_ident_type tp = get_type();
    return (tp >= cs_ident_type::IVAR) && (tp <= cs_ident_type::SVAR);
}

cs_var *cs_ident::get_var() {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<cs_var *>(this);
}

cs_var const *cs_ident::get_var() const {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<cs_var const *>(this);
}

bool cs_ident::is_ivar() const {
    return get_type() == cs_ident_type::IVAR;
}

cs_ivar *cs_ident::get_ivar() {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<cs_ivar *>(this);
}

cs_ivar const *cs_ident::get_ivar() const {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<cs_ivar const *>(this);
}

bool cs_ident::is_fvar() const {
    return get_type() == cs_ident_type::FVAR;
}

cs_fvar *cs_ident::get_fvar() {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<cs_fvar *>(this);
}

cs_fvar const *cs_ident::get_fvar() const {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<cs_fvar const *>(this);
}

bool cs_ident::is_svar() const {
    return get_type() == cs_ident_type::SVAR;
}

cs_svar *cs_ident::get_svar() {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<cs_svar *>(this);
}

cs_svar const *cs_ident::get_svar() const {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<cs_svar const *>(this);
}

void cs_var_impl::changed(cs_state &cs) {
    if (cb_var) {
        switch (p_type) {
            case CsIdIvar:
                cb_var(cs, *static_cast<cs_ivar_impl *>(this));
                break;
            case CsIdFvar:
                cb_var(cs, *static_cast<cs_fvar_impl *>(this));
                break;
            case CsIdSvar:
                cb_var(cs, *static_cast<cs_svar_impl *>(this));
                break;
            default:
                break;
        }
    }
}

cs_int cs_ivar::get_val_min() const {
    return static_cast<cs_ivar_impl const *>(this)->p_minval;
}
cs_int cs_ivar::get_val_max() const {
    return static_cast<cs_ivar_impl const *>(this)->p_maxval;
}

cs_int cs_ivar::get_value() const {
    return static_cast<cs_ivar_impl const *>(this)->p_storage;
}
void cs_ivar::set_value(cs_int val) {
    static_cast<cs_ivar_impl *>(this)->p_storage = val;
}

cs_float cs_fvar::get_val_min() const {
    return static_cast<cs_fvar_impl const *>(this)->p_minval;
}
cs_float cs_fvar::get_val_max() const {
    return static_cast<cs_fvar_impl const *>(this)->p_maxval;
}

cs_float cs_fvar::get_value() const {
    return static_cast<cs_fvar_impl const *>(this)->p_storage;
}
void cs_fvar::set_value(cs_float val) {
    static_cast<cs_fvar_impl *>(this)->p_storage = val;
}

cs_strref cs_svar::get_value() const {
    return static_cast<cs_svar_impl const *>(this)->p_storage;
}
void cs_svar::set_value(cs_strref val) {
    static_cast<cs_svar_impl *>(this)->p_storage = val;
}

std::string_view cs_command::get_args() const {
    return static_cast<cs_command_impl const *>(this)->p_cargs;
}

int cs_command::get_num_args() const {
    return static_cast<cs_command_impl const *>(this)->p_numargs;
}

void cs_init_lib_base(cs_state &cs);

static void *cs_default_alloc(void *, void *p, size_t, size_t ns) {
    if (!ns) {
        std::free(p);
        return nullptr;
    }
    return std::realloc(p, ns);
}

cs_state::cs_state(): cs_state{cs_default_alloc, nullptr} {}

cs_state::cs_state(cs_alloc_cb func, void *data):
    p_state(nullptr), p_callhook()
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
    static_cast<cs_command_impl *>(p)->p_type = CsIdDo;

    p = new_command("doargs", "e", [](auto &cs, auto args, auto &res) {
        cs_do_args(cs, [&cs, &res, &args]() {
            cs.run(args[0].get_code(), res);
        });
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdDoArgs;

    p = new_command("if", "tee", [](auto &cs, auto args, auto &res) {
        cs.run((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdIf;

    p = new_command("result", "t", [](auto &, auto args, auto &res) {
        res = std::move(args[0]);
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdResult;

    p = new_command("!", "t", [](auto &, auto args, auto &res) {
        res.set_int(!args[0].get_bool());
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdNot;

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
    static_cast<cs_command_impl *>(p)->p_type = CsIdAnd;

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
    static_cast<cs_command_impl *>(p)->p_type = CsIdOr;

    p = new_command("local", "", nullptr);
    static_cast<cs_command_impl *>(p)->p_type = CsIdLocal;

    p = new_command("break", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsBreakException();
        } else {
            throw cs_error(cs, "no loop to break");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdBreak;

    p = new_command("continue", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsContinueException();
        } else {
            throw cs_error(cs, "no loop to continue");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = CsIdContinue;

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
    p_state->destroy(p_state->strman);
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

LIBCUBESCRIPT_EXPORT cs_vprint_cb cs_state::set_var_printer(cs_vprint_cb func) {
    auto fn = std::move(p_state->varprintf);
    p_state->varprintf = std::move(func);
    return fn;
}

LIBCUBESCRIPT_EXPORT cs_vprint_cb const &cs_state::get_var_printer() const {
    return p_state->varprintf;
}

void *cs_state::alloc(void *ptr, size_t os, size_t ns) {
    return p_state->alloc(ptr, os, ns);
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
            *this, cs_strref{*p_state, name}, flags
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
        cs_strref{*p_state, n}, m, x, v, std::move(f), flags
    );
    add_ident(iv, iv);
    return iv;
}

LIBCUBESCRIPT_EXPORT cs_fvar *cs_state::new_fvar(
    std::string_view n, cs_float m, cs_float x, cs_float v, cs_var_cb f, int flags
) {
    auto *fv = p_state->create<cs_fvar_impl>(
        cs_strref{*p_state, n}, m, x, v, std::move(f), flags
    );
    add_ident(fv, fv);
    return fv;
}

LIBCUBESCRIPT_EXPORT cs_svar *cs_state::new_svar(
    std::string_view n, std::string_view v, cs_var_cb f, int flags
) {
    auto *sv = p_state->create<cs_svar_impl>(
        cs_strref{*p_state, n}, cs_strref{*p_state, v},
        cs_strref{*p_state, ""}, std::move(f), flags
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
            *this, cs_strref{*p_state, name}, std::move(v), identflags
        );
        add_ident(a, a);
    }
}

LIBCUBESCRIPT_EXPORT void cs_state::print_var(cs_var const &v) const {
    p_state->varprintf(*this, v);
}

LIBCUBESCRIPT_EXPORT cs_value cs_alias::get_value() const {
    return static_cast<cs_alias_impl const *>(this)->p_val;
}

void cs_alias::get_cval(cs_value &v) const {
    auto *imp = static_cast<cs_alias_impl const *>(this);
    switch (imp->p_val.get_type()) {
        case cs_value_type::STRING:
            v = imp->p_val;
            break;
        case cs_value_type::INT:
            v.set_int(imp->p_val.get_int());
            break;
        case cs_value_type::FLOAT:
            v.set_float(imp->p_val.get_float());
            break;
        default:
            v.set_none();
            break;
    }
}

int cs_ident::get_raw_type() const {
    return p_impl->p_type;
}

cs_ident_type cs_ident::get_type() const {
    if (p_impl->p_type > CsIdAlias) {
        return cs_ident_type::SPECIAL;
    }
    return cs_ident_type(p_impl->p_type);
}

std::string_view cs_ident::get_name() const {
    return p_impl->p_name;
}

int cs_ident::get_flags() const {
    return p_impl->p_flags;
}

int cs_ident::get_index() const {
    return p_impl->p_index;
}

template<typename SF>
static inline void cs_override_var(cs_state &cs, cs_var *v, int &vflags, SF sf) {
    if ((cs.identflags & CS_IDF_OVERRIDDEN) || (vflags & CS_IDF_OVERRIDE)) {
        if (vflags & CS_IDF_PERSIST) {
            throw cs_error(
                cs, "cannot override persistent variable '%s'",
                v->get_name().data()
            );
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
    sv->set_value(cs_strref{*p_state, v});
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
    return cs_strref{*p_state, static_cast<cs_svar *>(id)->get_value()};
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
    if ((a->get_index() < MaxArguments) && !cs_is_arg_used(*this, a)) {
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
    throw cs_error(
        cs,
        (iv->get_flags() & CS_IDF_HEX)
            ? (
                (iv->get_val_min() <= 255)
                    ? "valid range for '%s' is %d..0x%X"
                    : "valid range for '%s' is 0x%X..0x%X"
            )
            : "valid range for '%s' is %d..%d",
        iv->get_name().data(), iv->get_val_min(), iv->get_val_max()
    );
}

LIBCUBESCRIPT_EXPORT void cs_state::set_var_int_checked(cs_ivar *iv, cs_int v) {
    if (iv->get_flags() & CS_IDF_READONLY) {
        throw cs_error(
            *this, "variable '%s' is read only", iv->get_name().data()
        );
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

LIBCUBESCRIPT_EXPORT void cs_state::set_var_float_checked(cs_fvar *fv, cs_float v) {
    if (fv->get_flags() & CS_IDF_READONLY) {
        throw cs_error(
            *this, "variable '%s' is read only", fv->get_name().data()
        );
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
        throw cs_error(
            *this, "variable '%s' is read only", sv->get_name().data()
        );
    }
    cs_svar_impl *svp = static_cast<cs_svar_impl *>(sv);
    cs_override_var(
        *this, sv, svp->p_flags,
        [&svp]() { svp->p_overrideval = svp->p_storage; }
    );
    sv->set_value(cs_strref{*p_state, v});
    svp->changed(*this);
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
        cs_strref{*p_state, name}, cs_strref{*p_state, args}, nargs,
        std::move(func)
    );
    add_ident(cmd, cmd);
    return cmd;
}

static inline void cs_do_loop(
    cs_state &cs, cs_ident &id, cs_int offset, cs_int n, cs_int step,
    cs_bcode *cond, cs_bcode *body
) {
    cs_stacked_value idv{cs, &id};
    if (n <= 0 || !idv.has_alias()) {
        return;
    }
    for (cs_int i = 0; i < n; ++i) {
        idv.set_int(offset + i * step);
        idv.push();
        if (cond && !cs.run(cond).get_bool()) {
            break;
        }
        switch (cs.run_loop(body)) {
            case cs_loop_state::BREAK:
                goto end;
            default: /* continue and normal */
                break;
        }
    }
end:
    return;
}

static inline void cs_loop_conc(
    cs_state &cs, cs_value &res, cs_ident &id, cs_int offset, cs_int n,
    cs_int step, cs_bcode *body, bool space
) {
    cs_stacked_value idv{cs, &id};
    if (n <= 0 || !idv.has_alias()) {
        return;
    }
    cs_charbuf s{cs};
    for (cs_int i = 0; i < n; ++i) {
        idv.set_int(offset + i * step);
        idv.push();
        cs_value v{cs};
        switch (cs.run_loop(body, v)) {
            case cs_loop_state::BREAK:
                goto end;
            case cs_loop_state::CONTINUE:
                continue;
            default:
                break;
        }
        if (space && i) {
            s.push_back(' ');
        }
        s.append(v.get_str());
    }
end:
    res.set_str(s.str());
}

void cs_init_lib_base(cs_state &gcs) {
    gcs.new_command("error", "s", [](auto &cs, auto args, auto &) {
        throw cs_error(cs, args[0].get_str());
    });

    gcs.new_command("pcall", "err", [](auto &cs, auto args, auto &ret) {
        cs_alias *cret = args[1].get_ident()->get_alias(),
                *css  = args[2].get_ident()->get_alias();
        if (!cret || !css) {
            ret.set_int(0);
            return;
        }
        cs_value result{cs}, tback{cs};
        bool rc = true;
        try {
            cs.run(args[0].get_code(), result);
        } catch (cs_error const &e) {
            result.set_str(e.what());
            if (e.get_stack().get()) {
                cs_charbuf buf{cs};
                util::print_stack(std::back_inserter(buf), e.get_stack());
                tback.set_str(buf.str());
            }
            rc = false;
        }
        ret.set_int(rc);
        static_cast<cs_alias_impl *>(cret)->set_alias(cs, result);
        static_cast<cs_alias_impl *>(css)->set_alias(cs, tback);
    });

    gcs.new_command("?", "ttt", [](auto &, auto args, auto &res) {
        if (args[0].get_bool()) {
            res = args[1];
        } else {
            res = args[2];
        }
    });

    gcs.new_command("cond", "ee2V", [](auto &cs, auto args, auto &res) {
        for (size_t i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run(args[i].get_code()).get_bool()) {
                    cs.run(args[i + 1].get_code(), res);
                    break;
                }
            } else {
                cs.run(args[i].get_code(), res);
                break;
            }
        }
    });

    gcs.new_command("case", "ite2V", [](auto &cs, auto args, auto &res) {
        cs_int val = args[0].get_int();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_int() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("casef", "fte2V", [](auto &cs, auto args, auto &res) {
        cs_float val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("cases", "ste2V", [](auto &cs, auto args, auto &res) {
        cs_strref val = args[0].get_str();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_str() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("pushif", "rte", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias() || (idv.get_alias()->get_index() < MaxArguments)) {
            return;
        }
        if (args[1].get_bool()) {
            idv = args[1];
            idv.push();
            cs.run(args[2].get_code(), res);
        }
    });

    gcs.new_command("loop", "rie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    gcs.new_command("loop+", "riie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop*", "riie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop+*", "riiie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    gcs.new_command("loopwhile", "riee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    gcs.new_command("loopwhile+", "riiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile*", "riiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile+*", "riiiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    gcs.new_command("while", "ee", [](auto &cs, auto args, auto &) {
        cs_bcode *cond = args[0].get_code(), *body = args[1].get_code();
        while (cs.run(cond).get_bool()) {
            switch (cs.run_loop(body)) {
                case cs_loop_state::BREAK:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    gcs.new_command("loopconcat", "rie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    gcs.new_command("loopconcat+", "riie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat*", "riie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat+*", "riiie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[3].get_int(), args[2].get_int(), args[4].get_code(), true
        );
    });

    gcs.new_command("loopconcatword", "rie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword*", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+*", "riiie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });

    gcs.new_command("push", "rte", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias() || (idv.get_alias()->get_index() < MaxArguments)) {
            return;
        }
        idv = args[1];
        idv.push();
        cs.run(args[2].get_code(), res);
    });

    gcs.new_command("resetvar", "s", [](auto &cs, auto args, auto &) {
        cs.reset_var(args[0].get_str());
    });

    gcs.new_command("alias", "st", [](auto &cs, auto args, auto &) {
        cs.set_alias(args[0].get_str(), args[1]);
    });

    gcs.new_command("getvarmin", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.get_var_min_int(args[0].get_str()).value_or(0));
    });
    gcs.new_command("getvarmax", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.get_var_max_int(args[0].get_str()).value_or(0));
    });
    gcs.new_command("getfvarmin", "s", [](auto &cs, auto args, auto &res) {
        res.set_float(cs.get_var_min_float(args[0].get_str()).value_or(0.0f));
    });
    gcs.new_command("getfvarmax", "s", [](auto &cs, auto args, auto &res) {
        res.set_float(cs.get_var_max_float(args[0].get_str()).value_or(0.0f));
    });

    gcs.new_command("identexists", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.have_ident(args[0].get_str()));
    });

    gcs.new_command("getalias", "s", [](auto &cs, auto args, auto &res) {
        auto s0 = cs.get_alias_val(args[0].get_str());
        if (s0) {
            res.set_str(*s0);
        } else {
            res.set_str("");
        }
    });
}

void cs_init_lib_math(cs_state &cs);
void cs_init_lib_string(cs_state &cs);
void cs_init_lib_list(cs_state &cs);

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
