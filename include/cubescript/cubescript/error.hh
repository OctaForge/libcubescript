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

namespace cubescript {

struct state;

/** @brief Represents a Cubescript error.
 *
 * This is a standard error that can be thrown by either the Cubescript APIs
 * or from the language itself (either by the user, or by incorrect use of
 * the API).
 *
 * It has a message attached, as well as the current state of the call stack,
 * represented as cubescript::stack_state.
 *
 * Each Cubescript thread internally stores a buffer for the error message,
 * which is reused for each error raised from that thread.
 */
struct LIBCUBESCRIPT_EXPORT error {
    /** @brief A node in the call stack.
     *
     * The nodes are indexed. The bottommost node has index 1, the topmost
     * node has index N (where N is the number of levels the call stack has).
     *
     * There can be a gap in the stack (i.e. the bottommost node will have
     * index 1 and the one above it greater than 2). The gap is controlled
     * by the value of the `dbgalias` cubescript variable at the time of
     * creation of the error (the stack list will contain at most N nodes).
     */
    struct stack_node {
        stack_node const *next; /**< @brief Next level. */
        struct ident const *id; /**< @brief The ident of this level. */
        std::size_t index; /**< @brief The level index. */
    };

    error() = delete;
    error(error const &) = delete;

    /** @brief Errors are move constructible. */
    error(error &&v):
        p_errbeg{v.p_errbeg}, p_errend{v.p_errend},
        p_stack{std::move(v.p_stack)}, p_state{v.p_state}
    {}

    /** @brief Construct an error using a string. */
    error(state &cs, std::string_view msg);

    /** @brief Destroy the error. */
    ~error();

    /** @brief Get a view of the error message. */
    std::string_view what() const {
        return std::string_view{p_errbeg, std::size_t(p_errend - p_errbeg)};
    }

    /** @brief Get a reference to the call stack state. */
    stack_node *stack() {
        return p_stack;
    }

    /** @brief Get a reference to the call stack state. */
    stack_node const *stack() const {
        return p_stack;
    }
private:
    friend struct error_p;

    error(state &cs, char const *errbeg, char const *errend);

    char const *p_errbeg, *p_errend;
    stack_node *p_stack;
    state *p_state;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_ERROR_HH */
