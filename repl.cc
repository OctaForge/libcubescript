#include <ostd/io.hh>
#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <cubescript.hh>

using namespace cscript;

ostd::ConstCharRange version =
    "CubeScript 0.0.1 (REPL mode)  Copyright (C) 2016 Daniel \"q66\" Kolesa";
CsSvar *prompt = nullptr;

static ostd::Maybe<ostd::String> read_line() {
    ostd::write(prompt->get_value());
    char buf[512];
    if (fgets(buf, sizeof(buf), stdin)) {
        return ostd::String(buf);
    }
    return ostd::nothing;
}

static void do_tty(CsState &cs) {
    ostd::writeln(version);
    for (;;) {
        auto line = read_line();
        if (!line) {
            continue;
        }
        CsValue ret;
        ret.set_null();
        cs.run_ret(line.value(), ret);
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
