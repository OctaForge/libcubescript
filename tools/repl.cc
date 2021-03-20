#include <signal.h>

#include <optional>
#include <memory>
#include <iterator>

#include <ostd/platform.hh>
#include <ostd/io.hh>
#include <ostd/string.hh>

#include <cubescript/cubescript.hh>

using namespace cscript;

std::string_view version = "CubeScript 0.0.1";

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

inline std::string_view get_complete_cmd(std::string_view buf) {
    std::string_view not_allowed = "\"/;()[] \t\r\n\0";
    auto found = buf.find_first_of(not_allowed);
    while (found != buf.npos) {
        buf = buf.substr(found + 1, buf.size() - found - 1);
        found = buf.find_first_of(not_allowed);
    }
    return buf;
}

inline std::string_view get_arg_type(char arg) {
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

inline void fill_cmd_args(std::string &writer, std::string_view args) {
    char variadic = '\0';
    int nrep = 0;
    if (!args.empty() && ((args.back() == 'V') || (args.back() == 'C'))) {
        variadic = args.back();
        args.remove_suffix(1);
        if (!args.empty() && isdigit(args.back())) {
            nrep = args.back() - '0';
            args.remove_suffix(1);
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
            writer += get_arg_type(args.front());
            args.remove_prefix(1);
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

inline cs_command *get_hint_cmd(cs_state &cs, std::string_view buf) {
    std::string_view nextchars = "([;";
    auto lp = buf.find_first_of(nextchars);
    if (lp != buf.npos) {
        cs_command *cmd = get_hint_cmd(cs, buf.substr(1, buf.size() - 1));
        if (cmd) {
            return cmd;
        }
    }
    std::size_t nsp = 0;
    for (auto c: buf) {
        if (!isspace(c)) {
            break;
        }
        ++nsp;
    }
    buf.remove_prefix(nsp);
    std::string_view spaces = " \t\r\n";
    auto p = buf.find_first_of(spaces);
    if (p != buf.npos) {
        buf = buf.substr(0, p);
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

void print_usage(std::string_view progname, bool err) {
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

/* an example of what var printer would look like in real usage */
static void repl_print_var(cs_state const &cs, cs_var const &var) {
    switch (var.get_type()) {
        case cs_ident_type::IVAR: {
            auto &iv = static_cast<cs_ivar const &>(var);
            auto val = iv.get_value();
            if (!(iv.get_flags() & CS_IDF_HEX) || (val < 0)) {
                ostd::writefln("%s = %d", iv.get_name(), val);
            } else if (iv.get_val_max() == 0xFFFFFF) {
                ostd::writefln(
                    "%s = 0x%.6X (%d, %d, %d)", iv.get_name(),
                    val, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF
                );
            } else {
                ostd::writefln("%s = 0x%X", iv.get_name(), val);
            }
            break;
        }
        case cs_ident_type::FVAR: {
            auto &fv = static_cast<cs_fvar const &>(var);
            auto val = fv.get_value();
            ostd::writefln(
                (floor(val) == val) ? "%s = %.1f" : "%s = %.7g",
                fv.get_name(), val
            );
            break;
        }
        case cs_ident_type::SVAR: {
            auto &sv = static_cast<cs_svar const &>(var);
            auto val = std::string_view{sv.get_value()};
            if (val.find('"') == val.npos) {
                ostd::writefln("%s = \"%s\"", sv.get_name(), val);
            } else {
                ostd::writefln("%s = [%s]", sv.get_name(), val);
            }
            break;
        }
        default:
            break;
    }
}

static bool do_run_file(cs_state &cs, std::string_view fname, cs_value &ret) {
    std::unique_ptr<char[]> buf;
    std::size_t len;

    ostd::file_stream f{fname, ostd::stream_mode::READ};
    if (!f.is_open()) {
        return false;
    }

    len = f.size();
    buf = std::make_unique<char[]>(len + 1);
    if (!buf) {
        return false;
    }

    try {
        f.get(buf.get(), len);
    } catch (...) {
        return false;
    }
    buf[len] = '\0';

    cs.run(std::string_view{buf.get(), len}, ret, fname);
    return true;
}

static bool do_call(cs_state &cs, std::string_view line, bool file = false) {
    cs_value ret{cs};
    scs = &cs;
    signal(SIGINT, do_sigint);
    try {
        if (file) {
            if (!do_run_file(cs, line, ret)) {
                ostd::cerr.writeln("cannot read file: ", line);
            }
        } else {
            cs.run(line, ret);
        }
    } catch (cscript::cs_error const &e) {
        signal(SIGINT, SIG_DFL);
        scs = nullptr;
        std::string_view terr = e.what();
        auto col = terr.find(':');
        bool is_lnum = false;
        if (col != terr.npos) {
            auto pre = terr.substr(0, col);
            auto it = std::find_if(
                pre.begin(), pre.end(),
                [](auto c) { return !isdigit(c); }
            );
            is_lnum = (it == pre.end());
            terr = terr.substr(col + 2, terr.size() - col - 2);
        }
        if (!file && ((terr == "missing \"]\"") || (terr == "missing \")\""))) {
            return true;
        }
        ostd::writeln(!is_lnum ? "stdin: " : "stdin:", e.what());
        if (e.get_stack().get()) {
            std::string str;
            cscript::util::print_stack(std::back_inserter(str), e.get_stack());
            ostd::writeln(str);
        }
        return false;
    }
    signal(SIGINT, SIG_DFL);
    scs = nullptr;
    if (ret.get_type() != cs_value_type::NONE) {
        ostd::writeln(std::string_view{ret.get_str()});
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
    gcs.set_var_printer(repl_print_var);
    gcs.init_libs();

    gcs.new_command("exec", "s", [](auto &cs, auto args, auto &) {
        auto file = args[0].get_str();
        cs_value val{cs};
        bool ret = do_run_file(cs, file, val);
        if (!ret) {
            throw cscript::cs_error(
                cs, "could not run file \"%s\"", file
            );
        }
    });

    gcs.new_command("echo", "C", [](auto &, auto args, auto &) {
        ostd::writeln(std::string_view{args[0].get_str()});
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
