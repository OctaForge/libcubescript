#ifndef LIBCUBESCRIPT_ALIAS_HH
#define LIBCUBESCRIPT_ALIAS_HH

#include <cubescript/cubescript.hh>

#include "cs_lock.hh"

#include <bitset>
#include <memory>

namespace cubescript {

static constexpr std::size_t MAX_ARGUMENTS = 32;
using argset = std::bitset<MAX_ARGUMENTS>;

struct var_value {
    /* ugly but does the trick */
    using FS = std::conditional_t<
        sizeof(float_type) == sizeof(unsigned char),
        unsigned char,
        std::conditional_t<
            sizeof(float_type) == sizeof(unsigned short),
            unsigned short,
            std::conditional_t<
                sizeof(float_type) == sizeof(unsigned long),
                unsigned long,
                unsigned long long
            >
        >
    >;

    var_value(integer_type v);
    var_value(float_type v);
    var_value(std::string_view const &v, state &cs);

    ~var_value();

    value_type type() const {
        return p_type;
    }

    void save();
    void restore();

    void steal_value(any_value &v, state &cs);
    any_value to_value() const;

private:
    using VU = union {
        atomic_type<integer_type> i;
        atomic_type<FS> f;
        atomic_type<char const *> s;
    };

    /* fixed upon creation */
    value_type p_type;
    alignas(VU) unsigned char p_stor[sizeof(VU)];
    alignas(VU) unsigned char p_ostor[sizeof(VU)];
};

enum {
    ID_UNKNOWN = -1, ID_VAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_BREAK, ID_CONTINUE, ID_RESULT,
    ID_NOT, ID_AND, ID_OR
};

enum {
    IDENT_FLAG_UNKNOWN    = 1 << 0,
    IDENT_FLAG_ARG        = 1 << 1,
    IDENT_FLAG_READONLY   = 1 << 2,
    IDENT_FLAG_OVERRIDE   = 1 << 3,
    IDENT_FLAG_OVERRIDDEN = 1 << 4,
    IDENT_FLAG_PERSIST    = 1 << 5
};

struct ident_stack {
    any_value val_s;
    bcode_ref code;
    ident_stack *next;
    ident_stack(): val_s{}, code{}, next{nullptr} {}
};

struct alias_stack {
    ident_stack *node = nullptr;
    int flags = 0;

    void push(ident_stack &st);
    void pop();

    void set_arg(alias *a, thread_state &ts, any_value &v);
    void set_alias(alias *a, thread_state &ts, any_value &v);
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
    atomic_type<int> p_flags;
    /* represents the ident_type above, but internally it has a wider
     * variety of values, so it's an int here (maps to an internal enum)
     */
    int p_type;

    int p_index = -1;
};

bool ident_is_callable(ident const *id);

struct var_impl: ident_impl, builtin_var {
    var_impl(string_ref name, int flags, integer_type v);
    var_impl(string_ref name, int flags, float_type v);
    var_impl(string_ref name, int flags, std::string_view const &v, state &cs);

    command *get_setter(thread_state &ts) const;

    var_value p_storage;
};

void var_changed(thread_state &ts, builtin_var &id, any_value &oldval);

struct alias_impl: ident_impl, alias {
    alias_impl(state &cs, string_ref n, string_ref a, int flags);
    alias_impl(state &cs, string_ref n, std::string_view a, int flags);
    alias_impl(state &cs, string_ref n, integer_type a, int flags);
    alias_impl(state &cs, string_ref n, float_type a, int flags);
    alias_impl(state &cs, string_ref n, int flags);
    alias_impl(state &cs, string_ref n, any_value v, int flags);

    ident_stack p_initial;
};

struct command_impl: ident_impl, command {
    command_impl(
        string_ref name, string_ref args, int numargs, command_func func
    );

    void call_id(
        thread_state &ts, span_type<any_value> args, any_value &ret
    ) const;

    string_ref p_cargs;
    command_func p_cb_cftv;
    int p_numargs;
};

bool ident_is_used_arg(ident const *id, thread_state &ts);

struct ident_p {
    ident_p(ident &id): ip{&id} {}

    ident_impl &impl() { return *ip->p_impl; }
    void impl(ident_impl *impl) { ip->p_impl = impl; }

    ident *ip;
};

} /* namespace cubescript */

#endif
