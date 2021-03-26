#ifndef LIBCUBESCRIPT_ALIAS_HH
#define LIBCUBESCRIPT_ALIAS_HH

#include <cubescript/cubescript.hh>

namespace cubescript {

enum {
    ID_UNKNOWN = -1, ID_IVAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_BREAK, ID_CONTINUE, ID_RESULT,
    ID_NOT, ID_AND, ID_OR
};

struct ident_link {
    ident *id;
    ident_link *next;
    int usedargs;
    ident_stack *argstack;
};

struct ident_impl {
    ident_impl() = delete;
    ident_impl(ident_impl const &) = delete;
    ident_impl(ident_impl &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~ident_impl() {};

    ident_impl &operator=(ident_impl const &) = delete;
    ident_impl &operator=(ident_impl &&) = delete;

    ident_impl(ident_type tp, string_ref name, int flags = 0);

    string_ref p_name;
    /* represents the ident_type above, but internally it has a wider
     * variety of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

    int p_index = -1;
};

struct var_impl: ident_impl {
    var_impl(
        ident_type tp, string_ref name, var_cb_func func, int flags = 0
    );

    var_cb_func cb_var;

    void changed(state &cs);
};

struct ivar_impl: var_impl, integer_var {
    ivar_impl(
        string_ref n, integer_type m, integer_type x, integer_type v, var_cb_func f, int flags
    );

    integer_type p_storage, p_minval, p_maxval, p_overrideval;
};

struct fvar_impl: var_impl, float_var {
    fvar_impl(
        string_ref n, float_type m, float_type x, float_type v,
        var_cb_func f, int flags
    );

    float_type p_storage, p_minval, p_maxval, p_overrideval;
};

struct svar_impl: var_impl, string_var {
    svar_impl(
        string_ref n, string_ref v, string_ref ov, var_cb_func f, int flags
    );

    string_ref p_storage, p_overrideval;
};

struct alias_impl: ident_impl, alias {
    alias_impl(state &cs, string_ref n, string_ref a, int flags);
    alias_impl(state &cs, string_ref n, std::string_view a, int flags);
    alias_impl(state &cs, string_ref n, integer_type a, int flags);
    alias_impl(state &cs, string_ref n, float_type a, int flags);
    alias_impl(state &cs, string_ref n, int flags);
    alias_impl(state &cs, string_ref n, any_value v, int flags);

    void push_arg(any_value &v, ident_stack &st, bool um = true);
    void pop_arg();
    void undo_arg(ident_stack &st);
    void redo_arg(ident_stack &st);
    void set_arg(thread_state &ts, any_value &v);
    void set_alias(thread_state &ts, any_value &v);

    void clean_code();
    bcode *compile_code(thread_state &ts);

    bcode *p_acode;
    ident_stack *p_astack;
    any_value p_val;
};

struct command_impl: ident_impl, command {
    command_impl(
        string_ref name, string_ref args, int numargs, command_func func
    );

    void call(state &cs, std::span<any_value> args, any_value &ret) {
        p_cb_cftv(cs, args, ret);
    }

    string_ref p_cargs;
    command_func p_cb_cftv;
    int p_numargs;
};

bool ident_is_used_arg(ident *id, thread_state &ts);

} /* namespace cubescript */

#endif
