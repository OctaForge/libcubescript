#ifndef LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH

#include <string_view>

#include "value.hh"

namespace cubescript {

enum class ident_type {
    IVAR = 0, FVAR, SVAR, COMMAND, ALIAS, SPECIAL
};

struct global_var;
struct integer_var;
struct float_var;
struct string_var;
struct alias;
struct command;

struct LIBCUBESCRIPT_EXPORT ident {
    ident_type get_type() const;
    std::string_view get_name() const;
    int get_index() const;

    bool is_alias() const;
    alias *get_alias();
    alias const *get_alias() const;

    bool is_command() const;
    command *get_command();
    command const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    global_var *get_var();
    global_var const *get_var() const;

    bool is_ivar() const;
    integer_var *get_ivar();
    integer_var const *get_ivar() const;

    bool is_fvar() const;
    float_var *get_fvar();
    float_var const *get_fvar() const;

    bool is_svar() const;
    string_var *get_svar();
    string_var const *get_svar() const;

    bool is_overridden(state &cs) const;
    bool is_persistent(state &cs) const;

protected:
    friend struct ident_p;

    ident() = default;

    struct ident_impl *p_impl{};
};

enum class var_type {
    DEFAULT = 0,
    PERSISTENT,
    OVERRIDABLE
};

struct LIBCUBESCRIPT_EXPORT global_var: ident {
    bool is_read_only() const;
    bool is_overridable() const;

    var_type get_variable_type() const;

    void save(state &cs);

protected:
    global_var() = default;
};

struct LIBCUBESCRIPT_EXPORT integer_var: global_var {
    integer_type get_value() const;
    void set_value(
        state &cs, integer_type val, bool do_write = true, bool trigger = true
    );
    void set_raw_value(integer_type val);

protected:
    integer_var() = default;
};

struct LIBCUBESCRIPT_EXPORT float_var: global_var {
    float_type get_value() const;
    void set_value(
        state &cs, float_type val, bool do_write = true, bool trigger = true
    );
    void set_raw_value(float_type val);

protected:
    float_var() = default;
};

struct LIBCUBESCRIPT_EXPORT string_var: global_var {
    string_ref get_value() const;
    void set_value(
        state &cs, string_ref val, bool do_write = true, bool trigger = true
    );
    void set_raw_value(string_ref val);

protected:
    string_var() = default;
};

struct LIBCUBESCRIPT_EXPORT alias: ident {
    bool is_arg() const;

    any_value get_value(state &cs) const;
    void set_value(state &cs, any_value v);

protected:
    alias() = default;
};

struct LIBCUBESCRIPT_EXPORT command: ident {
    std::string_view get_args() const;
    int get_num_args() const;

protected:
    command() = default;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH */
