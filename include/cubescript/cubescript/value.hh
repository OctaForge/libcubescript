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

struct LIBCUBESCRIPT_EXPORT bcode_ref {
    bcode_ref():
        p_code(nullptr)
    {}
    bcode_ref(bcode_ref const &v);
    bcode_ref(bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~bcode_ref();

    bcode_ref &operator=(bcode_ref const &v);
    bcode_ref &operator=(bcode_ref &&v);

    bool empty() const;
    operator bool() const;

private:
    friend struct bcode_p;

    bcode_ref(struct bcode *v);

    struct bcode *p_code;
};

struct LIBCUBESCRIPT_EXPORT string_ref {
    friend struct any_value;
    friend struct string_pool;

    string_ref() = delete;
    string_ref(state &cs, std::string_view str);

    string_ref(string_ref const &ref);

    ~string_ref();

    string_ref &operator=(string_ref const &ref);

    operator std::string_view() const;

    std::size_t size() const {
        return std::string_view{*this}.size();
    }
    std::size_t length() const {
        return std::string_view{*this}.length();
    }

    char const *data() const;

    std::string_view view() const {
        return std::string_view{*this};
    }

    bool empty() const {
        return (size() == 0);
    }

    bool operator==(string_ref const &s) const;

private:
    string_ref(char const *p);

    char const *p_str;
};

enum class value_type {
    NONE = 0, INTEGER, FLOAT, STRING, CODE, IDENT
};

struct LIBCUBESCRIPT_EXPORT any_value {
    any_value();

    ~any_value();

    any_value(any_value const &);
    any_value(any_value &&v);

    any_value &operator=(any_value const &);
    any_value &operator=(any_value &&);

    value_type get_type() const;

    void set_integer(integer_type val);
    void set_float(float_type val);
    void set_string(std::string_view val, state &cs);
    void set_string(string_ref const &val);
    void set_none();
    void set_code(bcode_ref const &val);
    void set_ident(ident *val);

    string_ref get_string(state &cs) const;
    integer_type get_integer() const;
    float_type get_float() const;
    bcode_ref get_code() const;
    ident *get_ident() const;
    any_value get_plain() const;

    bool get_bool() const;

    void force_none();
    void force_plain();
    float_type force_float();
    integer_type force_integer();
    std::string_view force_string(state &cs);
    bcode_ref force_code(state &cs);
    ident &force_ident(state &cs);

private:
    std::aligned_union_t<1, integer_type, float_type, void *> p_stor;
    value_type p_type;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH */
