#ifndef LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH
#define LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH

#include <cstddef>
#include <string_view>
#include <new>

namespace cubescript {

struct internal_state;
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
    string_ref(internal_state *cs, std::string_view str);
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

    char const *data() const {
        return std::string_view{*this}.data();
    }

    std::string_view view() const {
        return std::string_view{*this};
    }

    bool empty() const {
        return (size() == 0);
    }

    bool operator==(string_ref const &s) const;

private:
    /* for internal use only */
    string_ref(char const *p, internal_state *cs);

    internal_state *p_state;
    char const *p_str;
};

enum class value_type {
    NONE = 0, INTEGER, FLOAT, STRING, CODE, IDENT
};

struct LIBCUBESCRIPT_EXPORT any_value {
    any_value() = delete;
    ~any_value();

    any_value(state &);
    any_value(internal_state &);

    any_value(any_value const &);
    any_value(any_value &&v);

    any_value &operator=(any_value const &);
    any_value &operator=(any_value &&);

    value_type get_type() const;

    void set_integer(integer_type val);
    void set_float(float_type val);
    void set_string(std::string_view val);
    void set_string(string_ref const &val);
    void set_none();
    void set_code(bcode_ref const &val);
    void set_ident(ident *val);

    string_ref get_string() const;
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
    std::string_view force_string();
    bcode_ref force_code(state &cs);
    ident &force_ident(state &cs);

private:
    template<typename T>
    struct stor_t {
        internal_state *state;
        T val;
    };

    internal_state *get_state() const {
        return std::launder(
            reinterpret_cast<stor_t<void *> const *>(&p_stor)
        )->state;
    }

    std::aligned_union_t<1,
        stor_t<integer_type>,
        stor_t<float_type>,
        stor_t<void *>,
        string_ref
    > p_stor;
    value_type p_type;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_VALUE_HH */
