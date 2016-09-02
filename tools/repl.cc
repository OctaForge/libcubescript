#include <ostd/platform.hh>
#include <ostd/io.hh>
#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <cubescript.hh>

using namespace cscript;

ostd::ConstCharRange version = "CubeScript 0.0.1 (REPL mode)";

/* util */

#ifdef OSTD_PLATFORM_WIN32
#include <io.h>
static bool stdin_is_tty() {
    return _isatty(_fileno(stdin));
}
#else
#include <unistd.h>
static bool stdin_is_tty() {
    return isatty(0);
}
#endif

/* line editing support */

CsState *gcs = nullptr;

#ifdef CS_REPL_HAS_COMPLETE
static ostd::ConstCharRange get_complete_cmd(ostd::ConstCharRange buf) {
    ostd::ConstCharRange not_allowed = "\"/;()[] \t\r\n\0";
    ostd::ConstCharRange found = ostd::find_one_of(buf, not_allowed);
    while (!found.empty()) {
        ++found;
        buf = found;
        found = ostd::find_one_of(found, not_allowed);
    }
    return buf;
}
#endif /* CS_REPL_HAS_COMPLETE */

#ifdef CS_REPL_HAS_HINTS

static inline ostd::ConstCharRange get_arg_type(char arg) {
    switch (arg) {
        case 'i':
            return "int";
        case 'b':
            return "int_min";
        case 'f':
            return "float";
        case 'F':
            return "float_prev";
        case 't':
            return "any";
        case 'T':
            return "any_m";
        case 'E':
            return "cond";
        case 'N':
            return "numargs";
        case 'S':
            return "str_m";
        case 's':
            return "str";
        case 'e':
            return "block";
        case 'r':
            return "ident";
        case '$':
            return "self";
    }
    return "illegal";
}

static void fill_cmd_args(ostd::String &writer, ostd::ConstCharRange args) {
    char variadic = '\0';
    int nrep = 0;
    if (!args.empty() && ((args.back() == 'V') || (args.back() == 'C'))) {
        variadic = args.back();
        args.pop_back();
        if (!args.empty() && isdigit(args.back())) {
            nrep = args.back() - '0';
            args.pop_back();
        }
    }
    if (args.empty()) {
        if (variadic == 'C') {
            writer += "concat(...)";
        } else if (variadic == 'V') {
            writer += "...";
        }
        return;
    }
    int norep = int(args.size()) - nrep;
    if (norep > 0) {
        for (int i = 0; i < norep; ++i) {
            if (i != 0) {
                writer += ", ";
            }
            writer += get_arg_type(*args);
            ++args;
        }
    }
    if (variadic) {
        if (norep > 0) {
            writer += ", ";
        }
        if (variadic == 'C') {
            writer += "concat(";
        }
        if (!args.empty()) {
            if (args.size() > 1) {
                writer += '{';
            }
            for (ostd::Size i = 0; i < args.size(); ++i) {
                if (i) {
                    writer += ", ";
                }
                writer += get_arg_type(args[i]);
            }
            if (args.size() > 1) {
                writer += '}';
            }
        }
        writer += "...";
        if (variadic == 'C') {
            writer += ")";
        }
    }
}

static CsCommand *get_hint_cmd(ostd::ConstCharRange buf) {
    ostd::ConstCharRange nextchars = "([;";
    auto lp = ostd::find_one_of(buf, nextchars);
    if (!lp.empty()) {
        CsCommand *cmd = get_hint_cmd(buf + 1);
        if (cmd) {
            return cmd;
        }
    }
    while (!buf.empty() && isspace(buf.front())) {
        ++buf;
    }
    ostd::ConstCharRange spaces = " \t\r\n";
    ostd::ConstCharRange s = ostd::find_one_of(buf, spaces);
    if (!s.empty()) {
        buf = ostd::slice_until(buf, s);
    }
    if (!buf.empty()) {
        auto cmd = gcs->get_ident(buf);
        return cmd ? cmd->get_command() : nullptr;
    }
    return nullptr;
}

#endif /* CS_REPL_HAS_HINTS */

#include "tools/edit_linenoise.hh"
#include "tools/edit_libedit.hh"
#include "tools/edit_readline.hh"
#include "tools/edit_fallback.hh"

static void do_tty(CsState &cs) {
    auto prompt = cs.new_svar("PROMPT", "> ");
    auto prompt2 = cs.new_svar("PROMPT2", ">> ");

    bool do_exit = false;
    cs.new_command("quit", "", [&do_exit](auto, auto &) {
        do_exit = true;
    });

    ostd::writeln(version);
    for (;;) {
        auto line = read_line(prompt);
        if (!line) {
            return;
        }
        auto lv = ostd::move(line.value());
        if (lv.empty()) {
            continue;
        }
        while (lv.back() == '\\') {
            lv.resize(lv.size() - 1);
            auto line2 = read_line(prompt2);
            if (!line2) {
                return;
            }
            lv += line2.value();
        }
        add_history(lv);
        CsValue ret;
        ret.set_null();
        cs.run_ret(lv, ret);
        if (ret.get_type() != CsValueType::null) {
            ostd::writeln(ret.get_str());
        }
        if (do_exit) {
            return;
        }
    }
}

int main(int, char **argv) {
    CsState cs;
    gcs = &cs;
    cs.init_libs();
    if (stdin_is_tty()) {
        init_lineedit(argv[0]);
        do_tty(cs);
    } else {
        ostd::err.writeln("Only interactive mode is supported for now.");
    }
}
