#include <signal.h>

#include <ostd/platform.hh>
#include <ostd/io.hh>
#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <cubescript/cubescript.hh>

using namespace cscript;

ostd::ConstCharRange version = "CubeScript 0.0.1";

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

#include "edit_linenoise.hh"
#include "edit_readline.hh"
#include "edit_fallback.hh"

/* usage */

void print_usage(ostd::ConstCharRange progname, bool err) {
    auto &s = err ? ostd::err : ostd::out;
    s.writeln(
        "Usage: ", progname, " [options] [file]\n"
        "Options:\n"
        "  -e str  run string \"str\"\n"
        "  -i      enter interactive mode after the above\n"
        "  -v      show version information\n"
        "  -h      show this message\n"
        "  --      stop handling options\n"
        "  -       execute stdin and stop handling options"
    );
    s.flush();
}

void print_version() {
    ostd::writeln(version);
}

static void do_sigint(int n) {
    /* in case another SIGINT happens, terminate normally */
    signal(n, SIG_DFL);
    if (gcs) {
        gcs->set_call_hook([]() {
            gcs->set_call_hook(nullptr);
            gcs->error("<execution interrupted>");
        });
    }
}

static void do_call(CsState &cs, ostd::ConstCharRange line, bool file = false) {
    CsValue ret;
    signal(SIGINT, do_sigint);
    ostd::String err;
    if (!cs.pcall([&]() {
        if (file) {
            if (!cs.run_file(line, ret)) {
                ostd::err.writeln("cannot read file: ", line);
            }
        } else {
            cs.run(line, ret);
        }
    }, &err)) {
        signal(SIGINT, SIG_DFL);
        ostd::writeln("error: ", err);
        return;
    }
    signal(SIGINT, SIG_DFL);
    if (ret.get_type() != CsValueType::Null) {
        ostd::writeln(ret.get_str());
    }
}

static void do_tty(CsState &cs) {
    auto prompt = cs.new_svar("PROMPT", "> ");
    auto prompt2 = cs.new_svar("PROMPT2", ">> ");

    bool do_exit = false;
    cs.new_command("quit", "", [&do_exit](auto, auto &) {
        do_exit = true;
    });

    ostd::writeln(version, " (REPL mode)");
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
        do_call(cs, lv);
        if (do_exit) {
            return;
        }
    }
}

int main(int argc, char **argv) {
    CsState cs;
    gcs = &cs;
    cs.init_libs();

    cs.new_command("exec", "sb", [&cs](auto args, auto &res) {
        auto file = args[0].get_strr();
        bool ret = cs.run_file(file);
        if (!ret) {
            if (args[1].get_int()) {
                cs.get_err().writefln("could not run file \"%s\"", file);
            }
            res.set_int(0);
        } else {
            res.set_int(1);
        }
    });

    cs.new_command("echo", "C", [&cs](auto args, auto &) {
        cs.get_out().writeln(args[0].get_strr());
    });

    int firstarg = 0;
    bool has_inter = false, has_ver = false, has_help = false, has_str = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            firstarg = i;
            goto endargs;
        }
        switch (argv[i][1]) {
            case '-':
                if (argv[i][2] != '\0') {
                    firstarg = -1;
                    goto endargs;
                }
                firstarg = (argv[i + 1] != nullptr) ? (i + 1) : 0;
                goto endargs;
            case '\0':
                firstarg = i;
                goto endargs;
            case 'i':
                if (argv[i][2] != '\0') {
                    firstarg = -1;
                    goto endargs;
                }
                has_inter = true;
                break;
            case 'v':
                if (argv[i][2] != '\0') {
                    firstarg = -1;
                    goto endargs;
                }
                has_ver = true;
                break;
            case 'h':
                if (argv[i][2] != '\0') {
                    firstarg = -1;
                    goto endargs;
                }
                has_help = true;
                break;
            case 'e':
                has_str = true;
                if (argv[i][2] == '\0') {
                    ++i;
                    if (!argv[i]) {
                        firstarg = -1;
                        goto endargs;
                    }
                }
                break;
            default:
                firstarg = -1;
                goto endargs;
        }
    }
endargs:
    if (firstarg < 0) {
        print_usage(argv[0], true);
        return 1;
    }
    if (has_ver && !has_inter) {
        print_version();
    }
    if (has_help) {
        print_usage(argv[0], false);
        return 0;
    }
    for (int i = 1; i < ((firstarg > 0) ? firstarg : argc); ++i) {
        switch (argv[i][1]) {
            case 'e': {
                auto str = argv[i] + 2;
                if (*str == '\0') {
                    str = argv[++i];
                }
                do_call(cs, str);
                break;
            }
        }
    }
    if (firstarg) {
        do_call(cs, argv[firstarg], true);
    }
    if (!firstarg && !has_str && !has_ver) {
        if (stdin_is_tty()) {
            init_lineedit(argv[0]);
            do_tty(cs);
            return 0;
        } else {
            ostd::String str;
            for (char c = '\0'; (c = ostd::in.getchar()) != EOF;) {
                str += c;
            }
            do_call(cs, str);
        }
    }
    if (has_inter) {
        if (stdin_is_tty()) {
            init_lineedit(argv[0]);
            do_tty(cs);
        }
        return 0;
    }
}
