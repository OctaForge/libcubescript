#include <memory>

#include "cs_bcode.hh"
#include "cs_state.hh"
#include "cs_strman.hh"
#include "cs_vm.hh" // FIXME, only Max Arguments

namespace cscript {

cs_shared_state::cs_shared_state(cs_alloc_cb af, void *data):
    allocf{af}, aptr{data},
    idents{allocator_type{this}},
    identmap{allocator_type{this}},
    varprintf{},
    strman{create<cs_strman>(this)},
    empty{bcode_init_empty(this)}
{}

cs_shared_state::~cs_shared_state() {
     bcode_free_empty(this, empty);
     destroy(strman);
}

void *cs_shared_state::alloc(void *ptr, size_t os, size_t ns) {
    void *p = allocf(aptr, ptr, os, ns);
    if (!p && ns) {
        throw std::bad_alloc{};
    }
    return p;
}

static void *cs_default_alloc(void *, void *p, size_t, size_t ns) {
    if (!ns) {
        std::free(p);
        return nullptr;
    }
    return std::realloc(p, ns);
}

void cs_init_lib_base(cs_state &cs);
void cs_init_lib_math(cs_state &cs);
void cs_init_lib_string(cs_state &cs);
void cs_init_lib_list(cs_state &cs);

/* public interfaces */

cs_state::cs_state(): cs_state{cs_default_alloc, nullptr} {}

cs_state::cs_state(cs_alloc_cb func, void *data):
    p_state{nullptr}, p_callhook{}
{
    cs_command *p;

    if (!func) {
        func = cs_default_alloc;
    }
    /* allocator is not set up yet, use func directly */
    p_state = static_cast<cs_shared_state *>(
        func(data, nullptr, 0, sizeof(cs_shared_state))
    );
    /* allocator will be set up in the constructor */
    new (p_state) cs_shared_state{func, data};
    p_owner = true;

    /* will be used as message storage for errors */
    p_errbuf = p_state->create<cs_charbuf>(*this);

    for (int i = 0; i < MaxArguments; ++i) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%d", i + 1);
        new_ident(static_cast<char const *>(buf), CS_IDF_ARG);
    }

    cs_ident *id = new_ident("//dummy");
    if (id->get_index() != DummyIdx) {
        throw cs_internal_error{"invalid dummy index"};
    }

    id = new_ivar("numargs", MaxArguments, 0, 0);
    if (id->get_index() != NumargsIdx) {
        throw cs_internal_error{"invalid numargs index"};
    }

    id = new_ivar("dbgalias", 0, 1000, 4);
    if (id->get_index() != DbgaliasIdx) {
        throw cs_internal_error{"invalid dbgalias index"};
    }

    p = new_command("do", "e", [](auto &cs, auto args, auto &res) {
        cs.run(args[0].get_code(), res);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_DO;

    p = new_command("doargs", "e", [](auto &cs, auto args, auto &res) {
        cs_do_args(cs, [&cs, &res, &args]() {
            cs.run(args[0].get_code(), res);
        });
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_DOARGS;

    p = new_command("if", "tee", [](auto &cs, auto args, auto &res) {
        cs.run((args[0].get_bool() ? args[1] : args[2]).get_code(), res);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_IF;

    p = new_command("result", "t", [](auto &, auto args, auto &res) {
        res = std::move(args[0]);
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_RESULT;

    p = new_command("!", "t", [](auto &, auto args, auto &res) {
        res.set_int(!args[0].get_bool());
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_NOT;

    p = new_command("&&", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(1);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                cs_bcode *code = args[i].get_code();
                if (code) {
                    cs.run(code, res);
                } else {
                    res = std::move(args[i]);
                }
                if (!res.get_bool()) {
                    break;
                }
            }
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_AND;

    p = new_command("||", "E1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            res.set_int(0);
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                cs_bcode *code = args[i].get_code();
                if (code) {
                    cs.run(code, res);
                } else {
                    res = std::move(args[i]);
                }
                if (res.get_bool()) {
                    break;
                }
            }
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_OR;

    p = new_command("local", "", nullptr);
    static_cast<cs_command_impl *>(p)->p_type = ID_LOCAL;

    p = new_command("break", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsBreakException();
        } else {
            throw cs_error(cs, "no loop to break");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_BREAK;

    p = new_command("continue", "", [](auto &cs, auto, auto &) {
        if (cs.is_in_loop()) {
            throw CsContinueException();
        } else {
            throw cs_error(cs, "no loop to continue");
        }
    });
    static_cast<cs_command_impl *>(p)->p_type = ID_CONTINUE;

    cs_init_lib_base(*this);
}

LIBCUBESCRIPT_EXPORT cs_state::~cs_state() {
    destroy();
}

LIBCUBESCRIPT_EXPORT void cs_state::destroy() {
    if (!p_state || !p_owner) {
        return;
    }
    for (auto &p: p_state->idents) {
        cs_ident *i = p.second;
        cs_alias *a = i->get_alias();
        if (a) {
            a->get_value().force_none();
            static_cast<cs_alias_impl *>(a)->clean_code();
        }
        p_state->destroy(i->p_impl);
    }
    p_state->destroy(static_cast<cs_charbuf *>(p_errbuf));
    p_state->destroy(p_state);
}

cs_state::cs_state(cs_shared_state *s):
    p_state(s), p_owner(false)
{}

LIBCUBESCRIPT_EXPORT cs_state cs_state::new_thread() {
    return cs_state{p_state};
}

LIBCUBESCRIPT_EXPORT cs_hook_cb cs_state::set_call_hook(cs_hook_cb func) {
    auto hk = std::move(p_callhook);
    p_callhook = std::move(func);
    return hk;
}

LIBCUBESCRIPT_EXPORT cs_hook_cb const &cs_state::get_call_hook() const {
    return p_callhook;
}

LIBCUBESCRIPT_EXPORT cs_hook_cb &cs_state::get_call_hook() {
    return p_callhook;
}

LIBCUBESCRIPT_EXPORT cs_vprint_cb cs_state::set_var_printer(
    cs_vprint_cb func
) {
    auto fn = std::move(p_state->varprintf);
    p_state->varprintf = std::move(func);
    return fn;
}

LIBCUBESCRIPT_EXPORT cs_vprint_cb const &cs_state::get_var_printer() const {
    return p_state->varprintf;
}

LIBCUBESCRIPT_EXPORT void *cs_state::alloc(void *ptr, size_t os, size_t ns) {
    return p_state->alloc(ptr, os, ns);
}

LIBCUBESCRIPT_EXPORT void cs_state::init_libs(int libs) {
    if (libs & CS_LIB_MATH) {
        cs_init_lib_math(*this);
    }
    if (libs & CS_LIB_STRING) {
        cs_init_lib_string(*this);
    }
    if (libs & CS_LIB_LIST) {
        cs_init_lib_list(*this);
    }
}

} /* namespace cscript */
