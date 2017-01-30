#ifndef CS_REPL_HAS_EDIT
/* use nothing (no line editing support) */

#include <ostd/string.hh>
#include <ostd/maybe.hh>

static void init_lineedit(CsState &, ostd::ConstCharRange) {
}

static ostd::Maybe<std::string> read_line(CsState &, CsSvar *pr) {
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
