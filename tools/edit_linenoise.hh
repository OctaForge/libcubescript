#ifdef CS_REPL_USE_LINENOISE
#ifdef OSTD_PLATFORM_POSIX
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the bundled linenoise library, default */

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include "linenoise.hh"

#ifdef CS_REPL_HAS_COMPLETE
static void ln_complete(char const *buf, linenoiseCompletions *lc) {
    ostd::ConstCharRange cmd = get_complete_cmd(buf);
    for (auto id: gcs->identmap.iter()) {
        if (!id->is_command()) {
            continue;
        }
        ostd::ConstCharRange idname = id->get_name();
        if (idname.size() <= cmd.size()) {
            continue;
        }
        if (idname.slice(0, cmd.size()) == cmd) {
            linenoiseAddCompletion(lc, idname.data());
        }
    }
}
#endif /* CS_REPL_HAS_COMPLETE */

#ifdef CS_REPL_HAS_HINTS
static char *ln_hint(char const *buf, int *color, int *bold) {
    CsCommand *cmd = get_hint_cmd(buf);
    if (!cmd) {
        return nullptr;
    }
    ostd::String args = " [";
    fill_cmd_args(args, cmd->get_args());
    args += ']';
    *color = 35;
    *bold = 1;
    char *ret = new char[args.size() + 1];
    memcpy(ret, args.data(), args.size() + 1);
    return ret;
}

static void ln_hint_free(void *hint) {
    delete[] static_cast<char *>(hint);
}
#endif /* CS_REPL_HAS_HINTS */

static void init_lineedit(ostd::ConstCharRange) {
    /* sensible default history size */
    linenoiseHistorySetMaxLen(1000);
#ifdef CS_REPL_HAS_COMPLETE
    linenoiseSetCompletionCallback(ln_complete);
#endif
#ifdef CS_REPL_HAS_HINTS
    linenoiseSetHintsCallback(ln_hint);
    linenoiseSetFreeHintsCallback(ln_hint_free);
#endif
}

static ostd::Maybe<ostd::String> read_line(CsSvar *pr) {
    auto line = linenoise(pr->get_value().data());
    if (!line) {
        /* linenoise traps ctrl-c, detect it and let the user exit */
        if (errno == EAGAIN) {
            raise(SIGINT);
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
#endif
