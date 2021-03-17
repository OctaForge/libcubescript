#include <signal.h>

#include <optional>

#include <ostd/platform.hh>
#include <ostd/io.hh>
#include <ostd/string.hh>

#include <cubescript/cubescript.hh>

using namespace cscript;

ostd::string_range version = "CubeScript 0.0.1";

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

inline ostd::string_range get_complete_cmd(ostd::string_range buf) {
    ostd::string_range not_allowed = "\"/;()[] \t\r\n\0";
    ostd::string_range found = ostd::find_one_of(buf, not_allowed);
    while (!found.empty()) {
        ++found;
        buf = found;
        found = ostd::find_one_of(found, not_allowed);
    }
    return buf;
}

inline ostd::string_range get_arg_type(char arg) {
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

inline void fill_cmd_args(std::string &writer, ostd::string_range args) {
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
            for (std::size_t i = 0; i < args.size(); ++i) {
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

inline cs_command *get_hint_cmd(cs_state &cs, ostd::string_range buf) {
    ostd::string_range nextchars = "([;";
    auto lp = ostd::find_one_of(buf, nextchars);
    if (!lp.empty()) {
        cs_command *cmd = get_hint_cmd(cs, buf.slice(1, buf.size()));
        if (cmd) {
            return cmd;
        }
    }
    while (!buf.empty() && isspace(buf.front())) {
        ++buf;
    }
    ostd::string_range spaces = " \t\r\n";
    ostd::string_range s = ostd::find_one_of(buf, spaces);
    if (!s.empty()) {
        buf = buf.slice(0, &s[0] - &buf[0]);
    }
    if (!buf.empty()) {
        auto cmd = cs.get_ident(buf);
        return cmd ? cmd->get_command() : nullptr;
    }
    return nullptr;
}

#include "edit_linenoise.hh"
#include "edit_readline.hh"
#include "edit_fallback.hh"

/* usage */

void print_usage(ostd::string_range progname, bool err) {
    auto &s = err ? ostd::cerr : ostd::cout;
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

static cs_state *scs = nullptr;
static void do_sigint(int n) {
    /* in case another SIGINT happens, terminate normally */
    signal(n, SIG_DFL);
    scs->set_call_hook([](cs_state &cs) {
        cs.set_call_hook(nullptr);
        throw cscript::cs_error(cs, "<execution interrupted>");
    });
}

static bool do_call(cs_state &cs, ostd::string_range line, bool file = false) {
    cs_value ret{cs};
    scs = &cs;
    signal(SIGINT, do_sigint);
    try {
        if (file) {
            if (!cs.run_file(line, ret)) {
                ostd::cerr.writeln("cannot read file: ", line);
            }
        } else {
            cs.run(line, ret);
        }
    } catch (cscript::cs_error const &e) {
        signal(SIGINT, SIG_DFL);
        scs = nullptr;
        ostd::string_range terr = e.what();
        auto col = ostd::find(terr, ':');
        bool is_lnum = false;
        if (!col.empty()) {
            is_lnum = ostd::find_if(
                terr.slice(0, &col[0] - &terr[0]),
                [](auto c) { return !isdigit(c); }
            ).empty();
            terr = col.slice(2, col.size());
        }
        if (!file && ((terr == "missing \"]\"") || (terr == "missing \")\""))) {
            return true;
        }
        ostd::writeln(!is_lnum ? "stdin: " : "stdin:", e.what());
        if (e.get_stack().get()) {
            cscript::util::print_stack(ostd::cout.iter(), e.get_stack());
            ostd::write('\n');
        }
        return false;
    }
    signal(SIGINT, SIG_DFL);
    scs = nullptr;
    if (ret.get_type() != cs_value_type::Null) {
        ostd::writeln(ret.get_str());
    }
    return false;
}

static void do_tty(cs_state &cs) {
    auto prompt = cs.new_svar("PROMPT", "> ");
    auto prompt2 = cs.new_svar("PROMPT2", ">> ");

    bool do_exit = false;
    cs.new_command("quit", "", [&do_exit](auto &, auto, auto &) {
        do_exit = true;
    });

    ostd::writeln(version, " (REPL mode)");
    for (;;) {
        auto line = read_line(cs, prompt);
        if (!line) {
            return;
        }
        auto lv = std::move(line.value());
        if (lv.empty()) {
            continue;
        }
        while ((lv.back() == '\\') || do_call(cs, lv)) {
            bool bsl = (lv.back() == '\\');
            if (bsl) {
                lv.resize(lv.size() - 1);
            }
            auto line2 = read_line(cs, prompt2);
            if (!line2) {
                return;
            }
            if (!bsl || (line2.value() == "\\")) {
                lv += '\n';
            }
            lv += line2.value();
        }
        add_history(cs, lv);
        if (do_exit) {
            return;
        }
    }
}

int main(int argc, char **argv) {
    cs_state gcs;
    gcs.init_libs();

    gcs.new_command("exec", "s", [](auto &cs, auto args, auto &) {
        auto file = args[0].get_str();
        bool ret = cs.run_file(file);
        if (!ret) {
            throw cscript::cs_error(
                cs, "could not run file \"%s\"", file
            );
        }
    });

    gcs.new_command("echo", "C", [](auto &, auto args, auto &) {
        ostd::writeln(ostd::string_range{args[0].get_str()});
    });

    int firstarg = 0;
    bool has_inter = false, has_ver = false, has_help = false;
    char const *has_str = nullptr;
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
                if (argv[i][2] == '\0') {
                    ++i;
                    if (!argv[i]) {
                        firstarg = -1;
                        goto endargs;
                    } else {
                        has_str = argv[i];
                    }
                } else {
                    has_str = argv[i] + 2;
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
    if (has_str) {
        do_call(gcs, has_str);
    }
    if (firstarg) {
        do_call(gcs, argv[firstarg], true);
    }
    if (!firstarg && !has_str && !has_ver) {
        if (stdin_is_tty()) {
            init_lineedit(gcs, argv[0]);
            do_tty(gcs);
            return 0;
        } else {
            std::string str;
            for (signed char c = '\0'; (c = ostd::cin.get_char()) != EOF;) {
                str += c;
            }
            do_call(gcs, str);
        }
    }
    if (has_inter) {
        if (stdin_is_tty()) {
            init_lineedit(gcs, argv[0]);
            do_tty(gcs);
        }
        return 0;
    }
}
