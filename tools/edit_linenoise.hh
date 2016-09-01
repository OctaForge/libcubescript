#ifdef CS_REPL_USE_LINENOISE
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the bundled linenoise library, default */

#include <errno.h>

#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include "linenoise.hh"

static void init_lineedit(ostd::ConstCharRange) {
    /* sensible default history size */
    linenoiseHistorySetMaxLen(1000);
}

static ostd::Maybe<ostd::String> read_line(CsSvar *pr) {
    auto line = linenoise(pr->get_value().data());
    if (!line) {
        /* linenoise traps ctrl-c, detect it and let the user exit */
        if (errno == EAGAIN) {
            return ostd::nothing;
        }
        return ostd::String();
    }
    ostd::String ret = line;
    linenoiseFree(line);
    return ostd::move(ret);
}

static void add_history(ostd::ConstCharRange line) {
    /* backed by ostd::String so it's terminated */
    linenoiseHistoryAdd(line.data());
}

#endif
#endif
