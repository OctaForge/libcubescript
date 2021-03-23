#include "cs_ident.hh"

#include "cs_bcode.hh"
#include "cs_vm.hh"

namespace cscript {

cs_ident_impl::cs_ident_impl(cs_ident_type tp, cs_strref nm, int fl):
    p_name{nm}, p_type{int(tp)}, p_flags{fl}
{}

cs_var_impl::cs_var_impl(
    cs_ident_type tp, cs_strref name, cs_var_cb f, int fl
):
    cs_ident_impl{tp, name, fl}, cb_var{std::move(f)}
{}

void cs_var_impl::changed(cs_state &cs) {
    if (cb_var) {
        switch (p_type) {
            case ID_IVAR:
                cb_var(cs, *static_cast<cs_ivar_impl *>(this));
                break;
            case ID_FVAR:
                cb_var(cs, *static_cast<cs_fvar_impl *>(this));
                break;
            case ID_SVAR:
                cb_var(cs, *static_cast<cs_svar_impl *>(this));
                break;
            default:
                break;
        }
    }
}

cs_ivar_impl::cs_ivar_impl(
    cs_strref name, cs_int m, cs_int x, cs_int v, cs_var_cb f, int fl
):
    cs_var_impl{
        cs_ident_type::IVAR, name, std::move(f),
        fl | ((m > x) ? CS_IDF_READONLY : 0)
    },
    p_storage{v}, p_minval{m}, p_maxval{x}, p_overrideval{0}
{}

cs_fvar_impl::cs_fvar_impl(
    cs_strref name, cs_float m, cs_float x, cs_float v, cs_var_cb f, int fl
):
    cs_var_impl{
        cs_ident_type::FVAR, name, std::move(f),
        fl | ((m > x) ? CS_IDF_READONLY : 0)
    },
    p_storage{v}, p_minval{m}, p_maxval{x}, p_overrideval{0}
{}

cs_svar_impl::cs_svar_impl(
    cs_strref name, cs_strref v, cs_strref ov, cs_var_cb f, int fl
):
    cs_var_impl{cs_ident_type::SVAR, name, std::move(f), fl},
    p_storage{v}, p_overrideval{ov}
{}

cs_alias_impl::cs_alias_impl(
    cs_state &cs, cs_strref name, cs_strref a, int fl
):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val.set_str(a);
}

cs_alias_impl::cs_alias_impl(
    cs_state &cs, cs_strref name, std::string_view a, int fl
):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val.set_str(a);
}

cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_int a, int fl):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val.set_int(a);
}

cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_float a, int fl):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val.set_float(a);
}

cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, int fl):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val.set_none();
}

cs_alias_impl::cs_alias_impl(cs_state &cs, cs_strref name, cs_value v, int fl):
    cs_ident_impl{cs_ident_type::ALIAS, name, fl},
    p_acode{nullptr}, p_astack{nullptr}, p_val{cs}
{
    p_val = v;
}

void cs_alias_impl::push_arg(cs_value &v, cs_ident_stack &st, bool um) {
    if (p_astack == &st) {
        /* prevent cycles and unnecessary code elsewhere */
        p_val = std::move(v);
        clean_code();
        return;
    }
    st.val_s = std::move(p_val);
    st.next = p_astack;
    p_astack = &st;
    p_val = std::move(v);
    clean_code();
    if (um) {
        p_flags &= ~CS_IDF_UNKNOWN;
    }
}

void cs_alias_impl::pop_arg() {
    if (!p_astack) {
        return;
    }
    cs_ident_stack *st = p_astack;
    p_val = std::move(p_astack->val_s);
    clean_code();
    p_astack = st->next;
}

void cs_alias_impl::undo_arg(cs_ident_stack &st) {
    cs_ident_stack *prev = p_astack;
    st.val_s = std::move(p_val);
    st.next = prev;
    p_astack = prev->next;
    p_val = std::move(prev->val_s);
    clean_code();
}

void cs_alias_impl::redo_arg(cs_ident_stack &st) {
    cs_ident_stack *prev = st.next;
    prev->val_s = std::move(p_val);
    p_astack = prev;
    p_val = std::move(st.val_s);
    clean_code();
}

void cs_alias_impl::set_arg(cs_state &cs, cs_value &v) {
    if (ident_is_used_arg(this, cs)) {
        p_val = std::move(v);
        clean_code();
    } else {
        push_arg(v, cs.p_callstack->argstack[get_index()], false);
        cs.p_callstack->usedargs |= 1 << get_index();
    }
}

void cs_alias_impl::set_alias(cs_state &cs, cs_value &v) {
    p_val = std::move(v);
    clean_code();
    p_flags = (p_flags & cs.identflags) | cs.identflags;
}

void cs_alias_impl::clean_code() {
    if (p_acode) {
        bcode_decr(p_acode->get_raw());
        p_acode = nullptr;
    }
}

cs_bcode *cs_alias_impl::compile_code(cs_state &cs) {
    if (!p_acode) {
        cs_gen_state gs(cs);
        gs.code.reserve(64);
        gs.gen_main(get_value().get_str());
        /* i wish i could steal the memory somehow */
        uint32_t *code = bcode_alloc(cs, gs.code.size());
        memcpy(code, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        bcode_incr(code);
        p_acode = reinterpret_cast<cs_bcode *>(code);
    }
    return p_acode;
}

cs_command_impl::cs_command_impl(
    cs_strref name, cs_strref args, int nargs, cs_command_cb f
):
    cs_ident_impl{cs_ident_type::COMMAND, name, 0},
    p_cargs{args}, p_cb_cftv{std::move(f)}, p_numargs{nargs}
{}

bool ident_is_used_arg(cs_ident *id, cs_state &cs) {
    if (!cs.p_callstack) {
        return true;
    }
    return cs.p_callstack->usedargs & (1 << id->get_index());
}

/* public interface */

LIBCUBESCRIPT_EXPORT int cs_ident::get_raw_type() const {
    return p_impl->p_type;
}

LIBCUBESCRIPT_EXPORT cs_ident_type cs_ident::get_type() const {
    if (p_impl->p_type > ID_ALIAS) {
        return cs_ident_type::SPECIAL;
    }
    return cs_ident_type(p_impl->p_type);
}

LIBCUBESCRIPT_EXPORT std::string_view cs_ident::get_name() const {
    return p_impl->p_name;
}

LIBCUBESCRIPT_EXPORT int cs_ident::get_flags() const {
    return p_impl->p_flags;
}

LIBCUBESCRIPT_EXPORT int cs_ident::get_index() const {
    return p_impl->p_index;
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_alias() const {
    return get_type() == cs_ident_type::ALIAS;
}

LIBCUBESCRIPT_EXPORT cs_alias *cs_ident::get_alias() {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<cs_alias *>(this);
}

LIBCUBESCRIPT_EXPORT cs_alias const *cs_ident::get_alias() const {
    if (!is_alias()) {
        return nullptr;
    }
    return static_cast<cs_alias const *>(this);
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_command() const {
    return get_type() == cs_ident_type::COMMAND;
}

LIBCUBESCRIPT_EXPORT cs_command *cs_ident::get_command() {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<cs_command_impl *>(this);
}

LIBCUBESCRIPT_EXPORT cs_command const *cs_ident::get_command() const {
    if (!is_command()) {
        return nullptr;
    }
    return static_cast<cs_command_impl const *>(this);
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_special() const {
    return get_type() == cs_ident_type::SPECIAL;
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_var() const {
    switch (get_type()) {
        case cs_ident_type::IVAR:
        case cs_ident_type::FVAR:
        case cs_ident_type::SVAR:
            return true;
        default:
            break;
    }
    return false;
}

LIBCUBESCRIPT_EXPORT cs_var *cs_ident::get_var() {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<cs_var *>(this);
}

LIBCUBESCRIPT_EXPORT cs_var const *cs_ident::get_var() const {
    if (!is_var()) {
        return nullptr;
    }
    return static_cast<cs_var const *>(this);
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_ivar() const {
    return get_type() == cs_ident_type::IVAR;
}

LIBCUBESCRIPT_EXPORT cs_ivar *cs_ident::get_ivar() {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<cs_ivar *>(this);
}

LIBCUBESCRIPT_EXPORT cs_ivar const *cs_ident::get_ivar() const {
    if (!is_ivar()) {
        return nullptr;
    }
    return static_cast<cs_ivar const *>(this);
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_fvar() const {
    return get_type() == cs_ident_type::FVAR;
}

LIBCUBESCRIPT_EXPORT cs_fvar *cs_ident::get_fvar() {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<cs_fvar *>(this);
}

LIBCUBESCRIPT_EXPORT cs_fvar const *cs_ident::get_fvar() const {
    if (!is_fvar()) {
        return nullptr;
    }
    return static_cast<cs_fvar const *>(this);
}

LIBCUBESCRIPT_EXPORT bool cs_ident::is_svar() const {
    return get_type() == cs_ident_type::SVAR;
}

LIBCUBESCRIPT_EXPORT cs_svar *cs_ident::get_svar() {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<cs_svar *>(this);
}

LIBCUBESCRIPT_EXPORT cs_svar const *cs_ident::get_svar() const {
    if (!is_svar()) {
        return nullptr;
    }
    return static_cast<cs_svar const *>(this);
}

LIBCUBESCRIPT_EXPORT cs_int cs_ivar::get_val_min() const {
    return static_cast<cs_ivar_impl const *>(this)->p_minval;
}

LIBCUBESCRIPT_EXPORT cs_int cs_ivar::get_val_max() const {
    return static_cast<cs_ivar_impl const *>(this)->p_maxval;
}

LIBCUBESCRIPT_EXPORT cs_int cs_ivar::get_value() const {
    return static_cast<cs_ivar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void cs_ivar::set_value(cs_int val) {
    static_cast<cs_ivar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT cs_float cs_fvar::get_val_min() const {
    return static_cast<cs_fvar_impl const *>(this)->p_minval;
}

LIBCUBESCRIPT_EXPORT cs_float cs_fvar::get_val_max() const {
    return static_cast<cs_fvar_impl const *>(this)->p_maxval;
}

LIBCUBESCRIPT_EXPORT cs_float cs_fvar::get_value() const {
    return static_cast<cs_fvar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void cs_fvar::set_value(cs_float val) {
    static_cast<cs_fvar_impl *>(this)->p_storage = val;
}

LIBCUBESCRIPT_EXPORT cs_strref cs_svar::get_value() const {
    return static_cast<cs_svar_impl const *>(this)->p_storage;
}

LIBCUBESCRIPT_EXPORT void cs_svar::set_value(cs_strref val) {
    static_cast<cs_svar_impl *>(this)->p_storage = val;
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

LIBCUBESCRIPT_EXPORT std::string_view cs_command::get_args() const {
    return static_cast<cs_command_impl const *>(this)->p_cargs;
}

LIBCUBESCRIPT_EXPORT int cs_command::get_num_args() const {
    return static_cast<cs_command_impl const *>(this)->p_numargs;
}

} /* namespace cscript */
