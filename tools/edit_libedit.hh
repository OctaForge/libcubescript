#ifdef CS_REPL_USE_LIBEDIT
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the NetBSD libedit library */

#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <histedit.h>

static EditLine *els = nullptr;
static History *elh = nullptr;

static char *el_prompt(EditLine *el) {
    void *prompt = nullptr;
    el_get(el, EL_CLIENTDATA, &prompt);
    if (!prompt) {
        return const_cast<char *>("");
    }
    return const_cast<char *>(static_cast<CsSvar *>(prompt)->get_value().data());
}

static void init_lineedit(ostd::ConstCharRange progname) {
    els = el_init(progname.data(), stdin, stdout, stderr);
    elh = history_init();

    /* init history with reasonable size */
    HistEvent ev;
    history(elh, &ev, H_SETSIZE, 1000);
    el_set(els, EL_HIST, history, elh);

    el_set(els, EL_PROMPT, el_prompt);
}

static ostd::Maybe<ostd::String> read_line(CsSvar *pr) {
    int count;
    el_set(els, EL_CLIENTDATA, static_cast<void *>(pr));
    auto line = el_gets(els, &count);
    if (count > 0) {
        ostd::String ret = line;
        /* libedit keeps the trailing \n */
        ret.resize(ret.size() - 1);
        return ostd::move(ret);
    } else if (!count) {
        return ostd::String();
    }
    return ostd::nothing;
}

static void add_history(ostd::ConstCharRange line) {
    HistEvent ev;
    /* backed by ostd::String so it's terminated */
    history(elh, &ev, H_ENTER, line.data());
}

#endif
#endif
