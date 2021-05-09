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

/** @brief Represents the simplified call stack at a point in time.
 *
 * This is a simplified call stack; it is generally carried by errors
 * and can be utilized to print out a stack trace.
 *
 * The actual stack is represented as a linked list of nodes. There can
 * be a gap in the list, if the user has limited the maximum debug depth
 * with the `dbgalias` cubescript variable; the bottommost node will always
 * represent the bottom of the stack, while the nodes above it will be the
 * rest of the stack or a part of the stack starting from the top.
 */
struct LIBCUBESCRIPT_EXPORT stack_state {
    /** @brief A node in the call stack.
     *
     * The nodes are indexed. The bottommost node has index 1, the topmost
     * node has index N (where N is the number of levels the call stack has).
     */
    struct node {
        node const *next; /**< @brief Next level. */
        struct ident const *id; /**< @brief The ident of this level. */
        std::size_t index; /**< @brief The level index. */
    };

    stack_state() = delete;

    /** @brief Construct the stack state. */
    stack_state(state &cs, node *nd = nullptr);

    stack_state(stack_state const &) = delete;

    /** @brief Move the stack state somewhere else.
     *
     * Stack states are movable, but not copyable.
     */
    stack_state(stack_state &&st);

    /** @brief Destroy the stack state. */
    ~stack_state();

    stack_state &operator=(stack_state const &) = delete;

    /** @brief Move-assign the stack state somewhere else.
     *
     * Stack states are move assignable, but not copy assignable.
     */
    stack_state &operator=(stack_state &&);

    /** @brief Get the pointer to the topmost (current) level. */
    node const *get() const;

private:
    state &p_state;
    node *p_node;
};

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
    friend struct state;

    error() = delete;
    error(error const &) = delete;

    /** @brief Errors are move constructible. */
    error(error &&v):
        p_errbeg{v.p_errbeg}, p_errend{v.p_errend},
        p_stack{std::move(v.p_stack)}
    {}

    /** @brief Construct an error using a string. */
    error(state &cs, std::string_view msg);

    /** @brief Get a view of the error message. */
    std::string_view what() const {
        return std::string_view{p_errbeg, std::size_t(p_errend - p_errbeg)};
    }

    /** @brief Get a reference to the call stack state. */
    stack_state &stack() {
        return p_stack;
    }

    /** @brief Get a reference to the call stack state. */
    stack_state const &stack() const {
        return p_stack;
    }
private:
    friend struct error_p;

    error(state &cs, char const *errbeg, char const *errend);

    char const *p_errbeg, *p_errend;
    stack_state p_stack;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_ERROR_HH */
