#ifndef CS_REPL_HAS_EDIT
/* use nothing (no line editing support) */

#include <optional>

#include <ostd/string.hh>

static void init_lineedit(CsState &, ostd::ConstCharRange) {
}

static std::optional<std::string> read_line(CsState &, CsSvar *pr) {
    ostd::write(pr->get_value());
    std::string ret;
    /* i really need to implement some sort of get_line for ostd streams */
    for (char c = ostd::in.getchar(); c && (c != '\n'); c = ostd::in.getchar()) {
        ret += c;
    }
    return std::move(ret);
}

static void add_history(CsState &, ostd::ConstCharRange) {
}

#endif
