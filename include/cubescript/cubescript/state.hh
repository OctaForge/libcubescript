/** @file state.hh
 *
 * @brief State API.
 *
 * The state is the main handle using which you interact with the language
 * from C++. It represents a single Cubescript thread.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_STATE_HH
#define LIBCUBESCRIPT_CUBESCRIPT_STATE_HH

#include <cstddef>
#include <utility>
#include <optional>
#include <functional>
#include <string_view>

#include "callable.hh"
#include "ident.hh"
#include "value.hh"

namespace cubescript {

struct state;

/** @brief The allocator function signature
 *
 * This is the signature of the function pointer passed to do allocations.
 *
 * The first argument is the user data, followed by the old pointer (which
 * is `nullptr` for new allocations and a valid pointer for reallocations
 * and frees). Then follows the original size of the object (zero for new
 * allocations, a valid value for reallocations and frees) and the new
 * size of the object (zero for frees, a valid value for reallocations
 * and new allocations).
 *
 * It must return the new pointer (`nullptr` when freeing) and does not have
 * to throw (the library will throw `std::bad_alloc` itself if it receives
 * a `nullptr` upon allocation).
 *
 * A typical allocation function will look like this:
 *
 * ```
 * void *my_alloc(void *, void *p, std::size_t, std::size_t ns) {
 *     if (!ns) {
 *         std::free(p);
 *         return nullptr;
 *     }
 *     return std::realloc(p, ns);
 * }
 * ```
 */
using alloc_func = void *(*)(void *, void *, size_t, size_t);

/** @brief A call hook function
 *
 * It is possible to set up a call hook for each thread, which is called
 * upon entering the VM. The hook returns nothing and receives the thread
 * reference.
 */
using hook_func = internal::callable<void, state &>;

/** @brief A command function
 *
 * This is how every command looks. It returns nothing and takes the thread
 * reference, a span of input arguments, and a reference to return value.
 */
using command_func = internal::callable<
    void, state &, span_type<any_value>, any_value &
>;

/** @brief The Cubescript thread
 *
 * Represents a Cubescript thread, either the main thread or a side thread
 * depending on how it's created. The state is what you create first and
 * also what you should always destroy last.
 */
struct LIBCUBESCRIPT_EXPORT state {
    /** @brief Create a new Cubescript main thread
     *
     * This creates a main thread without specifying an allocation function,
     * using a simple, builtin implementation. Otherwise it is the same.
     */
    state();

    /** @brief Create a new Cubescript main thread
     *
     * For this variant you have to specify a function used to allocate memory.
     * The optional data will be passed to allocation every time and is your
     * only way to pass custom data to it, since unlike other kinds of hooks,
     * the allocation function is a plain function pointer to ensure it never
     * allocates by itself.
     */
    state(alloc_func func, void *data = nullptr);

    /** @brief Destroy the thread
     *
     * If the thread is a main thread, all state is destroyed. That means
     * main threads should always be destroyed last.
     */
    virtual ~state();

    /** @brief Cubescript threads are not copyable */
    state(state const &) = delete;

    /** @brief Move-construct the Cubescript thread
     *
     * Keep in mind that you should never use `s` after this is done.
     */
    state(state &&s);

    /** @brief Cubescript threads are not copy assignable */
    state &operator=(state const &) = delete;

    /** @brief Move-assign the Cubescript thread
     *
     * Keep in mind that you should never use `s` after this is done.
     * The original `this` is destroyed in the process.
     */
    state &operator=(state &&s);

    /** @brief Swap two Cubescript threads */
    void swap(state &s);

    /** @brief Create a non-main thread
     *
     * This creates a non-main thread. You can also create non-main threads
     * using other non-main threads, but they will always all be dependent
     * on the main thread they originally came from.
     *
     * @return the thread
     */
    state new_thread();

    /** @brief Attach a call hook to the thread
     *
     * The call hook is called every time the VM is entered. You can use
     * this for debugging and other tracking, or say, as a means of
     * interrupting execution from the side in an interactive interpreter.
     */
    template<typename F>
    hook_func call_hook(F &&f) {
        return call_hook(
            hook_func{std::forward<F>(f), callable_alloc, this}
        );
    }

    /** @brief Get a reference to the call hook */
    hook_func const &call_hook() const;

    /** @brief Get a reference to the call hook */
    hook_func &call_hook();

    /** @brief Clear override state for the given ident
     *
     * If the ident is overridden, clear the flag. Global variables will have
     * their value restored to the original, and the changed hook will be
     * triggered. Aliases will be set to an empty string.
     *
     * Other ident types will do nothing.
     */
    void clear_override(ident &id);

    /** @brief Clear override state for all idents.
     *
     * @see clear_override()
     */
    void clear_overrides();

    /** @brief Create a new integer var
     *
     * @param n the name
     * @param v the default value
     * @throw cubescript::error in case of redefinition or invalid name
     */
    integer_var &new_var(
        std::string_view n, integer_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );

    /** @brief Create a new float var
     *
     * @param n the name
     * @param v the default value
     * @throw cubescript::error in case of redefinition or invalid name
     */
    float_var &new_var(
        std::string_view n, float_type v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );

    /** @brief Create a new string var
     *
     * @param n the name
     * @param v the default value
     * @throw cubescript::error in case of redefinition or invalid name
     */
    string_var &new_var(
        std::string_view n, std::string_view v, bool read_only = false,
        var_type vtp = var_type::DEFAULT
    );

    /** @brief Create a new ident
     *
     * If such ident already exists, nothing will be done and a reference
     * will be returned. Otherwise, a new alias will be created and this
     * alias will be returned, however it will not be visible from the
     * language until actually assigned (it does not exist to the language
     * just as is).
     *
     * @param n the name
     * @throw cubescript::error in case of invalid name
     */
    ident &new_ident(std::string_view n);

    /** @brief Get a specific cubescript::ident */
    std::optional<std::reference_wrapper<ident>> get_ident(
        std::string_view name
    );

    /** @brief Assign a value to a name
     *
     * This will set something of the given name to the given value. The
     * something may be a variable or an alias.
     *
     * If no ident of such name exists, a new alias will be created and
     * set.
     *
     * @throw cubescript::error if `name` is a builtin ident (a registered
     * command or similar) or if it is invalid
     *
     * @see lookup_value()
     * @see reset_value()
     * @see touch_value()
     */
    void assign_value(std::string_view name, any_value v);

    /** @brief Lookup a value by name
     *
     * This will lookup an ident of the given name and return its value.
     * The ident may be a variable or an alias.
     *
     * @throw cubescript::error if `name` does not exist or belongs to an
     * ident that doesn't support lookups
     *
     * @see assign_value()
     * @see reset_value()
     * @see touch_value()
     */
    any_value lookup_value(std::string_view name);

    /** @brief Reset a value by name
     *
     * This is like clear_override() except it works by name and performs
     * extra checks.
     *
     * @throw cubescript::error if non-existent or read only
     *
     * @see assign_value()
     * @see lookup_value()
     * @see touch_value()
     */
    void reset_value(std::string_view name);

    /** @brief Touch a value by name
     *
     * If an ident with the given name exists and is a global variable,
     * a changed hook will be triggered with it, acting like if a new
     * value was set, but without actually setting it.
     *
     * @see assign_value()
     * @see lookup_value()
     * @see reset_value()
     */
    void touch_value(std::string_view name);

    /** @brief Register a command
     *
     * This registers a builtin command. A command consists of a valid name,
     * a valid argument list, and a function to call.
     *
     * The argument list is a simple list of types. Currently the following
     * simple types are recognized:
     *
     * * `s` - a string
     * * `i` - an integer
     * * `f` - a float
     * * `a` - any (passed as is)
     * * `b` - bytecode/block
     * * `c` - condition (see below)
     * * `v` - ident (variable/alias/any kind)
     * * `#` - number of real arguments passed up until now
     * * `$` - self ident (the command, except for special hooks)
     *
     * For condition types, the type of the value is generally kept as is,
     * except for non-empty strings, which are compiled as bytecode. Therefore,
     * to check condition types, you first try to get their bytecode; if it is
     * valid, you run it and use its result, otherwise use the value as is,
     * and then evaluate it as a boolean.
     *
     * When an argument is not provided by the caller, it is assigned to none
     * type. Using the appropriate getters on the value structure will get
     * you fallback defaults (e.g. 0 for integer and so on) but you can still
     * check the type explicitly for whether it was actually provided.
     *
     * Commands also support variadics. Variadic commands have their type
     * list suffixed with `...`.
     *
     * If `...` is used alone, the inputs are any arbitrary values. However,
     * they can also be used with repetition. Repetition works for example
     * like `if2...`. The `2` is the number of types to repeat; it must be at
     * most the number of simple types preceeding it. It must be followed by
     * `...`. This specific example means that the variadic arguments are a
     * sequence of integer, float, integer, float, integer, float and so on.
     *
     * The resulting command stores the number of arguments it takes. The
     * variadic part is not a part of it (neither is the part subject to
     * repetition), while all simple types are a part of it (including
     * 'fake' ones like argument count).
     *
     * It is also possible to register special commands. Special commands work
     * like normal ones but are special-purpose. The currently allowed special
     * commands are `//ivar`, `//fvar`, `//svar` and `//var_changed`. These
     * are the only commands where the name can be in this format.
     *
     * The first three are handlers for for global variables, used when either
     * printing or setting them using syntax `varname optional_vals` or using
     * `varname = value`. Their type signature must always start with `$`
     * and can be followed by any user types, generally you will also want
     * to terminate the list with `#` to find out whether any values were
     * passed.
     *
     * This way you can have custom handlers for printing as well as custom
     * syntaxes for setting (e.g. your custom integer var handler may want to
     * take up to 4 values to allow setting of RGBA color channels). When no
     * arguments are passed (checked using `#`) you will want to print the
     * value using a format you want. When using the `=` assignment syntax,
     * one value is passed.
     *
     * There are builtin default handlers that take at most one arg (`i`, `f`
     * and `s`) which also print to standard output (`name = value`).
     *
     * For `//var_changed`, there is no default handler. The arg list must be
     * `$aa`. This will be called whenever the value of a builtin variable of
     * any type changes, and will be passed the variable as its first argument,
     * the previous value as the second argument and the new value as the third
     * argument (mainly for convenience).
     *
     * For these builtins, `$` will refer to the variable ident, not to the
     * builtin command.
     *
     * @throw cubescript::error upon redefinition, invalid name or arg list
     */
    template<typename F>
    command &new_command(
        std::string_view name, std::string_view args, F &&f
    ) {
        return new_command(
            name, args,
            command_func{std::forward<F>(f), callable_alloc, this}
        );
    }

    /** @brief Get a span of all idents */
    span_type<ident *> get_idents();

    /** @brief Get a span of all idents */
    span_type<ident const *> get_idents() const;

    /** @brief Compile a string.
     *
     * This compiles the given string, optionally using `source` as a filename
     * for debug information (useful when implementing file I/O functions).
     *
     * @return a bytecode reference
     * @throw cubescript::error on compilation failure
     */
    bcode_ref compile(
        std::string_view v, std::string_view source = std::string_view{}
    );

    /** @brief Get if the thread is in override mode
     *
     * If the thread is in override mode, any assigned alias or variable will
     * be given the overridden flag, with variables also saving their old
     * value. Upon clearing the flag (using clear_override() or similar)
     * the old value will be restored (aliases will be set to an empty
     * string).
     *
     * Overridable variables will always act like if the thread is in override
     * mode, even if it's not.
     *
     * Keep in mind that if an alias is pushed, its flags will be cleared once
     * popped.
     *
     * @see override_mode()
     */
    bool override_mode() const;

    /** @brief Set the thread's override mode */
    bool override_mode(bool v);

    /** @brief Get if the thread is in persist most
     *
     * In persist mode, newly assigned aliases will have the persist flag
     * set on them, which is an indicator that they should be saved to disk
     * like persistent variables. The library does no saving, so by default
     * it works as an indicator for the user.
     *
     * Keep in mind that if an alias is pushed, its flags will be cleared once
     * popped.
     */
    bool persist_mode() const;

    /** @brief Set the thread's persist mode */
    bool persist_mode(bool v);

    /** @brief Get the maximum call depth of the VM
     *
     * If zero, it is unlimited, otherwise it specifies how much the VM is
     * allowed to recurse. By default, it is zero.
     */
    std::size_t max_call_depth() const;

    /** @brief Set the maximum call depth ov the VM
     *
     * If zero, it is unlimited (this is the default). You can limit how much
     * the VM is allowed to recurse if you have specific constraints to adhere
     * to.
     *
     * @return the old value
     */
    std::size_t max_call_depth(std::size_t v);

private:
    friend struct state_p;

    LIBCUBESCRIPT_LOCAL state(void *is);

    hook_func call_hook(hook_func func);

    command &new_command(
        std::string_view name, std::string_view args, command_func func
    );

    static void *callable_alloc(
        void *data, void *p, std::size_t os, std::size_t ns
    ) {
        return static_cast<state *>(data)->alloc(p, os, ns);
    }

    void *alloc(void *ptr, size_t olds, size_t news);

    struct thread_state *p_tstate = nullptr;
};

/** @brief Initialize the base library
 *
 * You can choose which parts of the standard library you include in your
 * program. The base library contains core constructs for things such as
 * error handling, conditionals, looping, and var/alias management.
 *
 * Calling this multiple times has no effect; commands will only be
 * registered once.
 *
 * @see cubescript::std_init_math()
 * @see cubescript::std_init_string()
 * @see cubescript::std_init_list()
 * @see cubescript::std_init_all()
 */
LIBCUBESCRIPT_EXPORT void std_init_base(state &cs);

/** @brief Initialize the math library
 *
 * You can choose which parts of the standard library you include in your
 * program. The math library contains arithmetic and other math related
 * functions.
 *
 * Calling this multiple times has no effect; commands will only be
 * registered once.
 *
 * @see cubescript::std_init_base()
 * @see cubescript::std_init_string()
 * @see cubescript::std_init_list()
 * @see cubescript::std_init_all()
 */
LIBCUBESCRIPT_EXPORT void std_init_math(state &cs);

/** @brief Initialize the string library
 *
 * You can choose which parts of the standard library you include in your
 * program. The string library contains commands to manipulate strings.
 *
 * Calling this multiple times has no effect; commands will only be
 * registered once.
 *
 * @see cubescript::std_init_base()
 * @see cubescript::std_init_math()
 * @see cubescript::std_init_list()
 * @see cubescript::std_init_all()
 */
LIBCUBESCRIPT_EXPORT void std_init_string(state &cs);

/** @brief Initialize the list library
 *
 * You can choose which parts of the standard library you include in your
 * program. The list library contains commands to manipulate lists.
 *
 * Calling this multiple times has no effect; commands will only be
 * registered once.
 *
 * @see cubescript::std_init_base()
 * @see cubescript::std_init_math()
 * @see cubescript::std_init_string()
 * @see cubescript::std_init_all()
 */
LIBCUBESCRIPT_EXPORT void std_init_list(state &cs);

/** @brief Initialize all standard libraries
 *
 * This is like calling each of the individual standard library init
 * functions and exists mostly just for convenience.

 * @see cubescript::std_init_base()
 * @see cubescript::std_init_math()
 * @see cubescript::std_init_string()
 * @see cubescript::std_init_list()
 */
LIBCUBESCRIPT_EXPORT void std_init_all(state &cs);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_STATE_HH */
