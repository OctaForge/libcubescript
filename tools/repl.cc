#include <ctype.h>

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

#include "tools/edit_linenoise.hh"
#include "tools/edit_libedit.hh"
#include "tools/edit_readline.hh"
#include "tools/edit_fallback.hh"

static void do_tty(CsState &cs) {
    auto prompt = cs.add_ident<CsSvar>("PROMPT", "> ");
    auto prompt2 = cs.add_ident<CsSvar>("PROMPT2", ">> ");

    bool do_exit = false;
    cs.add_command("quit", "", [&do_exit](auto, auto &) {
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
    cs.init_libs();
    if (stdin_is_tty()) {
        init_lineedit(argv[0]);
        do_tty(cs);
    } else {
        ostd::err.writeln("Only interactive mode is supported for now.");
    }
}
