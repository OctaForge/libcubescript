#include "cubescript.hh"
#include "cs_private.hh"

namespace cscript {

static void cs_init_lib_base_var(CsState &cs) {
    cs.add_command("nodebug", "e", [&cs](TvalRange args) {
        ++cs.nodebug;
        cs.run_ret(args[0].get_code());
        --cs.nodebug;
    });

    cs.add_command("push", "rTe", [&cs](TvalRange args) {
        Ident *id = args[0].get_ident();
        if (id->type != ID_ALIAS || id->index < MaxArguments) return;
        IdentStack stack;
        TaggedValue &v = args[1];
        id->push_arg(v, stack);
        v.set_null();
        cs.run_ret(args[2].get_code());
        id->pop_arg();
    });

    cs.add_command("local", nullptr, nullptr, ID_LOCAL);

    cs.add_command("resetvar", "s", [&cs](TvalRange args) {
        cs.result->set_int(cs.reset_var(args[0].get_strr()));
    });

    cs.add_command("alias", "sT", [&cs](TvalRange args) {
        TaggedValue &v = args[1];
        cs.set_alias(args[0].get_strr(), v);
        v.set_null();
    });

    cs.add_command("getvarmin", "s", [&cs](TvalRange args) {
        cs.result->set_int(cs.get_var_min_int(args[0].get_strr()).value_or(0));
    });
    cs.add_command("getvarmax", "s", [&cs](TvalRange args) {
        cs.result->set_int(cs.get_var_max_int(args[0].get_strr()).value_or(0));
    });
    cs.add_command("getfvarmin", "s", [&cs](TvalRange args) {
        cs.result->set_float(cs.get_var_min_float(args[0].get_strr()).value_or(0.0f));
    });
    cs.add_command("getfvarmax", "s", [&cs](TvalRange args) {
        cs.result->set_float(cs.get_var_max_float(args[0].get_strr()).value_or(0.0f));
    });

    cs.add_command("identexists", "s", [&cs](TvalRange args) {
        cs.result->set_int(cs.have_ident(args[0].get_strr()));
    });

    cs.add_command("getalias", "s", [&cs](TvalRange args) {
        cs.result->set_str(ostd::move(cs.get_alias(args[0].get_strr()).value_or("")));
    });
}

void cs_init_lib_io(CsState &cs) {
    cs.add_command("exec", "sb", [&cs](TvalRange args) {
        auto file = args[0].get_strr();
        bool ret = cs.run_file(file);
        if (!ret) {
            if (args[1].get_int())
                ostd::err.writefln("could not run file \"%s\"", file);
            cs.result->set_int(0);
        } else
            cs.result->set_int(1);
    });

    cs.add_command("echo", "C", [](TvalRange args) {
        ostd::writeln(args[0].get_strr());
    });
}

static void cs_init_lib_base_loops(CsState &cs);

void cs_init_lib_base(CsState &cs) {
    cs.add_command("do", "e", [&cs](TvalRange args) {
        cs.run_ret(args[0].get_code());
    }, ID_DO);

    cs.add_command("doargs", "e", [&cs](TvalRange args) {
        if (cs.stack != &cs.noalias)
            cs_do_args(cs, [&]() { cs.run_ret(args[0].get_code()); });
        else
            cs.run_ret(args[0].get_code());
    }, ID_DOARGS);

    cs.add_command("if", "tee", [&cs](TvalRange args) {
        cs.run_ret((args[0].get_bool() ? args[1] : args[2]).get_code());
    }, ID_IF);

    cs.add_command("result", "T", [&cs](TvalRange args) {
        TaggedValue &v = args[0];
        *cs.result = v;
        v.set_null();
    }, ID_RESULT);

    cs.add_command("!", "t", [&cs](TvalRange args) {
        cs.result->set_int(!args[0].get_bool());
    }, ID_NOT);

    cs.add_command("&&", "E1V", [&cs](TvalRange args) {
        if (args.empty())
            cs.result->set_int(1);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) cs.result->cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code);
            else
                *cs.result = args[i];
            if (!cs.result->get_bool()) break;
        }
    }, ID_AND);

    cs.add_command("||", "E1V", [&cs](TvalRange args) {
        if (args.empty())
            cs.result->set_int(0);
        else for (ostd::Size i = 0; i < args.size(); ++i) {
            if (i) cs.result->cleanup();
            if (args[i].get_type() == VAL_CODE)
                cs.run_ret(args[i].code);
            else
                *cs.result = args[i];
            if (cs.result->get_bool()) break;
        }
    }, ID_OR);

    cs.add_command("?", "tTT", [&cs](TvalRange args) {
        cs.result->set(args[0].get_bool() ? args[1] : args[2]);
    });

    cs.add_command("cond", "ee2V", [&cs](TvalRange args) {
        for (ostd::Size i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run_bool(args[i].code)) {
                    cs.run_ret(args[i + 1].code);
                    break;
                }
            } else {
                cs.run_ret(args[i].code);
                break;
            }
        }
    });

#define CS_CMD_CASE(name, fmt, type, acc, compare) \
    cs.add_command(name, fmt "te2V", [&cs](TvalRange args) { \
        type val = ostd::move(acc); \
        ostd::Size i; \
        for (i = 1; (i + 1) < args.size(); i += 2) { \
            if (compare) { \
                cs.run_ret(args[i + 1].code); \
                return; \
            } \
        } \
    });

    CS_CMD_CASE("case", "i", int, args[0].get_int(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_int() == val)));

    CS_CMD_CASE("casef", "f", float, args[0].get_float(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_float() == val)));

    CS_CMD_CASE("cases", "s", ostd::String, args[0].get_str(),
                    ((args[i].get_type() == VAL_NULL) ||
                     (args[i].get_str() == val)));

#undef CS_CMD_CASE

    cs.add_command("pushif", "rTe", [&cs](TvalRange args) {
        Ident *id = args[0].get_ident();
        TaggedValue &v = args[1];
        ostd::Uint32 *code = args[2].get_code();
        if ((id->type != ID_ALIAS) || (id->index < MaxArguments))
            return;
        if (v.get_bool()) {
            IdentStack stack;
            id->push_arg(v, stack);
            v.set_null();
            cs.run_ret(code);
            id->pop_arg();
        }
    });

    cs_init_lib_base_loops(cs);
    cs_init_lib_base_var(cs);
}

static inline void cs_set_iter(Ident &id, int i, IdentStack &stack) {
    if (id.stack == &stack) {
        if (id.get_valtype() != VAL_INT) {
            if (id.get_valtype() == VAL_STR) {
                delete[] id.val.s;
                id.val.s = nullptr;
                id.val.len = 0;
            }
            id.clean_code();
            id.valtype = VAL_INT;
        }
        id.val.i = i;
        return;
    }
    TaggedValue v;
    v.set_int(i);
    id.push_arg(v, stack);
}

static inline void cs_do_loop(CsState &cs, Ident &id, int offset, int n,
                              int step, ostd::Uint32 *cond, ostd::Uint32 *body) {
    if (n <= 0 || (id.type != ID_ALIAS))
        return;
    IdentStack stack;
    for (int i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        if (cond && !cs.run_bool(cond)) break;
        cs.run_int(body);
    }
    id.pop_arg();
}

static inline void cs_loop_conc(CsState &cs, Ident &id, int offset, int n,
                                int step, ostd::Uint32 *body, bool space) {
    if (n <= 0 || id.type != ID_ALIAS)
        return;
    IdentStack stack;
    ostd::Vector<char> s;
    for (int i = 0; i < n; ++i) {
        cs_set_iter(id, offset + i * step, stack);
        TaggedValue v;
        cs.run_ret(body, v);
        ostd::String vstr = ostd::move(v.get_str());
        if (space && i) s.push(' ');
        s.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n > 0) id.pop_arg();
    s.push('\0');
    ostd::Size len = s.size() - 1;
    cs.result->set_mstr(ostd::CharRange(s.disown(), len));
}

static void cs_init_lib_base_loops(CsState &cs) {
    cs.add_command("loop", "rie", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    cs.add_command("loop+", "riie", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    cs.add_command("loop*", "riie", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    cs.add_command("loop+*", "riiie", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    cs.add_command("loopwhile", "riee", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    cs.add_command("loopwhile+", "riiee", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    cs.add_command("loopwhile*", "riiee", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    cs.add_command("loopwhile+*", "riiiee", [&cs](TvalRange args) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    cs.add_command("while", "ee", [&cs](TvalRange args) {
        ostd::Uint32 *cond = args[0].get_code(), *body = args[1].get_code();
        while (cs.run_bool(cond)) {
            cs.run_int(body);
        }
    });

    cs.add_command("loopconcat", "rie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    cs.add_command("loopconcat+", "riie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), true
        );
    });

    cs.add_command("loopconcat*", "riie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), true
        );
    });

    cs.add_command("loopconcat+*", "riiie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), true
        );
    });

    cs.add_command("loopconcatword", "rie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    cs.add_command("loopconcatword+", "riie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), false
        );
    });

    cs.add_command("loopconcatword*", "riie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), false
        );
    });

    cs.add_command("loopconcatword+*", "riiie", [&cs](TvalRange args) {
        cs_loop_conc(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });
}

} /* namespace cscript */
