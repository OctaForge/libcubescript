#ifdef CS_REPL_USE_LINENOISE
#ifndef _WIN32
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the bundled linenoise library, default */

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include <optional>

#include "linenoise.hh"

static cs::state *ln_cs = nullptr;

inline void ln_complete(char const *buf, linenoiseCompletions *lc) {
    std::string_view cmd = get_complete_cmd(buf);
    for (auto id: ln_cs->get_idents()) {
        if (!id->is_command()) {
            continue;
        }
        std::string_view idname = id->get_name();
        if (idname.size() <= cmd.size()) {
            continue;
        }
        if (idname.substr(0, cmd.size()) == cmd) {
            linenoiseAddCompletion(lc, idname.data());
        }
    }
}

inline char *ln_hint(char const *buf, int *color, int *bold) {
    cs::command *cmd = get_hint_cmd(*ln_cs, buf);
    if (!cmd) {
        return nullptr;
    }
    std::string args = " [";
    fill_cmd_args(args, cmd->get_args());
    args += ']';
    *color = 35;
    *bold = 1;
    char *ret = new char[args.size() + 1];
    memcpy(ret, args.data(), args.size() + 1);
    return ret;
}

inline void ln_hint_free(void *hint) {
    delete[] static_cast<char *>(hint);
}

inline void init_lineedit(cs::state &cs, std::string_view) {
    /* sensible default history size */
    linenoiseHistorySetMaxLen(1000);
    ln_cs = &cs;
    linenoiseSetCompletionCallback(ln_complete);
    linenoiseSetHintsCallback(ln_hint);
    linenoiseSetFreeHintsCallback(ln_hint_free);
}

inline std::optional<std::string> read_line(cs::state &, cs::string_var *pr) {
    auto line = linenoise(pr->get_value().data());
    if (!line) {
        /* linenoise traps ctrl-c, detect it and let the user exit */
        if (errno == EAGAIN) {
            raise(SIGINT);
            return std::nullopt;
        }
        return std::string{};
    }
    std::string ret = line;
    linenoiseFree(line);
    return ret;
}

inline void add_history(cs::state &, std::string_view line) {
    /* backed by std::string so it's terminated */
    linenoiseHistoryAdd(line.data());
}

#endif
#endif
#endif
