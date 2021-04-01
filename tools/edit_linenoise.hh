#ifdef CS_REPL_USE_LINENOISE
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the bundled linenoise library, default */

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include <optional>

#include "linenoise.hh"

static cs::state *ln_cs = nullptr;

inline void ln_complete(char const *buf, std::vector<std::string> &lc) {
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
            lc.emplace_back(idname);
        }
    }
}

inline std::string ln_hint(char const *buf, int &color, int &bold) {
    cs::command *cmd = get_hint_cmd(*ln_cs, buf);
    if (!cmd) {
        return std::string{};
    }
    std::string args = " [";
    fill_cmd_args(args, cmd->get_args());
    args += ']';
    color = 35;
    bold = 1;
    return args;
}

inline void init_lineedit(cs::state &cs, std::string_view) {
    /* sensible default history size */
    linenoise::SetHistoryMaxLen(1000);
    ln_cs = &cs;
    linenoise::SetCompletionCallback(ln_complete);
    linenoise::SetHintsCallback(ln_hint);
}

inline std::optional<std::string> read_line(cs::state &, cs::string_var *pr) {
    std::string line;
    auto quit = linenoise::Readline(pr->get_value().data(), line);
    if (quit) {
        /* linenoise traps ctrl-c, detect it and let the user exit */
        if (errno == EAGAIN) {
            raise(SIGINT);
            return std::nullopt;
        }
        return std::string{};
    }
    return line;
}

inline void add_history(cs::state &, std::string_view line) {
    /* backed by std::string so it's terminated */
    linenoise::AddHistory(line.data());
}

#endif
#endif
