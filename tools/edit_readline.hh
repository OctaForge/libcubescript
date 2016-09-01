#ifdef CS_REPL_USE_READLINE
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the GNU readline library */

#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <readline/readline.h>
#include <readline/history.h>

static void init_lineedit(ostd::ConstCharRange) {
}

static ostd::Maybe<ostd::String> read_line(CsSvar *pr) {
    auto line = readline(pr->get_value().data());
    if (!line) {
        return ostd::String();
    }
    ostd::String ret = line;
    free(line);
    return ostd::move(ret);
}

static void add_history(ostd::ConstCharRange line) {
    /* backed by ostd::String so it's terminated */
    add_history(line.data());
}

#endif
#endif
