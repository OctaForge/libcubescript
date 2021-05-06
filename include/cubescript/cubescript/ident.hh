/** @file ident.hh
 *
 * @brief Identifier management.
 *
 * Identifiers in `libcubescript` represent variables, aliases, commands
 * and so on. This file contains the handles for those and everything you
 * need to interface with them.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH

#include <string_view>

#include "value.hh"

namespace cubescript {

/** @brief The type of the ident.
 *
 * Cubescript has a selection of idents. This represents the type of each.
 */
enum class ident_type {
    IVAR = 0, /**< @brief Integer builtin variable. */
    FVAR,     /**< @brief Float builtin variable. */
    SVAR,     /**< @brief String builtin variable. */
    COMMAND,  /**< @brief Builtin command. */
    ALIAS,    /**< @brief User assigned variable. */
    SPECIAL   /**< @brief Other (internal unexposed type). */
};

/** @brief The ident structure.
 *
 * Every object within the Cubescript language is represented with an ident.
 * This is the generic base interface. There are some operations that are
 * available on any ident.
 *
 * You can also check the actual type with it (cubescript::ident_type) and
 * decide to cast it to its appropriate specific type, or use the helpers.
 *
 * An ident always has a valid name. A valid name is pretty much any
 * valid Cubescript word (see cubescript::parse_word()) which does not
 * begin with a number (a digit, a `+` or `-` followed by a digit or a
 * period followed by a digit, or a period followed by a digit).
 */
struct LIBCUBESCRIPT_EXPORT ident {
    /** @brief Get the cubescript::ident_type of this ident. */
    ident_type type() const;

    /** @brief Get a view to the name of the ident. */
    std::string_view name() const;

    /** @brief Get the index of the ident.
     *
     * Idents are internally indexed. There is no guarantee of what index
     * the ident will have, but you can still use it to identify the object
     * with an integer (it is guaranteed that once created, it will stay the
     * same for the whole lifetime of the main thread).
     */
    int index() const;

    /** @brief Check if the idents are the same. */
    bool operator==(ident &other) const;

    /** @brief Check if the idents are not the same. */
    bool operator!=(ident &other) const;

    /** @brief Check if the ident is a cubescript::global_var.
     *
     * This will return `true` if ident::type() returns either
     * ident_type::IVAR, ident_type::FVAR or ident_type::SVAR.
     */
    bool is_var() const;

    /** @brief Get if the ident is overridden.
     *
     * This can be true for aliases or builtins. When an alias or a builtin
     * is assigned to and the VM is in override mode or the builtin is
     * var_type::OVERRIDABLE, they are marked as overridden (and builtins
     * have their value saved beforehand).
     *
     * This can be cleared later, which will erase the value (for aliases)
     * or restore the saved one (for builtins). For aliases, this can be
     * specific to the Cubescript thread.
     */
    bool is_overridden(state &cs) const;

    /** @brief Get if the ident is persistent.
     *
     * This can be true in two cases. Either it's a builtin and it has the
     * var_type::PERSISTENT flag, or it's an alias that is assigned to while
     * the VM is in persist mode. The latter can be thread specific (when the
     * alias is currently pushed).
     */
    bool is_persistent(state &cs) const;

    /** @brief Call the ident.
     *
     * The default implementation just throws a cubescript::error, since it
     * is not callable. It can be overridden as needed.
     *
     * If a command, it will simply be executed with the given arguments,
     * ensuring that missing ones are filled in and types are set properly.
     * If a builtin variable, the appropriate handler will be called. If
     * an alias, the value of it will be compiled and executed. Any other
     * ident type will simply do nothing.
     *
     * @return the return value
     */
    virtual any_value call(span_type<any_value> args, state &cs);

protected:
    friend struct ident_p;

    ident() = default;
    virtual ~ident();

    struct ident_impl *p_impl{};
};

/** @brief An additional cubescript::global_var type.
 *
 * Global vars can have no additional type, or they can be persistent, or
 * they can be overridable. Persistent variables are meant to be saved and
 * loaded later (the actual logic is up to the user of the library).
 *
 * Overridable variables are overridden when assigned to (this can also
 * happen to normal variables when the VM is in override mode), which saves
 * their old value (which can be restored later when un-overridden). This
 * is mutually exclusive; overridable variables cannot be persistent, and
 * attempting to assign to a persistent variable while the VM is in override
 * mode will raise an error.
 */
enum class var_type {
    DEFAULT = 0, /**< @brief The default type. */
    PERSISTENT,  /**< @brief Persistent variable. */
    OVERRIDABLE  /**< @brief Overridable variable. */
};

/** @brief A global variable.
 *
 * This represents one of cubescript::integer_var, cubescript::float_var or
 * cubescript::string_var as a single interface, with shared operations.
 */
struct LIBCUBESCRIPT_EXPORT global_var: ident {
    /** @brief Get whether the variable is read only.
     *
     * Variables can be set as read only during their creation (but not
     * later). This will prevent assignments to them from within the language
     * or using checked APIs, but it is still possible to assign to them
     * using raw APIs. The raw APIs will not invoke value triggers, however.
     */
    bool is_read_only() const;

    /** @brief Get whether the variable is overridable.
     *
     * Equivalent to `variable_type() == var_type::OVERRIDABLE`.
     */
    bool is_overridable() const;

    /** @brief Get the cubescript::var_type of the variable. */
    var_type variable_type() const;

    /** @brief Save the variable.
     *
     * This is mainly intended for variable assignment triggers. If the
     * variable is overridable or the given thread is in override mode,
     * this will save the current value of the variable (if not already
     * overridden). Otherwise, it will clear any existing overridden flag.
     *
     * @throw cubescript::error if the thread is in override mode and the
     * variable is persistent.
     */
    void save(state &cs);

    /** @brief Call the variable.
     *
     * While variables are not callable by themselves, this acts like
     * if calling the variable in the language. By default, that means
     * doing it with zero arguments retrieves its value, while passing
     * arguments will set its value. The actual semantics depend on how
     * the handler is set up for each variable type.
     */
    any_value call(span_type<any_value> args, state &cs);

    /** @brief Get the value of the variable. */
    any_value value() const;

protected:
    global_var() = default;
};

/** @brief An integer variable.
 *
 * A specialization of cubescript::global_var for integer values.
 */
struct LIBCUBESCRIPT_EXPORT integer_var: global_var {
    /** @brief Set the value of the variable.
     *
     * If read only, an error is raised. If `do_write` is `false`, nothing
     * will be performed other than the read-only checking. If `trigger` is
     * `false`, a potential variable change trigger command will not be
     * invoked. The value is saved with global_var::save(), assuming
     * `do_write` is `true`. After that, integer_var::set_raw_value()
     * is invoked, and then the trigger.
     *
     * @throw cubescript::error if read only or if the changed trigger throws.
     */
    void set_value(
        state &cs, integer_type val, bool do_write = true, bool trigger = true
    );

    /** @brief Set the value of the variable in a raw manner.
     *
     * This will always set the value and ignore any kinds of checks. It will
     * not invoke any triggers either, nor it will save the the value.
     */
    void set_raw_value(integer_type val);

    /** @brief Call override for integer vars. */
    any_value call(span_type<any_value> args, state &cs);

protected:
    integer_var() = default;
};

/** @brief A float variable.
 *
 * A specialization of cubescript::global_var for float values.
 */
struct LIBCUBESCRIPT_EXPORT float_var: global_var {
    /** @brief Set the value of the variable.
     *
     * If read only, an error is raised. If `do_write` is `false`, nothing
     * will be performed other than the read-only checking. If `trigger` is
     * `false`, a potential variable change trigger command will not be
     * invoked. The value is saved with global_var::save(), assuming
     * `do_write` is `true`. After that, integer_var::set_raw_value()
     * is invoked, and then the trigger.
     *
     * @throw cubescript::error if read only or if the changed trigger throws.
     */
    void set_value(
        state &cs, float_type val, bool do_write = true, bool trigger = true
    );

    /** @brief Set the value of the variable in a raw manner.
     *
     * This will always set the value and ignore any kinds of checks. It will
     * not invoke any triggers either, nor it will save the the value.
     */
    void set_raw_value(float_type val);

    /** @brief Call override for float vars. */
    any_value call(span_type<any_value> args, state &cs);

protected:
    float_var() = default;
};

/** @brief A string variable.
 *
 * A specialization of cubescript::global_var for string values.
 */
struct LIBCUBESCRIPT_EXPORT string_var: global_var {
    /** @brief Set the value of the variable.
     *
     * If read only, an error is raised. If `do_write` is `false`, nothing
     * will be performed other than the read-only checking. If `trigger` is
     * `false`, a potential variable change trigger command will not be
     * invoked. The value is saved with global_var::save(), assuming
     * `do_write` is `true`. After that, integer_var::set_raw_value()
     * is invoked, and then the trigger.
     *
     * @throw cubescript::error if read only or if the changed trigger throws.
     */
    void set_value(
        state &cs, string_ref val, bool do_write = true, bool trigger = true
    );

    /** @brief Set the value of the variable in a raw manner.
     *
     * This will always set the value and ignore any kinds of checks. It will
     * not invoke any triggers either, nor it will save the the value.
     */
    void set_raw_value(string_ref val);

    /** @brief Call override for string vars. */
    any_value call(span_type<any_value> args, state &cs);

protected:
    string_var() = default;
};

/** @brief An alias.
 *
 * An alias is an ident that is created inside the language, for example
 * by assignment. Any variable that you can assign to or look up and is not
 * a builtin is an alias. Aliases don't have special assignment syntax nor
 * they have changed triggers nor value saving. They technically always
 * represent a string within the language, though on C++ side they can
 * have float or integer values too.
 */
struct LIBCUBESCRIPT_EXPORT alias: ident {
    /** @brief Get if this alias represents a function argument.
     *
     * This is true for `argN` aliases representing the arguments passed to
     * the current function.
     */
    bool is_arg() const;

    /** @brief Get the value of the alias for the given thread. */
    any_value value(state &cs) const;

    /** @brief Set the value of the alias for the given thread. */
    void set_value(state &cs, any_value v);

    /** @brief Call an alias.
     *
     * The alias will be called like if it was called in the language.
     */
    any_value call(span_type<any_value> args, state &cs);

protected:
    alias() = default;
};

/** @brief A command.
 *
 * Commands are builtins that can be invoked from the language and have a
 * native implementation registered from C++. Once registered, a command
 * cannot be unregistered or otherwise changed.
 */
struct LIBCUBESCRIPT_EXPORT command: ident {
    /** @brief Get the argument list. */
    std::string_view args() const;

    /** @brief Get the number of arguments the command expects.
     *
     * Only non-variadic arguments count here (i.e. no repeated arguments,
     * no `C`, no `V`; everything else counts as one argument).
     */
    int arg_count() const;

    /** @brief Call a command.
     *
     * The command will be called like if it was called in the language.
     */
    any_value call(span_type<any_value> args, state &cs);

protected:
    command() = default;
};

/** @brief A safe alias handler for commands
 *
 * In general, when dealing with aliases in commands, you do not want to
 * set them directly, since this would set the alias globally. Instead, you
 * can use this to make aliases local to the command.
 *
 * Internally, each Cubescript thread has a mapping for alias state within
 * the thread. This mapping is stack based - which means you can push an
 * alias, and then anything affecting the value of the alias in that thread
 * will only be visible until the stack is popped. This structure provides
 * a safe means of handling the alias stack; constructing it will push the
 * alias, destroying it will pop it.
 *
 * Therefore, what you can do is something like this:
 *
 * ```
 * {
 *     alias_local s{my_thread, "test"};
 *     // branch taken when the alias was successfully pushed
 *     // setting the alias will only be visible within this scope
 *     s.set(some_value); // a convenient setter
 *     my_thread.run(...);
 * }
 * ```
 *
 * If the provided input is not an alias, a cubescript::error will be thrown.
 * Often you don't have to catch it (since this is primarily intended for use
 * within commands, the error will propagate outside your command).
 *
 * Since the goal is to interact tightly with RAII and ensure consistency at
 * all times, it is not possible to copy or move this object. That means you
 * should also not be storing it; it should be used purely as a scope based
 * alias stack manager.
 */
struct LIBCUBESCRIPT_EXPORT alias_local {
    /** @brief Construct the local handler */
    alias_local(state &cs, ident &a);

    /** @brief Construct the local handler
     *
     * The ident will be retrieved using state::new_ident().
     */
    alias_local(state &cs, std::string_view name);

    /** @brief Construct the local handler
     *
     * The ident will be retrieved from the value. If the contained value
     * is not an ident, it will be treated as a name.
     */
    alias_local(state &cs, any_value const &val);

    /** @brief Destroy the local handler */
    ~alias_local();

    /** @brief Local handlers are not copyable */
    alias_local(alias_local const &) = delete;

    /** @brief Local handlers are not movable */
    alias_local(alias_local &&) = delete;

    /** @brief Local handlers are not copy assignable */
    alias_local &operator=(alias_local const &) = delete;

    /** @brief Local handlers are not move assignable */
    alias_local &operator=(alias_local &&v) = delete;

    /** @brief Get the contained alias */
    alias &get_alias() noexcept { return *p_alias; }

    /** @brief Get the contained alias */
    alias const &get_alias() const noexcept { return *p_alias; }

    /** @brief Set the contained alias's value
     *
     * @return `true` if the alias is valid, `false` otherwise
     */
    bool set(any_value val);

private:
    alias *p_alias;
    void *p_sp;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_IDENT_HH */
