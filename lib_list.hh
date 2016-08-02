#ifndef LIB_LIST_HH
#define LIB_LIST_HH

#include "cubescript.hh"

namespace cscript {

ostd::ConstCharRange cs_parse_str(ostd::ConstCharRange str);
char const *parseword(char const *p);

struct ListParser {
    ostd::ConstCharRange input;
    ostd::ConstCharRange quote = ostd::ConstCharRange();
    ostd::ConstCharRange item = ostd::ConstCharRange();

    ListParser() = delete;
    ListParser(ostd::ConstCharRange src): input(src) {}

    void skip() {
        for (;;) {
            while (!input.empty()) {
                char c = input.front();
                if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
                    input.pop_front();
                else
                    break;
            }
            if ((input.size() < 2) || (input[0] != '/') || (input[1] != '/'))
                break;
            input = ostd::find(input, '\n');
        }
    }

    bool parse() {
        skip();
        if (input.empty())
            return false;
        switch (input.front()) {
        case '"':
            quote = input;
            input.pop_front();
            item = input;
            input = cs_parse_str(input);
            item = ostd::slice_until(item, input);
            if (!input.empty() && (input.front() == '"'))
                input.pop_front();
            quote = ostd::slice_until(quote, input);
            break;
        case '(':
        case '[': {
            quote = input;
            input.pop_front();
            item = input;
            char btype = quote.front();
            int brak = 1;
            for (;;) {
                input = ostd::find_one_of(input,
                    ostd::ConstCharRange("\"/;()[]"));
                if (input.empty())
                    return true;
                char c = input.front();
                input.pop_front();
                switch (c) {
                case '"':
                    input = cs_parse_str(input);
                    if (!input.empty() && (input.front() == '"'))
                        input.pop_front();
                    break;
                case '/':
                    if (!input.empty() && (input.front() == '/'))
                        input = ostd::find(input, '\n');
                    break;
                case '(':
                case '[':
                    brak += (c == btype);
                    break;
                case ')':
                    if ((btype == '(') && (--brak <= 0))
                        goto endblock;
                    break;
                case ']':
                    if ((btype == '[') && (--brak <= 0))
                        goto endblock;
                    break;
                }
            }
endblock:
            item = ostd::slice_until(item, input);
            item.pop_back();
            quote = ostd::slice_until(quote, input);
            break;
        }
        case ')':
        case ']':
            return false;
        default: {
            char const *e = parseword(input.data());
            item = input;
            input.pop_front_n(e - input.data());
            item = ostd::slice_until(item, input);
            quote = item;
            break;
        }
        }
        skip();
        if (!input.empty() && (input.front() == ';'))
            input.pop_front();
        return true;
    }

    ostd::String element() {
        ostd::String s;
        s.reserve(item.size());
        if (!quote.empty() && (quote.front() == '"')) {
            auto writer = s.iter_cap();
            util::unescape_string(writer, item);
            writer.put('\0');
        } else {
            memcpy(s.data(), item.data(), item.size());
            s[item.size()] = '\0';
        }
        s.advance(item.size());
        return s;
    }
};

} /*namespace cscript */

#endif
