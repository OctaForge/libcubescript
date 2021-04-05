#include "cs_ident.hh"

#include "cs_bcode.hh"
#include "cs_gen.hh"
#include "cs_thread.hh"

namespace cubescript {

ident_impl::ident_impl(ident_type tp, string_ref nm, int fl):
    p_name{nm}, p_type{int(tp)}, p_flags{fl}
{}

bool ident_is_callable(ident const *id) {
    if (!id->is_command() && !id->is_special()) {
        return false;
    }
    return !!static_cast<command_impl const *>(id)->p_cb_cftv;
}

var_impl::var_impl(
    ident_type tp, string_ref name, int fl
):
    ident_impl{tp, name, fl}
{}

ivar_impl::ivar_impl(string_ref name, integer_type v, int fl):
    var_impl{ident_type::IVAR, name, fl}, p_storage{v}, p_override{v}
{}

fvar_impl::fvar_impl(string_ref name, float_type v, int fl):
    var_impl{ident_type::FVAR, name, fl}, p_storage{v}, p_override{v}
{}

svar_impl::svar_impl(string_ref name, string_ref v, int fl):
    var_impl{ident_type::SVAR, name, fl}, p_storage{v}, p_override{v}
{}

alias_impl::alias_impl(
    state &cs, string_ref name, string_ref a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s.set_string(a);
}

alias_impl::alias_impl(
    state &cs, string_ref name, std::string_view a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s.set_string(a);
}

alias_impl::alias_impl(state &cs, string_ref name, integer_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s.set_integer(a);
}

alias_impl::alias_impl(state &cs, string_ref name, float_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s.set_float(a);
}

alias_impl::alias_impl(state &cs, string_ref name, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s.set_none();
}

alias_impl::alias_impl(state &cs, string_ref name, any_value v, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{cs}
{
    p_initial.val_s = v.get_plain();
}

command_impl::command_impl(
    string_ref name, string_ref args, int nargs, command_func f
):
    ident_impl{ident_type::COMMAND, name, 0},
    p_cargs{args}, p_cb_cftv{std::move(f)}, p_numargs{nargs}
{}

void var_changed(thread_state &ts, ident *id) {
    auto *cid = ts.istate->cmd_var_changed;
    if (!cid) {
        return;
    }
    auto *cimp = static_cast<command_impl *>(cid);
    any_value val{*ts.pstate};
    val.set_ident(id);
    cimp->call(ts, std::span<any_value>{&val, 1}, val);
}

void ivar_impl::save_val() {
    p_override = p_storage;
}

void fvar_impl::save_val() {
    p_override = p_storage;
}

void svar_impl::save_val() {
    p_override = std::move(p_storage);
}

void command_impl::call(
    thread_state &ts, std::span<any_value> args, any_value &ret
) {
    auto idstsz = ts.idstack.size();
    try {
        p_cb_cftv(*ts.pstate, args, ret);
    } catch (...) {
        ts.idstack.resize(idstsz, ident_stack{*ts.pstate});
        throw;
    }
    ts.idstack.resize(idstsz, ident_stack{*ts.pstate});
}

bool ident_is_used_arg(ident *id, thread_state &ts) {
    if (!ts.callstack) {
        return true;
    }
    return ts.callstack->usedargs[id->get_index()];
}

void alias_stack::push(ident_stack &st) {
    st.next = node;
    node = &st;
}

void alias_stack::pop() {
    node = node->next;
}

void alias_stack::set_arg(alias *a, thread_state &ts, any_value &v) {
    if (ident_is_used_arg(a, ts)) {
        node->code = bcode_ref{};
    } else {
        push(ts.idstack.emplace_back(*ts.pstate));
        ts.callstack->usedargs[a->get_index()] = true;
    }
    node->val_s = std::move(v);
}

void alias_stack::set_alias(alias *a, thread_state &ts, any_value &v) {
    node->val_s = std::move(v);
    node->code = bcode_ref{};
    flags = ts.ident_flags;
    auto *imp = static_cast<alias_impl *>(a);
    if (node == &imp->p_initial) {
        imp->p_flags = flags;
    }
}

/* public interface */

LIBCUBESCRIPT_EXPORT int ident::get_raw_type() const {
    return p_impl->p_type;
}

LIBCUBESCRIPT_EXPORT ident_type ident::get_type() const {
    if (p_impl->p_type > ID_ALIAS) {
        return ident_type::SPECIAL;
    }
    return ident_type(p_impl->p_type);
}

LIBCUBESCRIPT_EXPORT std::string_view ident::get_name() const {
    return p_impl->p_name;
}

LIBCUBESCRIPT_EXPORT int ident::get_index() const {
    return p_impl->p_index;
}

LIBCUBESCRIPT_EXPORT bool ident::is_alias() const {
    return get_type() == ident_type::ALIAS;
}

LIBCUBESCRIPT_EXPORT alias *ident::get_alias() {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<alias *>(this);
}

LIBCUBESCRIPT_EXPORT alias const *ident::get_alias() const {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<alias const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_command() const {
    return get_type() == ident_type::COMMAND;
}

LIBCUBESCRIPT_EXPORT command *ident::get_command() {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<command_impl *>(this);
}

LIBCUBESCRIPT_EXPORT command const *ident::get_command() const {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<command_impl const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_special() const {
    return get_type() == ident_type::SPECIAL;
}

LIBCUBESCRIPT_EXPORT bool ident::is_var() const {
    switch (get_type()) {
        case ident_type::IVAR:
        case ident_type::FVAR:
        case ident_type::SVAR:
            return true;
        default:
            break;
    }
    return false;
}

LIBCUBESCRIPT_EXPORT global_var *ident::get_var() {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<global_var *>(this);
}

LIBCUBESCRIPT_EXPORT global_var const *ident::get_var() const {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<global_var const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_ivar() const {
    return get_type() == ident_type::IVAR;
}

LIBCUBESCRIPT_EXPORT integer_var *ident::get_ivar() {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<integer_var *>(this);
}

LIBCUBESCRIPT_EXPORT integer_var const *ident::get_ivar() const {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<integer_var const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_fvar() const {
    return get_type() == ident_type::FVAR;
}

LIBCUBESCRIPT_EXPORT float_var *ident::get_fvar() {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<float_var *>(this);
}

LIBCUBESCRIPT_EXPORT float_var const *ident::get_fvar() const {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<float_var const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_svar() const {
    return get_type() == ident_type::SVAR;
}

LIBCUBESCRIPT_EXPORT string_var *ident::get_svar() {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<string_var *>(this);
}

LIBCUBESCRIPT_EXPORT string_var const *ident::get_svar() const {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<string_var const *>(this);
}

LIBCUBESCRIPT_EXPORT bool ident::is_overridden(state &cs) const {
    switch (get_type()) {
        case ident_type::IVAR:
        case ident_type::FVAR:
        case ident_type::SVAR:
            return (p_impl->p_flags & IDENT_FLAG_OVERRIDDEN);
        case ident_type::ALIAS:
            return (state_p{cs}.ts().get_astack(
                static_cast<alias const *>(this)
            ).flags & IDENT_FLAG_OVERRIDDEN);
        default:
            break;
    }
    return false;
}

LIBCUBESCRIPT_EXPORT bool ident::is_persistent(state &cs) const {
    switch (get_type()) {
        case ident_type::IVAR:
        case ident_type::FVAR:
        case ident_type::SVAR:
            return (p_impl->p_flags & IDENT_FLAG_PERSIST);
        case ident_type::ALIAS:
            return (state_p{cs}.ts().get_astack(
                static_cast<alias const *>(this)
            ).flags & IDENT_FLAG_PERSIST);
        default:
            break;
    }
    return false;
}

LIBCUBESCRIPT_EXPORT bool global_var::is_read_only() const {
    return (p_impl->p_flags & IDENT_FLAG_READONLY);
}

LIBCUBESCRIPT_EXPORT bool global_var::is_overridable() const {
    return (p_impl->p_flags & IDENT_FLAG_OVERRIDE);
}

LIBCUBESCRIPT_EXPORT var_type global_var::get_variable_type() const {
    if (p_impl->p_flags & IDENT_FLAG_OVERRIDE) {
        return var_type::OVERRIDABLE;
    } else if (p_impl->p_flags & IDENT_FLAG_PERSIST) {
        return var_type::PERSISTENT;
    } else {
        return var_type::DEFAULT;
    }
}

LIBCUBESCRIPT_EXPORT void global_var::save(state &cs) {
    auto &ts = state_p{cs}.ts();
    if ((ts.ident_flags & IDENT_FLAG_OVERRIDDEN) || is_overridable()) {
        if (p_impl->p_flags & IDENT_FLAG_PERSIST) {
            throw error{
                cs, "cannot override persistent variable '%s'",
                get_name().data()
            };
        }
        if (!(p_impl->p_flags & IDENT_FLAG_OVERRIDDEN)) {
            static_cast<var_impl *>(p_impl)->save_val();
        }
    } else {
        p_impl->p_flags &= IDENT_FLAG_OVERRIDDEN;
    }
}

LIBCUBESCRIPT_EXPORT integer_type integer_var::get_value() const {
    return static_cast<ivar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void integer_var::set_value(
    state &cs, integer_type val, bool do_write, bool trigger
) {
    if (is_read_only()) {
        throw error{
            cs, "variable '%s' is read only", get_name().data()
        };
    }
    if (!do_write) {
        return;
    }
    save(cs);
    set_raw_value(val);
    if (trigger) {
        var_changed(state_p{cs}.ts(), this);
    }
}

LIBCUBESCRIPT_EXPORT void integer_var::set_raw_value(integer_type val) {
    static_cast<ivar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT float_type float_var::get_value() const {
    return static_cast<fvar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void float_var::set_value(
    state &cs, float_type val, bool do_write, bool trigger
) {
    if (is_read_only()) {
        throw error{
            cs, "variable '%s' is read only", get_name().data()
        };
    }
    if (!do_write) {
        return;
    }
    save(cs);
    set_raw_value(val);
    if (trigger) {
        var_changed(state_p{cs}.ts(), this);
    }
}

LIBCUBESCRIPT_EXPORT void float_var::set_raw_value(float_type val) {
    static_cast<fvar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT string_ref string_var::get_value() const {
    return static_cast<svar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void string_var::set_value(
    state &cs, string_ref val, bool do_write, bool trigger
) {
    if (is_read_only()) {
        throw error{
            cs, "variable '%s' is read only", get_name().data()
        };
    }
    if (!do_write) {
        return;
    }
    save(cs);
    set_raw_value(std::move(val));
    if (trigger) {
        var_changed(state_p{cs}.ts(), this);
    }
}

LIBCUBESCRIPT_EXPORT void string_var::set_raw_value(string_ref val) {
    static_cast<svar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT any_value alias::get_value(state &cs) const {
    return state_p{cs}.ts().get_astack(this).node->val_s;
}

LIBCUBESCRIPT_EXPORT void alias::set_value(state &cs, any_value v) {
    auto &ts = state_p{cs}.ts();
    if (is_arg()) {
        ts.get_astack(this).set_arg(this, ts, v);
    } else {
        ts.get_astack(this).set_alias(this, ts, v);
    }
}

LIBCUBESCRIPT_EXPORT bool alias::is_arg() const {
    return (static_cast<alias_impl const *>(this)->p_flags & IDENT_FLAG_ARG);
}

LIBCUBESCRIPT_EXPORT std::string_view command::get_args() const {
    return static_cast<command_impl const *>(this)->p_cargs;
}

LIBCUBESCRIPT_EXPORT int command::get_num_args() const {
    return static_cast<command_impl const *>(this)->p_numargs;
}

/* external API for alias stack management */

LIBCUBESCRIPT_EXPORT alias_local::alias_local(state &cs, ident *a) {
    if (!a || !a->is_alias()) {
        p_alias = nullptr;
        return;
    }
    auto &ts = state_p{cs}.ts();
    p_alias = static_cast<alias *>(a);
    auto &ast = ts.get_astack(p_alias);
    ast.push(ts.idstack.emplace_back(cs));
    p_sp = &ast;
    ast.flags &= ~IDENT_FLAG_UNKNOWN;
}

LIBCUBESCRIPT_EXPORT alias_local::~alias_local() {
    if (p_alias) {
        static_cast<alias_stack *>(p_sp)->pop();
    }
}

LIBCUBESCRIPT_EXPORT bool alias_local::set(any_value val) {
    if (!p_alias) {
        return false;
    }
    static_cast<alias_stack *>(p_sp)->node->val_s = std::move(val);
    return true;
}

LIBCUBESCRIPT_EXPORT alias_local::operator bool() const noexcept {
    return !!p_alias;
}

} /* namespace cubescript */
