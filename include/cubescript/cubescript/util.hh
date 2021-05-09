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

/** @brief A list parser
 *
 * Cubescript does not have data structures and everything is a string.
 * However, you can represent lists as strings; there is a standard syntax
 * to them.
 *
 * A list in Cubescript is simply a bunch of items separated by whitespace.
 * The items can take the form of any literal value Cubescript has. That means
 * they can be number literals, they can be words, and they can be strings.
 * Strings can be quoted either with double quotes, square brackets or even
 * parenthesis; basically any syntax representing a value.
 *
 * Comments (anything following two slashes, inclusive) are skipped. As far
 * as allowed whitespace consisting an item delimiter goes, this is either
 * regular spaces, horizontal tabs, or newlines.
 *
 * Keep in mind that it does not own the string it is parsing. Therefore,
 * you have to make sure to keep it alive for as long as the parser is.
 *
 * The input string by itself should not be quoted.
 */
struct LIBCUBESCRIPT_EXPORT list_parser {
    /** @brief Construct a list parser.
     *
     * Nothing is done until you actually start parsing.
     *
     * @param cs the thread
     * @param s the string representing the list
     */
    list_parser(state &cs, std::string_view s = std::string_view{}):
        p_state{&cs}, p_input_beg{s.data()}, p_input_end{s.data() + s.size()}
     {}

    /** @brief Reset the input string for the list */
    void set_input(std::string_view s) {
        p_input_beg = s.data();
        p_input_end = s.data() + s.size();
    }

    /** @brief Get the current input string in the parser
     *
     * The already read items will not be contained in the result.
     */
    std::string_view input() const {
        return std::string_view{
            p_input_beg, std::size_t(p_input_end - p_input_beg)
        };
    }

    /** @brief Attempt to parse an item
     *
     * This will first skip whitespace and then attempt to read an element.
     *
     * @return `true` if an element was found, `false` otherwise
     */
    bool parse();

    /** @brief Get the number of items in the current list
     *
     * This will not contain items that are already parsed out, and will
     * parse the list itself, i.e. the final state will be an empty list.
     */
    std::size_t count();

    /** @brief Get the currently parsed item
     *
     * If the item was quoted with double quotes, the contents will be run
     * through cubescript::unescape_string() first.
     *
     * @see raw_item()
     * @see quoted_item()
     */
    string_ref get_item() const;

    /** @brief Get the currently parsed raw item
     *
     * Unlike get_item(), this will not unescape the string under any
     * circumstances and represents simply a slice of the original input.
     *
     * @see get_item()
     * @see quoted_item()
     */
    std::string_view raw_item() const {
        return std::string_view{p_ibeg, std::size_t(p_iend - p_ibeg)};
    }

    /** @brief Get the currently parsed raw item
     *
     * Like raw_item(), but contains the quotes too, if there were any.
     * Likewise, the resulting view is just a slice of the original input.
     *
     * @see get_item()
     * @see raw_item()
     */
    std::string_view quoted_item() const {
        return std::string_view{p_qbeg, std::size_t(p_qend - p_qbeg)};
    }

    /** @brief Skip whitespace in the input until a value is reached. */
    void skip_until_item();

private:
    state *p_state;
    char const *p_input_beg, *p_input_end;

    char const *p_ibeg{}, *p_iend{};
    char const *p_qbeg{}, *p_qend{};
};

/** @brief Parse a double quoted Cubescript string
 *
 * This parses double quoted strings according to the Cubescript syntax. The
 * string has to begin with a double quote; if it does not for any reason,
 * `str.data()` is returned.
 *
 * Escape sequences are not expanded and have the syntax `^X` where X is the
 * specific escape character (e.g. `^n` for newline). It is possible to make
 * the string multiline; the line needs to end with `\\`.
 *
 * Strings must be terminated again with double quotes.
 *
 * @param cs the thread
 * @param str the input string
 * @param[out] nlines the number of lines in the string
 *
 * @return a pointer to the character after the last double quotes
 * @throw cubescript::error if the string is started but not finished
 *
 * @see cubescript::parse_word()
 */
LIBCUBESCRIPT_EXPORT char const *parse_string(
    state &cs, std::string_view str, size_t &nlines
);

/** @brief Parse a double quoted Cubescript string
 *
 * This overload has the same semantics but it does not return the number
 * of lines.
 */
inline char const *parse_string(
    state &cs, std::string_view str
) {
    size_t nlines;
    return parse_string(cs, str, nlines);
}

/** @brief Parse a Cubescript word.
 *
 * A Cubescript word is a sequence of any characters that are not whitespace
 * (spaces, newlines, tabs) or a comment (two consecutive slashes). It is
 * allowed to have parenthesis and square brackets as long a they are balanced.
 *
 * Examples of valid words: `foo`, `test123`, `125.4`, `[foo]`, `hi(bar)`.
 *
 * If a non-word character is encountered immediately, the resulting pointer
 * will be `str.data()`.
 *
 * Keep in mind that a valid word may not be a valid ident name (e.g. numbers
 * are valid words but not valid ident names).
 *
 * @return a pointer to the first character after the word
 * @throw cubescript::error if there is unbalanced `[` or `(`
 */
LIBCUBESCRIPT_EXPORT char const *parse_word(
    state &cs, std::string_view str
);

/** @brief Concatenate a span of values
 *
 * The input values are concatenated by `sep`. Non-integer/float/string
 * input values are considered empty strings. Integers and floats are
 * converted to strings. The input list is not affected, however.
 */
LIBCUBESCRIPT_EXPORT string_ref concat_values(
    state &cs, span_type<any_value> vals,
    std::string_view sep = std::string_view{}
);

/** @brief Escape a Cubescript string
 *
 * This reads and input string and writes it into `writer`, treating special
 * characters as escape sequences. Newlines are turned into `^n`, tabs are
 * turned into `^t`, vertical tabs into `^f`; double quotes are prefixed
 * with a caret, carets are duplicated. All other characters are passed
 * through.
 *
 * @return `writer` after writing into it
 *
 * @see cubescript::unescape_string()
 */
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

/** @brief Unscape a Cubescript string
 *
 * If a caret is encountered, it is skipped. If the following character is `n`,
 * it is turned into a newline; `t` is turned into a tab, `f` into a vertical
 * tab, double quote is written as is, as is a second caret. Any others are
 * written as they are.
 *
 * If a backslash is encountered and followed by a newline, the sequence is
 * skipped, otherwise the backslash is written out. Any other character is
 * written out as is.
 *
 * @return `writer` after writing into it
 *
 * @see cubescript::unescape_string()
 */
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
                case 't': *writer++ = '\t'; break;
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

/** @brief Print a Cubescript stack
 *
 * This prints out the Cubescript stack as stored in cubescript::error, into
 * the `writer`. Each level is written on its own line. The line starts with
 * two spaces. If there is a gap in the stack and we've reached index 1,
 * the two spaces are followed with two periods. Following that is the index
 * followed by a right parenthesis, a space, and the name of the ident.
 *
 * The last line is not terminated with a newline.
 *
 * @return `writer` after writing into it
 */
template<typename R>
inline R print_stack(R writer, typename error::stack_node const *nd) {
    char buf[32] = {0};
    std::size_t pindex = 1;
    while (nd) {
        auto name = nd->id->name();
        *writer++ = ' ';
        *writer++ = ' ';
        if ((nd->index == 1) && (pindex > 2)) {
            *writer++ = '.';
            *writer++ = '.';
        }
        pindex = nd->index;
        snprintf(buf, sizeof(buf), "%zu", nd->index);
        char const *p = buf;
        std::copy(p, p + strlen(p), writer);
        *writer++ = ')';
        *writer++ = ' ';
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
