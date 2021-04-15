/** @file util.hh
 *
 * @brief Utility API.
 *
 * This contains various utilities that don't quite fit within the other
 * structures, but provide convenience; this includes things such as parsing
 * of lists, strings and numbers.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_UTIL_HH
#define LIBCUBESCRIPT_CUBESCRIPT_UTIL_HH

#include <cstddef>
#include <string_view>
#include <algorithm>

#include "ident.hh"

namespace cubescript {

struct LIBCUBESCRIPT_EXPORT alias_local {
    alias_local(state &cs, ident *a);
    ~alias_local();

    alias_local(alias_local const &) = delete;
    alias_local(alias_local &&) = delete;

    alias_local &operator=(alias_local const &) = delete;
    alias_local &operator=(alias_local &&v) = delete;

    alias *get_alias() noexcept { return p_alias; }
    alias const *get_alias() const noexcept { return p_alias; }

    bool set(any_value val);

    explicit operator bool() const noexcept;

private:
    alias *p_alias;
    void *p_sp;
};

struct LIBCUBESCRIPT_EXPORT list_parser {
    list_parser(state &cs, std::string_view s = std::string_view{}):
        p_state{&cs}, p_input_beg{s.data()}, p_input_end{s.data() + s.size()}
     {}

    void set_input(std::string_view s) {
        p_input_beg = s.data();
        p_input_end = s.data() + s.size();
    }

    std::string_view get_input() const {
        return std::string_view{
            p_input_beg, std::size_t(p_input_end - p_input_beg)
        };
    }

    bool parse();
    std::size_t count();

    string_ref get_item() const;

    std::string_view get_raw_item() const {
        return std::string_view{p_ibeg, std::size_t(p_iend - p_ibeg)};
    }
    std::string_view get_quoted_item() const {
        return std::string_view{p_qbeg, std::size_t(p_qend - p_qbeg)};
    }

    void skip_until_item();

private:
    state *p_state;
    char const *p_input_beg, *p_input_end;

    char const *p_ibeg{}, *p_iend{};
    char const *p_qbeg{}, *p_qend{};
};


LIBCUBESCRIPT_EXPORT char const *parse_string(
    state &cs, std::string_view str, size_t &nlines
);

inline char const *parse_string(
    state &cs, std::string_view str
) {
    size_t nlines;
    return parse_string(cs, str, nlines);
}

LIBCUBESCRIPT_EXPORT char const *parse_word(
    state &cs, std::string_view str
);

LIBCUBESCRIPT_EXPORT string_ref concat_values(
    state &cs, span_type<any_value> vals,
    std::string_view sep = std::string_view{}
);

template<typename R>
inline R escape_string(R writer, std::string_view str) {
    *writer++ = '"';
    for (auto c: str) {
        switch (c) {
            case '\n': *writer++ = '^'; *writer++ = 'n'; break;
            case '\t': *writer++ = '^'; *writer++ = 't'; break;
            case '\f': *writer++ = '^'; *writer++ = 'f'; break;
            case  '"': *writer++ = '^'; *writer++ = '"'; break;
            case  '^': *writer++ = '^'; *writer++ = '^'; break;
            default: *writer++ = c; break;
        }
    }
    *writer++ = '"';
    return writer;
}

template<typename R>
inline R unescape_string(R writer, std::string_view str) {
    for (auto it = str.begin(); it != str.end(); ++it) {
        if (*it == '^') {
            ++it;
            if (it == str.end()) {
                break;
            }
            switch (*it) {
                case 'n': *writer++ = '\n'; break;
                case 't': *writer++ = '\r'; break;
                case 'f': *writer++ = '\f'; break;
                case '"': *writer++ = '"'; break;
                case '^': *writer++ = '^'; break;
                default: *writer++ = *it; break;
            }
        } else if (*it == '\\') {
            ++it;
            if (it == str.end()) {
                break;
            }
            char c = *it;
            if ((c == '\r') || (c == '\n')) {
                if ((c == '\r') && ((it + 1) != str.end())) {
                    if (it[1] == '\n') {
                        ++it;
                    }
                }
                continue;
            }
            *writer++ = '\\';
        } else {
            *writer++ = *it;
        }
    }
    return writer;
}

template<typename R>
inline R print_stack(R writer, stack_state const &st) {
    char buf[32] = {0};
    auto nd = st.get();
    while (nd) {
        auto name = nd->id->get_name();
        *writer++ = ' ';
        *writer++ = ' ';
        if ((nd->index == 1) && st.gap()) {
            *writer++ = '.';
            *writer++ = '.';
        }
        snprintf(buf, sizeof(buf), "%d", nd->index);
        char const *p = buf;
        std::copy(p, p + strlen(p), writer);
        *writer++ = ')';
        std::copy(name.begin(), name.end(), writer);
        nd = nd->next;
        if (nd) {
            *writer++ = '\n';
        }
    }
    return writer;
}

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_UTIL_HH */
