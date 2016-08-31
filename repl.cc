#include <ostd/io.hh>
#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <cubescript.hh>

using namespace cscript;

ostd::ConstCharRange version =
    "CubeScript 0.0.1 (REPL mode)  Copyright (C) 2016 Daniel \"q66\" Kolesa";
CsSvar *prompt = nullptr;

static ostd::String read_line() {
    ostd::write(prompt->get_value());
    auto app = ostd::appender<ostd::String>();
    /* i really need to implement some sort of get_line for ostd streams */
    for (char c = ostd::in.getchar(); c && (c != '\n'); c = ostd::in.getchar()) {
        app.put(c);
    }
    return ostd::move(app.get());
}

static void do_tty(CsState &cs) {
    ostd::writeln(version);
    for (;;) {
        auto line = read_line();
        if (line.empty()) {
            continue;
        }
        CsValue ret;
        ret.set_null();
        cs.run_ret(line, ret);
        if (ret.get_type() != CsValueType::null) {
            ostd::writeln(ret.get_str());
        }
    }
}

int main() {
    CsState cs;
    cs.init_libs();
    prompt = cs.add_ident<CsSvar>("PROMPT", "> ");
    do_tty(cs);
}
