#ifndef LIBCUBESCRIPT_ALIAS_HH
#define LIBCUBESCRIPT_ALIAS_HH

#include <cubescript/cubescript.hh>

namespace cscript {

enum {
    ID_UNKNOWN = -1, ID_IVAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_BREAK, ID_CONTINUE, ID_RESULT,
    ID_NOT, ID_AND, ID_OR
};

struct cs_ident_link {
    cs_ident *id;
    cs_ident_link *next;
    int usedargs;
    cs_ident_stack *argstack;
};

struct cs_ident_impl {
    cs_ident_impl() = delete;
    cs_ident_impl(cs_ident_impl const &) = delete;
    cs_ident_impl(cs_ident_impl &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~cs_ident_impl() {};

    cs_ident_impl &operator=(cs_ident_impl const &) = delete;
    cs_ident_impl &operator=(cs_ident_impl &&) = delete;

    cs_ident_impl(cs_ident_type tp, cs_strref name, int flags = 0);

    cs_strref p_name;
    /* represents the cs_ident_type above, but internally it has a wider
     * variety of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

    int p_index = -1;
};

struct cs_var_impl: cs_ident_impl {
    cs_var_impl(
        cs_ident_type tp, cs_strref name, cs_var_cb func, int flags = 0
    );

    cs_var_cb cb_var;

    void changed(cs_state &cs);
};

struct cs_ivar_impl: cs_var_impl, cs_ivar {
    cs_ivar_impl(
        cs_strref n, cs_int m, cs_int x, cs_int v, cs_var_cb f, int flags
    );

    cs_int p_storage, p_minval, p_maxval, p_overrideval;
};

struct cs_fvar_impl: cs_var_impl, cs_fvar {
    cs_fvar_impl(
        cs_strref n, cs_float m, cs_float x, cs_float v,
        cs_var_cb f, int flags
    );

    cs_float p_storage, p_minval, p_maxval, p_overrideval;
};

struct cs_svar_impl: cs_var_impl, cs_svar {
    cs_svar_impl(
        cs_strref n, cs_strref v, cs_strref ov, cs_var_cb f, int flags
    );

    cs_strref p_storage, p_overrideval;
};

struct cs_alias_impl: cs_ident_impl, cs_alias {
    cs_alias_impl(cs_state &cs, cs_strref n, cs_strref a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, std::string_view a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_int a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_float a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_value v, int flags);

    void push_arg(cs_value &v, cs_ident_stack &st, bool um = true);
    void pop_arg();
    void undo_arg(cs_ident_stack &st);
    void redo_arg(cs_ident_stack &st);
    void set_arg(cs_state &cs, cs_value &v);
    void set_alias(cs_state &cs, cs_value &v);

    void clean_code();
    cs_bcode *compile_code(cs_state &cs);

    cs_bcode *p_acode;
    cs_ident_stack *p_astack;
    cs_value p_val;
};

struct cs_command_impl: cs_ident_impl, cs_command {
    cs_command_impl(
        cs_strref name, cs_strref args, int numargs, cs_command_cb func
    );

    void call(cs_state &cs, std::span<cs_value> args, cs_value &ret) {
        p_cb_cftv(cs, args, ret);
    }

    cs_strref p_cargs;
    cs_command_cb p_cb_cftv;
    int p_numargs;
};

bool ident_is_used_arg(cs_ident *id, cs_state &cs);

} /* namespace cscript */

#endif
