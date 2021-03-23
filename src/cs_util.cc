#include <cubescript/cubescript.hh>
#include "cs_vm.hh"
#include "cs_strman.hh"

#include <cctype>
#include <cmath>
#include <iterator>
#include <algorithm>

namespace cscript {

namespace util {
    LIBCUBESCRIPT_EXPORT char const *parse_string(
        cs_state &cs, std::string_view str, size_t &nlines
    ) {
        size_t nl = 0;
        nlines = nl;
        if (str.empty() || (str.front() != '\"')) {
            return str.data();
        }
        char const *beg = str.begin();
        char const *end = str.end();
        char const *orig = beg++;
        ++nl;
        while (beg != end) {
            switch (*beg) {
                case '\r':
                case '\n':
                case '\"':
                    goto end;
                case '^':
                case '\\': {
                    bool needn = (*beg == '\\');
                    if (++beg == end) {
                        goto end;
                    }
                    if ((*beg == '\r') || (*beg == '\n')) {
                        char c = *beg++;
                        ++nl;
                        if ((beg != end) && (c == '\r') && (*beg == '\n')) {
                            ++beg;
                        }
                    } else if (needn) {
                        goto end;
                    } else {
                        ++beg;
                    }
                    continue;
                }
                default:
                    break;
            }
            ++beg;
        }
end:
        nlines = nl;
        if ((beg == end) || (*beg != '\"')) {
            throw cs_error(
                cs, "unfinished string '%s'",
                std::string_view{orig, std::size_t(beg - orig)}
            );
        }
        return ++beg;
    }

    LIBCUBESCRIPT_EXPORT char const *parse_word(cs_state &cs, std::string_view str) {
        char const *it = str.begin();
        char const *end = str.end();
        for (; it != end; ++it) {
            std::string_view chrs{"\"/;()[] \t\r\n"};
            it = std::find_first_of(it, end, chrs.begin(), chrs.end());
            if (it == end) {
                return it;
            }
            switch (*it) {
                case '"':
                case ';':
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                    return it;
                case '/':
                    if (((end - it) > 1) && (it[1] == '/')) {
                        return it;
                    }
                    break;
                case '[':
                    ++it;
                    it = parse_word(cs, std::string_view{
                        it, std::size_t(end - it)
                    });
                    if ((it == end) || (*it != ']')) {
                        throw cs_error(cs, "missing \"]\"");
                    }
                    break;
                case '(':
                    ++it;
                    it = parse_word(cs, std::string_view{
                        it, std::size_t(end - it)
                    });
                    if ((it == end) || (*it != ')')) {
                        throw cs_error(cs, "missing \")\"");
                    }
                    break;
                case ']':
                case ')':
                    return it;
            }
        }
        return it;
    }
} /* namespace util */

LIBCUBESCRIPT_EXPORT bool cs_list_parser::parse() {
    skip_until_item();
    if (p_input_beg == p_input_end) {
        return false;
    }
    switch (*p_input_beg) {
        case '"': {
            char const *qi = p_input_beg;
            p_input_beg = util::parse_string(*p_state, get_input());
            p_quoted_item = std::string_view{qi, p_input_beg};
            p_item = p_quoted_item.substr(1, p_quoted_item.size() - 2);
            break;
        }
        case '(':
        case '[': {
            char btype = *p_input_beg;
            int brak = 1;
            char const *ibeg = p_input_beg++;
            for (;;) {
                std::string_view chrs{"\"/;()[]"};
                p_input_beg = std::find_first_of(
                    p_input_beg, p_input_end, chrs.begin(), chrs.end()
                );
                if (p_input_beg == p_input_end) {
                    return true;
                }
                char c = *p_input_beg++;
                switch (c) {
                    case '"':
                        /* the quote is needed in str parsing */
                        --p_input_beg;
                        p_input_beg = util::parse_string(*p_state, get_input());
                        break;
                    case '/':
                        if (
                            (p_input_beg != p_input_end) &&
                            (*p_input_beg == '/')
                        ) {
                            p_input_beg = std::find(
                                p_input_beg, p_input_end, '\n'
                            );
                        }
                        break;
                    case '(':
                    case '[':
                        brak += (c == btype);
                        break;
                    case ')':
                        if ((btype == '(') && (--brak <= 0)) {
                            goto endblock;
                        }
                        break;
                    case ']':
                        if ((btype == '[') && (--brak <= 0)) {
                            goto endblock;
                        }
                        break;
                }
            }
endblock:
            p_item = std::string_view{ibeg + 1, p_input_beg - 1};
            p_quoted_item = std::string_view{ibeg, p_input_beg};
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            char const *e = util::parse_word(*p_state, get_input());
            p_quoted_item = p_item = std::string_view{p_input_beg, e};
            p_input_beg = e;
            break;
        }
    }
    skip_until_item();
    if ((p_input_beg != p_input_end) && (*p_input_beg == ';')) {
        ++p_input_beg;
    }
    return true;
}

LIBCUBESCRIPT_EXPORT std::size_t cs_list_parser::count() {
    size_t ret = 0;
    while (parse()) {
        ++ret;
    }
    return ret;
}

LIBCUBESCRIPT_EXPORT cs_strref cs_list_parser::get_item() const {
    if (!p_quoted_item.empty() && (p_quoted_item.front() == '"')) {
        cs_charbuf buf{*p_state};
        util::unescape_string(std::back_inserter(buf), p_item);
        return cs_strref{*p_state, buf.str()};
    }
    return cs_strref{*p_state, p_item};
}

LIBCUBESCRIPT_EXPORT void cs_list_parser::skip_until_item() {
    for (;;) {
        while (p_input_beg != p_input_end) {
            char c = *p_input_beg;
            if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
                ++p_input_beg;
            } else {
                break;
            }
        }
        if ((p_input_end - p_input_beg) < 2) {
            break;
        }
        if ((p_input_beg[0] != '/') || (p_input_beg[1]) != '/') {
            break;
        }
        p_input_beg = std::find(p_input_beg, p_input_end, '\n');
    }
}

LIBCUBESCRIPT_EXPORT cs_strref value_list_concat(
    cs_state &cs, std::span<cs_value> vals, std::string_view sep
) {
    cs_charbuf buf{cs};
    for (std::size_t i = 0; i < vals.size(); ++i) {
        switch (vals[i].get_type()) {
            case cs_value_type::INT:
            case cs_value_type::FLOAT:
            case cs_value_type::STRING:
                std::ranges::copy(
                    cs_value{vals[i]}.force_str(), std::back_inserter(buf)
                );
                break;
            default:
                break;
        }
        if (i == (vals.size() - 1)) {
            break;
        }
        std::ranges::copy(sep, std::back_inserter(buf));
    }
    return cs_strref{cs, buf.str()};
}

} /* namespace cscript */
