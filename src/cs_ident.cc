#include "cs_ident.hh"

#include "cs_bcode.hh"
#include "cs_thread.hh"
#include "cs_vm.hh"
#include "cs_error.hh"

namespace cubescript {

ident_impl::ident_impl(ident_type tp, string_ref nm, int fl):
    p_name{nm}, p_type{int(tp)}, p_flags{fl}
{}

bool ident_is_callable(ident const *id) {
    auto tp = id->type();
    if ((tp != ident_type::COMMAND) && (tp != ident_type::SPECIAL)) {
        return false;
    }
    return !!static_cast<command_impl const *>(id)->p_cb_cftv;
}

var_impl::var_impl(string_ref name, int fl):
    ident_impl{ident_type::VAR, name, fl}
{}

alias_impl::alias_impl(
    state &, string_ref name, string_ref a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s.set_string(a);
}

alias_impl::alias_impl(
    state &cs, string_ref name, std::string_view a, int fl
):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s.set_string(a, cs);
}

alias_impl::alias_impl(state &, string_ref name, integer_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s.set_integer(a);
}

alias_impl::alias_impl(state &, string_ref name, float_type a, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s.set_float(a);
}

alias_impl::alias_impl(state &, string_ref name, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s.set_none();
}

alias_impl::alias_impl(state &, string_ref name, any_value v, int fl):
    ident_impl{ident_type::ALIAS, name, fl}, p_initial{}
{
    p_initial.val_s = v.get_plain();
}

command_impl::command_impl(
    string_ref name, string_ref args, int nargs, command_func f
):
    ident_impl{ident_type::COMMAND, name, 0},
    p_cargs{args}, p_cb_cftv{std::move(f)}, p_numargs{nargs}
{}

void var_changed(thread_state &ts, builtin_var &id, any_value &oldval) {
    auto *cid = ts.istate->cmd_var_changed;
    if (!cid) {
        return;
    }
    auto *cimp = static_cast<command_impl *>(cid);
    any_value val[3] = {};
    val[0].set_ident(id);
    val[1] = std::move(oldval);
    val[2] = id.value();
    cimp->call(ts, span_type<any_value>{
        static_cast<any_value *>(val), 3
    }, val[0]);
}

command *var_impl::get_setter(thread_state &ts) const {
    switch (p_storage.type()) {
        case value_type::INTEGER:
            return ts.istate->cmd_ivar;
        case value_type::FLOAT:
            return ts.istate->cmd_fvar;
        case value_type::STRING:
            return ts.istate->cmd_svar;
        default:
            break;
    }
    abort();
    return nullptr; /* not reached */
}

void command_impl::call(
    thread_state &ts, span_type<any_value> args, any_value &ret
) const {
    auto idstsz = ts.idstack.size();
    try {
        p_cb_cftv(*ts.pstate, args, ret);
    } catch (...) {
        ts.idstack.resize(idstsz);
        throw;
    }
    ts.idstack.resize(idstsz);
}

bool ident_is_used_arg(ident const *id, thread_state &ts) {
    if (!ts.callstack) {
        return true;
    }
    return ts.callstack->usedargs[id->index()];
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
        push(ts.idstack.emplace_back());
        ts.callstack->usedargs[a->index()] = true;
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

LIBCUBESCRIPT_EXPORT ident::~ident() {}

LIBCUBESCRIPT_EXPORT ident_type ident::type() const {
    if (p_impl->p_type > ID_ALIAS) {
        return ident_type::SPECIAL;
    }
    return ident_type(p_impl->p_type);
}

LIBCUBESCRIPT_EXPORT std::string_view ident::name() const {
    return p_impl->p_name;
}

LIBCUBESCRIPT_EXPORT int ident::index() const {
    return p_impl->p_index;
}

LIBCUBESCRIPT_EXPORT bool ident::operator==(ident &other) const {
    return this == &other;
}

LIBCUBESCRIPT_EXPORT bool ident::operator!=(ident &other) const {
    return this != &other;
}

LIBCUBESCRIPT_EXPORT bool ident::is_overridden(state &cs) const {
    switch (type()) {
        case ident_type::VAR:
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
    switch (type()) {
        case ident_type::VAR:
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

LIBCUBESCRIPT_EXPORT any_value ident::call(span_type<any_value>, state &cs) {
    throw error{cs, "this ident type is not callable"};
}

LIBCUBESCRIPT_EXPORT bool builtin_var::is_read_only() const {
    return (p_impl->p_flags & IDENT_FLAG_READONLY);
}

LIBCUBESCRIPT_EXPORT bool builtin_var::is_overridable() const {
    return (p_impl->p_flags & IDENT_FLAG_OVERRIDE);
}

LIBCUBESCRIPT_EXPORT var_type builtin_var::variable_type() const {
    if (p_impl->p_flags & IDENT_FLAG_OVERRIDE) {
        return var_type::OVERRIDABLE;
    } else if (p_impl->p_flags & IDENT_FLAG_PERSIST) {
        return var_type::PERSISTENT;
    } else {
        return var_type::DEFAULT;
    }
}

LIBCUBESCRIPT_EXPORT void builtin_var::save(state &cs) {
    auto &ts = state_p{cs}.ts();
    if ((ts.ident_flags & IDENT_FLAG_OVERRIDDEN) || is_overridable()) {
        if (p_impl->p_flags & IDENT_FLAG_PERSIST) {
            throw error_p::make(
                cs, "cannot override persistent variable '%s'",
                name().data()
            );
        }
        if (!(p_impl->p_flags & IDENT_FLAG_OVERRIDDEN)) {
            auto *imp = static_cast<var_impl *>(p_impl);
            imp->p_override = std::move(imp->p_storage);
            p_impl->p_flags |= IDENT_FLAG_OVERRIDDEN;
        }
    } else {
        p_impl->p_flags &= IDENT_FLAG_OVERRIDDEN;
    }
}

LIBCUBESCRIPT_EXPORT any_value builtin_var::call(
    span_type<any_value> args, state &cs
) {
    command *hid = static_cast<var_impl *>(this)->get_setter(state_p{cs}.ts());
    any_value ret{};
    auto &ts = state_p{cs}.ts();
    auto *cimp = static_cast<command_impl *>(hid);
    auto &targs = ts.vmstack;
    auto osz = targs.size();
    auto anargs = std::size_t(cimp->arg_count());
    auto nargs = args.size();
    targs.resize(
        osz + std::max(args.size(), anargs + 1)
    );
    for (std::size_t i = 0; i < nargs; ++i) {
        targs[osz + i + 1] = args[i];
    }
    exec_command(ts, cimp, this, &targs[osz], ret, nargs + 1, false);
    return ret;
}

LIBCUBESCRIPT_EXPORT any_value builtin_var::value() const {
    return static_cast<var_impl const *>(p_impl)->p_storage;
}

LIBCUBESCRIPT_EXPORT void builtin_var::set_raw_value(
    state &cs, any_value val
) {
    switch (static_cast<var_impl *>(p_impl)->p_storage.type()) {
        case value_type::INTEGER:
            val.force_integer();
            break;
        case value_type::FLOAT:
            val.force_float();
            break;
        case value_type::STRING:
            val.force_string(cs);
            break;
        default:
            abort(); /* unreachable unless we have a bug */
            break;
    }
    static_cast<var_impl *>(p_impl)->p_storage = std::move(val);
}

LIBCUBESCRIPT_EXPORT void builtin_var::set_value(
    state &cs, any_value val, bool do_write, bool trigger
) {
    if (is_read_only()) {
        throw error_p::make(
            cs, "variable '%s' is read only", name().data()
        );
    }
    if (!do_write) {
        return;
    }
    save(cs);
    auto oldv = value();
    set_raw_value(cs, std::move(val));
    if (trigger) {
        var_changed(state_p{cs}.ts(), *this, oldv);
    }
}

LIBCUBESCRIPT_EXPORT any_value alias::value(state &cs) const {
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

LIBCUBESCRIPT_EXPORT any_value alias::call(
    span_type<any_value> args, state &cs
) {
    any_value ret{};
    auto &ts = state_p{cs}.ts();
    if (is_arg() && !ident_is_used_arg(this, ts)) {
        return ret;
    }
    auto nargs = args.size();
    exec_alias(ts, this, &args[0], ret, nargs, nargs, 0, 0, BC_RET_NULL, true);
    return ret;
}

LIBCUBESCRIPT_EXPORT std::string_view command::args() const {
    return static_cast<command_impl const *>(this)->p_cargs;
}

LIBCUBESCRIPT_EXPORT int command::arg_count() const {
    return static_cast<command_impl const *>(this)->p_numargs;
}

LIBCUBESCRIPT_EXPORT any_value command::call(
    span_type<any_value> args, state &cs
) {
    any_value ret{};
    auto &cimpl = static_cast<command_impl &>(*this);
    if (!cimpl.p_cb_cftv) {
        return ret;
    }
    auto nargs = args.size();
    auto &ts = state_p{cs}.ts();
    if (nargs < std::size_t(cimpl.arg_count())) {
        auto &targs = ts.vmstack;
        auto osz = targs.size();
        targs.resize(osz + cimpl.arg_count());
        try {
            for (std::size_t i = 0; i < nargs; ++i) {
                targs[osz + i] = args[i];
            }
            exec_command(ts, &cimpl, this, &targs[osz], ret, nargs, false);
        } catch (...) {
            targs.resize(osz);
            throw;
        }
        targs.resize(osz);
    } else {
        exec_command(ts, &cimpl, this, &args[0], ret, nargs, false);
    }
    return ret;
}

/* external API for alias stack management */

LIBCUBESCRIPT_EXPORT alias_local::alias_local(state &cs, ident &a) {
    if (a.type() != ident_type::ALIAS) {
        throw error_p::make(cs, "ident '%s' is not an alias", a.name().data());
    }
    auto &ts = state_p{cs}.ts();
    p_alias = static_cast<alias *>(&a);
    auto &ast = ts.get_astack(p_alias);
    ast.push(ts.idstack.emplace_back());
    p_sp = &ast;
    ast.flags &= ~IDENT_FLAG_UNKNOWN;
}

LIBCUBESCRIPT_EXPORT alias_local::alias_local(state &cs, std::string_view name):
    alias_local{cs, cs.new_ident(name)}
{}

LIBCUBESCRIPT_EXPORT alias_local::alias_local(state &cs, any_value const &v):
    alias_local{cs, (
        v.type() == value_type::IDENT
    ) ? v.get_ident(cs) : cs.new_ident(v.get_string(cs))}
{}

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

} /* namespace cubescript */
