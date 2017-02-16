#ifdef CS_REPL_USE_READLINE
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the GNU readline library */

#include <string.h>

#include <optional>

#include <ostd/string.hh>

#include <readline/readline.h>
#include <readline/history.h>

static cs_state *rd_cs = nullptr;

static char *ln_complete_list(char const *buf, int state) {
    static ostd::string_range cmd;
    static ostd::PointerRange<cs_ident *> itr;

    if (!state) {
        cmd = get_complete_cmd(buf);
        itr = rd_cs->get_idents();
    }

    for (; !itr.empty(); itr.pop_front()) {
        cs_ident *id = itr.front();
        if (!id->is_command()) {
            continue;
        }
        ostd::string_range idname = id->get_name();
        if (idname.size() <= cmd.size()) {
            continue;
        }
        if (idname.slice(0, cmd.size()) == cmd) {
            itr.pop_front();
            return strdup(idname.data());
        }
    }

    return nullptr;
}

static char **ln_complete(char const *buf, int, int) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(buf, ln_complete_list);
}

void ln_hint() {
    cs_command *cmd = get_hint_cmd(*rd_cs, rl_line_buffer);
    if (!cmd) {
        rl_redisplay();
        return;
    }
    std::string old = rl_line_buffer;
    std::string args = old;
    args += " [";
    fill_cmd_args(args, cmd->get_args());
    args += "] ";
    rl_extend_line_buffer(args.size());
    rl_replace_line(args.data(), 0);
    rl_redisplay();
    rl_replace_line(old.data(), 0);
}

static void init_lineedit(cs_state &cs, ostd::string_range) {
    rd_cs = &cs;
    rl_attempted_completion_function = ln_complete;
    rl_redisplay_function = ln_hint;
}

static std::optional<std::string> read_line(cs_state &, cs_svar *pr) {
    auto line = readline(pr->get_value().data());
    if (!line) {
        return std::string();
    }
    std::string ret = line;
    free(line);
    return std::move(ret);
}

static void add_history(cs_state &, ostd::string_range line) {
    /* backed by std::string so it's terminated */
    add_history(line.data());
}

#endif
#endif
