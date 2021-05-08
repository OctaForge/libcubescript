/** @file value.hh
 *
 * @brief Value API.
 *
 * This file contains value handles. These include the main value handle,
 * which represents any Cubescript value as a tagged union (and you use it
 * for handling of things such as command arguments and return values), as
 * well as string references and bytecode references.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH
#define LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH

#include <cstddef>
#include <string_view>
#include <new>

namespace cubescript {

struct ident;
struct any_value;

/** @brief The loop state
 *
 * This is returned by state::call_loop().
 */
enum class loop_state {
    NORMAL = 0, /**< @brief The iteration ended normally. */
    BREAK,      /**< @brief The iteration was broken out of. */
    CONTINUE    /**< @brief The iteration ended early. */
};

/** @brief Bytecode reference.
 *
 * This is an object representing a bytecode reference. Bytecode references
 * are executable in a Cubescript thread. The typical way to get a bytecode
 * reference is as an argument to a command. You can also compile values
 * explicitly.
 *
 * Bytecode references use refcounting to maintain their lifetime, so once
 * you hold a reference, it will not be freed at very least until you are
 * done with it and the object is destroyed.
 *
 * The API does not expose any specifics of the bytecode format either.
 * This is an implementation detail; bytecode is also not meant to be
 * serialized and stored on disk (it's not guaranteed to be portable).
 */
struct LIBCUBESCRIPT_EXPORT bcode_ref {
    /** @brief Initialize a null reference.
     *
     * Null references can still be executed, but will not do anything.
     */
    bcode_ref():
        p_code(nullptr)
    {}

    /** @brief Copy a reference.
     *
     * A reference copy will increase the internal reference count and will
     * point to the same bytecode.
     */
    bcode_ref(bcode_ref const &v);

    /** @brief Move a reference.
     *
     * A reference move will not change the internal reference count; the
     * other reference will become a null reference.
     */
    bcode_ref(bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    /** @brief Destroy a reference.
     *
     * This will decrease the reference count, and if it's zero, free the
     * bytecode.
     */
    ~bcode_ref();

    /** @brief Copy-assign a reference.
     *
     * A reference copy will increase the internal reference count and will
     * point to the same bytecode.
     */
    bcode_ref &operator=(bcode_ref const &v);

    /** @brief Move-assign a reference.
     *
     * A reference move will not change the internal reference count; the
     * other reference will become a null reference.
     */
    bcode_ref &operator=(bcode_ref &&v);

    /** @brief Check if the bytecode is empty.
     *
     * Empty bytecode does not mean the same thing as null bytecode. While
     * null bytecode is considered empty, there can be non-null empty
     * bytecode; that is, a bytecode representing what an empty string
     * would compile into.
     */
    bool empty() const;

    /** @brief Check if the bytecode is null.
     *
     * This only checks if the bytecode is null. In general, you will want
     * to use bcode_ref::empty() instead.
     */
    explicit operator bool() const;

    /** @brief Execute the bytecode
     *
     * @return the return value
     */
    any_value call(state &cs) const;

    /** @brief Execute the bytecode as a loop body
     *
     * This exists to implement custom loop commands. A loop command will
     * consist of your desired loop and will take a body as an argument
     * (with bytecode type); this body will be run using this API. The
     * return value can be used to check if the loop was broken out of
     * or continued, and take steps accordingly.
     *
     * Some loops may evaluate to values, while others may not.
     */
    loop_state call_loop(state &cs, any_value &ret) const;

    /** @brief Execute the byctecode as a loop body
     *
     * This version ignores the return value of the body.
     */
    loop_state call_loop(state &cs) const;

private:
    friend struct bcode_p;

    bcode_ref(struct bcode *v);

    struct bcode *p_code;
};

/** @brief String reference.
 *
 * This Cubescript implementation uses interned strings everywhere in the
 * language and the API. This means every string in the language exists in
 * exactly one copy - if you try to create another string with the same
 * contents, it will point to the same data. This structure represents
 * a single reference to such string. By providing a reference counting
 * mechanism, it is possible to manage strings in a memory-safe manner.
 *
 * There is also no such thing as a null reference in this case. If you
 * have a string reference, it always points to a valid string no matter
 * what.
 *
 * It is not safe to have string references still around after the main
 * Cubescript thread is destroyed. Therefore, you should always make sure
 * that all string references you are holding are gone by the time the
 * main thread calls its destructor.
 *
 * For compatibility, all strings that are pointed to by string references
 * are null terminated and therefore can be used with C-style APIs.
 */
struct LIBCUBESCRIPT_EXPORT string_ref {
    friend struct any_value;
    friend struct string_pool;

    /** @brief String references are not default-constructible. */
    string_ref() = delete;

    /** @brief Create a new string reference.
     *
     * You need to provide a thread and a view of the string you wish
     * to create a reference for. The implementation will then ensure
     * that either a new string is allocated internally, or an existing
     * string's reference count is incremented.
     */
    string_ref(state &cs, std::string_view str);

    /** @brief Copy a string reference.
     *
     * This will increase the reference count for the pointed-to string.
     * There is explicitly no moving as this would create null references.
     */
    string_ref(string_ref const &ref);

    /** @brief Destroy a string reference.
     *
     * This will decrease the reference count. If it becomes zero, appropriate
     * actions will be taken (the exact behavior is implementation-defined).
     *
     * It is not safe to call this after destruction of the main thread.
     */
    ~string_ref();

    /** @brief Copy-assign a string reference.
     *
     * This will increase the reference count for the pointed-to string.
     * There is explicitly no moving as this would create null references.
     */
    string_ref &operator=(string_ref const &ref);

    /** @brief Get a view to the pointed-to string.
     *
     * This creates a view of the string. The view is not its own reference,
     * therefore it is possible it will get invalidated after the destructor
     * of this reference is called.
     */
    operator std::string_view() const;

    /** @brief A convenience wrapper to get the size.
     *
     * Like `view().size()`.
     */
    std::size_t size() const {
        return view().size();
    }

    /** @brief A convenience wrapper to get the length.
     *
     * Like `view().length()`.
     */
    std::size_t length() const {
        return view().length();
    }

    /** @brief Get a C string pointer.
     *
     * The pointer may become dangling once the reference is destroyed.
     * The string itself is always null terminated.
     */
    char const *data() const;

    /** @brief A convenience wrapper to get the view.
     *
     * Since instantiating a view can be ugly, this is a quick method
     * to get a view of a random string reference.
     */
    std::string_view view() const {
        return std::string_view{*this};
    }

    /** @brief Check if the string is empty. */
    bool empty() const {
        return (size() == 0);
    }

    /** @brief Check if the string equals another.
     *
     * This is effectively a `data() == s.data()` address comparison, and
     * therefore always has constant time complexity.
     */
    bool operator==(string_ref const &s) const;

    /** @brief Check if the string does not equal another.
     *
     * This is effectively a `data() != s.data()` address comparison, and
     * therefore always has constant time complexity.
     */
    bool operator!=(string_ref const &s) const;

private:
    string_ref(char const *p);

    char const *p_str;
};

/** @brief The type of a value.
 *
 * The cubescript::any_value structure can hold multiple types. Not all of
 * them are representable in the language.
 */
enum class value_type {
    NONE = 0, /**< @brief No value. */
    INTEGER,  /**< @brief Integer value (cubescript::integer_type). */
    FLOAT,    /**< @brief Floating point value (cubescript::float_type). */
    STRING,   /**< @brief String value (cubescript::string_ref). */
    CODE,     /**< @brief Bytecode value (cubescript::bcode_ref). */
    IDENT     /**< @brief Ident value (cubescript::ident). */
};

/** @brief A tagged union representing a value.
 *
 * This structure is used to represent argument and result types of commands
 * as well as values of aliases. When assigned to an alias, the only value
 * must not contain bytecode or an ident reference, as those cannot be
 * represented as values in the language.
 *
 * Of course, to the language, every value looks like a string. It is however
 * still possible to differentiate them on C++ side for better performance,
 * more efficient storage and greater convenience.
 *
 * When the value contains a string or bytecode, it holds a reference like
 * cubescript::string_ref or cubescript::bcode_ref would.
 *
 * Upon setting different types, the old type will get cleared, which may
 * include a reference count decrease.
 */
struct LIBCUBESCRIPT_EXPORT any_value {
    /** @brief Construct a value_type::NONE value. */
    any_value();

    /** @brief Construct a value_type::INTEGER value. */
    any_value(integer_type val);

    /** @brief Construct a value_type::FLOAT value. */
    any_value(float_type val);

    /** @brief Construct a value_type::STRING value. */
    any_value(std::string_view val, state &cs);

    /** @brief Construct a value_type::STRING value. */
    any_value(string_ref const &val);

    /** @brief Construct a value_type::CODE value. */
    any_value(bcode_ref const &val);

    /** @brief Construct a value_type::IDENT value. */
    any_value(ident &val);

    /** @brief Destroy the value.
     *
     * If holding a reference counted value, the refcount will be decreased
     * and the value will be possibly freed.
     */
    ~any_value();

    /** @brief Copy the value. */
    any_value(any_value const &);

    /** @brief Move the value.
     *
     * The other value becomes a value_type::NULL value.
     */
    any_value(any_value &&v);

    /** @brief Copy-assign the value. */
    any_value &operator=(any_value const &);

    /** @brief Move-assign the value.
     *
     * The other value becomes a value_type::NULL value.
     */
    any_value &operator=(any_value &&);

    /** @brief Assign an integer to the value. */
    any_value &operator=(integer_type val);

    /** @brief Assign a float to the value. */
    any_value &operator=(float_type val);

    /** @brief Assign a string reference to the value. */
    any_value &operator=(string_ref const &val);

    /** @brief Assign a bytecode reference to the value. */
    any_value &operator=(bcode_ref const &val);

    /** @brief Assign an ident to the value. */
    any_value &operator=(ident &val);

    /** @brief Get the type of the value. */
    value_type type() const;

    /** @brief Set the value to an integer.
     *
     * The type becomes value_type::INTEGER.
     */
    void set_integer(integer_type val);

    /** @brief Set the value to a float.
     *
     * The type becomes value_type::FLOAT.
     */
    void set_float(float_type val);

    /** @brief Set the value to a string.
     *
     * The type becomes value_type::STRING. The string will be allocated
     * (if non-existent) like a cubescript::string_ref, and its reference
     * count will be increased. This is why it is necessary to provide a state.
     */
    void set_string(std::string_view val, state &cs);

    /** @brief Set the value to a string reference.
     *
     * The type becomes value_type::STRING. The value will get copied
     * (therefore, the reference count will be increased).
     */
    void set_string(string_ref const &val);

    /** @brief Set the value to a value_type::NONE. */
    void set_none();

    /** @brief Set the value to a bytecode reference.
     *
     * The type becomes value_type::CODE. The value will get copied
     * (therefore, the reference count will be increased).
     */
    void set_code(bcode_ref const &val);

    /** @brief Set the value to an indent.
     *
     * The type becomes value_type::IDENT. No reference counting is
     * performed, so after main thread destruction this may become
     * dangling (and unsafe to use).
     */
    void set_ident(ident &val);

    /** @brief Get the value as a string reference.
     *
     * If the contained value is not a string, an appropriate conversion
     * will occur. This will not affect the contained type, all conversions
     * are only intermediate.
     *
     * If the type is not convertible, an empty string is used.
     */
    string_ref get_string(state &cs) const;

    /** @brief Get the value as an integer.
     *
     * If the contained value is not an integer, an appropriate conversion
     * will occur. This will not affect the contained type, all conversions
     * are only intermediate.
     *
     * Floating point values are rounded down and converted to integers.
     *
     * If the type is not convertible, 0 is returned.
     */
    integer_type get_integer() const;

    /** @brief Get the value as a float.
     *
     * If the contained value is not a float, an appropriate conversion
     * will occur. This will not affect the contained type, all conversions
     * are only intermediate.
     *
     * If the type is not convertible, 0 is returned.
     */
    float_type get_float() const;

    /** @brief Get the value as a bytecode.
     *
     * If the contained value is not bytecode, null bytecode is returned.
     */
    bcode_ref get_code() const;

    /** @brief Get the value as an ident.
     *
     * If the contained value is not an ident, a dummy is returned.
     */
    ident &get_ident(state &cs) const;

    /** @brief Get the value as representable inside the language.
     *
     * The returned value is the same value except if the original contents
     * were bytecode or an ident - in those cases the returned type is
     * value_type::NONE.
     */
    any_value get_plain() const;

    /** @brief Get the value converted to a boolean.
     *
     * For integer and float values, anything other than zero will become
     * `true`, while zero becomes `false`. Empty strings are `false`; other
     * strings first attempt conversion to an integer - if it's convertible
     * (strong conversion rules apply), it's treated like an integer. If it
     * is not, it's converted to a float (strong rules apply) and if it is
     * convertible, it's treated like a float. Any non-integer non-float
     * string is considered `true`.
     *
     * For any other type, `false` is returned.
     */
    bool get_bool() const;

    /** @brief Force the type to value_type::NONE.
     *
     * Like set_none().
     */
    void force_none();

    /** @brief Force the type to be representable in the language.
     *
     * Like `*this = get_plain()`.
     */
    void force_plain();

    /** @brief Force the type to value_type::FLOAT.
     *
     * Like `set_float(get_float())`.
     *
     * @return The value.
     */
    float_type force_float();

    /** @brief Force the type to value_type::INTEGER.
     *
     * Like `set_integer(get_integer())`.
     *
     * @return The value.
     */
    integer_type force_integer();

    /** @brief Force the type to value_type::STRING.
     *
     * Like `set_string(get_string(cs))`.
     *
     * @return A view to the string.
     */
    std::string_view force_string(state &cs);

    /** @brief Force the type to value_type::CODE.
     *
     * If the contained value is already bytecode, nothing happens. Otherwise
     * the value is converted to a string (like get_string()) and this string
     * is compiled as bytecode (as if using state::compile())
     *
     * @return A bytecode reference.
     */
    bcode_ref force_code(
        state &cs, std::string_view source = std::string_view{}
    );

    /** @brief Force the type to value_type::IDENT.
     *
     * If the contained value is already an ident, nothing happens. Otherwise
     * the value is converted to a string (like get_string()) and this string
     * is used as a name of the ident. If an ident of such name exists, it
     * will be stored, otherwise a new alias is pre-created (it will not be
     * visible to the language until a value is assigned to it though).
     *
     * @return An ident reference.
     */
    ident &force_ident(state &cs);

private:
    union {
        integer_type i;
        float_type f;
        char const *s;
        struct bcode *b;
        ident *v;
    } p_stor;
    value_type p_type;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH */
