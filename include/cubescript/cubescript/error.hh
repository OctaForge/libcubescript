/** @file error.hh
 *
 * @brief Error handling API.
 *
 * Defines structures and methods used for error handling in the library.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_ERROR_HH
#define LIBCUBESCRIPT_CUBESCRIPT_ERROR_HH

#include <string_view>
#include <stdexcept>
#include <utility>
#include <cstdlib>
#include <cstring>

namespace cubescript {

struct state;

struct internal_error: std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct LIBCUBESCRIPT_EXPORT stack_state {
    struct node {
        node const *next;
        struct ident const *id;
        int index;
    };

    stack_state() = delete;
    stack_state(state &cs, node *nd = nullptr, bool gap = false);
    stack_state(stack_state const &) = delete;
    stack_state(stack_state &&st);
    ~stack_state();

    stack_state &operator=(stack_state const &) = delete;
    stack_state &operator=(stack_state &&);

    node const *get() const;
    bool gap() const;

private:
    state &p_state;
    node *p_node;
    bool p_gap;
};

struct LIBCUBESCRIPT_EXPORT error {
    friend struct state;

    error() = delete;
    error(error const &) = delete;
    error(error &&v):
        p_errbeg{v.p_errbeg}, p_errend{v.p_errend},
        p_stack{std::move(v.p_stack)}
    {}

    std::string_view what() const {
        return std::string_view{p_errbeg, std::size_t(p_errend - p_errbeg)};
    }

    stack_state &get_stack() {
        return p_stack;
    }

    stack_state const &get_stack() const {
        return p_stack;
    }

    error(state &cs, std::string_view msg):
        p_errbeg{}, p_errend{}, p_stack{cs}
    {
        char *sp;
        char *buf = request_buf(cs, msg.size(), sp);
        std::memcpy(buf, msg.data(), msg.size());
        buf[msg.size()] = '\0';
        p_errbeg = sp;
        p_errend = buf + msg.size();
        p_stack = save_stack(cs);
    }

    template<typename ...A>
    error(state &cs, std::string_view msg, A const &...args):
        p_errbeg{}, p_errend{}, p_stack{cs}
    {
        std::size_t sz = msg.size() + 64;
        char *buf, *sp;
        for (;;) {
            buf = request_buf(cs, sz, sp);
            int written = std::snprintf(buf, sz, msg.data(), args...);
            if (written <= 0) {
                throw internal_error{"format error"};
            } else if (std::size_t(written) <= sz) {
                break;
            }
            sz = std::size_t(written);
        }
        p_errbeg = sp;
        p_errend = buf + sz;
        p_stack = save_stack(cs);
    }

private:
    stack_state save_stack(state &cs);
    char *request_buf(state &cs, std::size_t bufs, char *&sp);

    char const *p_errbeg, *p_errend;
    stack_state p_stack;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_ERROR_HH */
