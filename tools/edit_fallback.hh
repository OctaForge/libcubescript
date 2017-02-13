#ifndef CS_REPL_HAS_EDIT
/* use nothing (no line editing support) */

#include <optional>

#include <ostd/string.hh>

static void init_lineedit(cs_state &, ostd::ConstCharRange) {
}

static std::optional<std::string> read_line(cs_state &, cs_svar *pr) {
    ostd::write(pr->get_value());
    std::string ret;
    /* i really need to implement some sort of get_line for ostd streams */
    for (char c = ostd::in.getchar(); c && (c != '\n'); c = ostd::in.getchar()) {
        ret += c;
    }
    return std::move(ret);
}

static void add_history(cs_state &, ostd::ConstCharRange) {
}

#endif
