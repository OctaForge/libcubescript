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
    ident_type tp, string_ref name, var_cb_func f, int fl
):
    ident_impl{tp, name, fl}, cb_var{std::move(f)}
{}

void var_impl::changed(state &cs) {
    if (cb_var) {
        switch (p_type) {
            case ID_IVAR:
                cb_var(cs, *static_cast<ivar_impl *>(this));
                break;
            case ID_FVAR:
                cb_var(cs, *static_cast<fvar_impl *>(this));
                break;
            case ID_SVAR:
                cb_var(cs, *static_cast<svar_impl *>(this));
                break;
            default:
                break;
        }
    }
}

ivar_impl::ivar_impl(
    string_ref name, integer_type m, integer_type x, integer_type v, var_cb_func f, int fl
):
    var_impl{
        ident_type::IVAR, name, std::move(f),
        fl | ((m > x) ? IDENT_FLAG_READONLY : 0)
    },
    p_storage{v}, p_minval{m}, p_maxval{x}, p_overrideval{0}
{}

fvar_impl::fvar_impl(
    string_ref name, float_type m, float_type x, float_type v, var_cb_func f, int fl
):
    var_impl{
        ident_type::FVAR, name, std::move(f),
        fl | ((m > x) ? IDENT_FLAG_READONLY : 0)
    },
    p_storage{v}, p_minval{m}, p_maxval{x}, p_overrideval{0}
{}

svar_impl::svar_impl(
    string_ref name, string_ref v, string_ref ov, var_cb_func f, int fl
):
    var_impl{ident_type::SVAR, name, std::move(f), fl},
    p_storage{v}, p_overrideval{ov}
{}

alias_impl::alias_impl(
    state &cs, string_ref name, string_ref a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s.set_str(a);
}

alias_impl::alias_impl(
    state &cs, string_ref name, std::string_view a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s.set_str(a);
}

alias_impl::alias_impl(state &cs, string_ref name, integer_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s.set_int(a);
}

alias_impl::alias_impl(state &cs, string_ref name, float_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s.set_float(a);
}

alias_impl::alias_impl(state &cs, string_ref name, int fl):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s.set_none();
}

alias_impl::alias_impl(state &cs, string_ref name, any_value v, int fl):
    ident_impl{ident_type::ALIAS, name, fl},
    p_initial{cs}, p_astack{&p_initial}
{
    p_initial.val_s = v;
}

command_impl::command_impl(
    string_ref name, string_ref args, int nargs, command_func f
):
    ident_impl{ident_type::COMMAND, name, 0},
    p_cargs{args}, p_cb_cftv{std::move(f)}, p_numargs{nargs}
{}

void command_impl::call(state &cs, std::span<any_value> args, any_value &ret) {
    auto &ts = *cs.thread_pointer();
    auto idstsz = ts.idstack.size();
    try {
        p_cb_cftv(cs, args, ret);
    } catch (...) {
        ts.idstack.resize(idstsz, ident_stack{cs});
        throw;
    }
    ts.idstack.resize(idstsz, ident_stack{cs});
}

bool ident_is_used_arg(ident *id, thread_state &ts) {
    if (!ts.callstack) {
        return true;
    }
    return ts.callstack->usedargs[id->get_index()];
}

void alias_stack::set_arg(alias *a, thread_state &ts, any_value &v) {
    if (ident_is_used_arg(a, ts)) {
        node->code = bcode_ref{};
    } else {
        auto &st = ts.idstack.emplace_back(*ts.pstate);
        st.next = node;
        node = &st;
        ts.callstack->usedargs[a->get_index()] = true;
    }
    node->val_s = std::move(v);
}

void alias_stack::set_alias(alias *a, thread_state &ts, any_value &v) {
    node->val_s = std::move(v);
    node->code = bcode_ref{};
    auto *ai = static_cast<alias_impl *>(a);
    ai->p_flags = (ai->p_flags & ts.pstate->identflags) | ts.pstate->identflags;
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

LIBCUBESCRIPT_EXPORT int ident::get_flags() const {
    return p_impl->p_flags;
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

LIBCUBESCRIPT_EXPORT integer_type integer_var::get_val_min() const {
    return static_cast<ivar_impl const *>(this)->p_minval;
}

LIBCUBESCRIPT_EXPORT integer_type integer_var::get_val_max() const {
    return static_cast<ivar_impl const *>(this)->p_maxval;
}

LIBCUBESCRIPT_EXPORT integer_type integer_var::get_value() const {
    return static_cast<ivar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void integer_var::set_value(integer_type val) {
    static_cast<ivar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT float_type float_var::get_val_min() const {
    return static_cast<fvar_impl const *>(this)->p_minval;
}

LIBCUBESCRIPT_EXPORT float_type float_var::get_val_max() const {
    return static_cast<fvar_impl const *>(this)->p_maxval;
}

LIBCUBESCRIPT_EXPORT float_type float_var::get_value() const {
    return static_cast<fvar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void float_var::set_value(float_type val) {
    static_cast<fvar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT string_ref string_var::get_value() const {
    return static_cast<svar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void string_var::set_value(string_ref val) {
    static_cast<svar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT std::string_view command::get_args() const {
    return static_cast<command_impl const *>(this)->p_cargs;
}

LIBCUBESCRIPT_EXPORT int command::get_num_args() const {
    return static_cast<command_impl const *>(this)->p_numargs;
}

/* external API for alias stack management */

LIBCUBESCRIPT_EXPORT alias_local::alias_local(state &cs, ident *a) {
    if (!a || !a->is_alias() || (a->get_flags() & IDENT_FLAG_ARG)) {
        p_alias = nullptr;
        return;
    }
    auto &ts = *cs.thread_pointer();
    p_alias = static_cast<alias *>(a);
    auto &ast = ts.get_astack(p_alias);
    auto &st = ts.idstack.emplace_back(cs);
    st.next = ast.node;
    ast.node = &st;
    p_sp = &ast;
    static_cast<alias_impl *>(p_alias)->p_flags &= ~IDENT_FLAG_UNKNOWN;
}

LIBCUBESCRIPT_EXPORT alias_local::~alias_local() {
    if (p_alias) {
        auto &st = *static_cast<alias_stack *>(p_sp);
        st.node = st.node->next;
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
