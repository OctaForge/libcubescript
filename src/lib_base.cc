#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_vm.hh" // FIXME, only Max Arguments

namespace cscript {

static inline void cs_do_loop(
    cs_state &cs, cs_ident &id, cs_int offset, cs_int n, cs_int step,
    cs_bcode *cond, cs_bcode *body
) {
    cs_stacked_value idv{cs, &id};
    if (n <= 0 || !idv.has_alias()) {
        return;
    }
    for (cs_int i = 0; i < n; ++i) {
        idv.set_int(offset + i * step);
        idv.push();
        if (cond && !cs.run(cond).get_bool()) {
            break;
        }
        switch (cs.run_loop(body)) {
            case cs_loop_state::BREAK:
                goto end;
            default: /* continue and normal */
                break;
        }
    }
end:
    return;
}

static inline void cs_loop_conc(
    cs_state &cs, cs_value &res, cs_ident &id, cs_int offset, cs_int n,
    cs_int step, cs_bcode *body, bool space
) {
    cs_stacked_value idv{cs, &id};
    if (n <= 0 || !idv.has_alias()) {
        return;
    }
    cs_charbuf s{cs};
    for (cs_int i = 0; i < n; ++i) {
        idv.set_int(offset + i * step);
        idv.push();
        cs_value v{cs};
        switch (cs.run_loop(body, v)) {
            case cs_loop_state::BREAK:
                goto end;
            case cs_loop_state::CONTINUE:
                continue;
            default:
                break;
        }
        if (space && i) {
            s.push_back(' ');
        }
        s.append(v.get_str());
    }
end:
    res.set_str(s.str());
}

void cs_init_lib_base(cs_state &gcs) {
    gcs.new_command("error", "s", [](auto &cs, auto args, auto &) {
        throw cs_error(cs, args[0].get_str());
    });

    gcs.new_command("pcall", "err", [](auto &cs, auto args, auto &ret) {
        cs_alias *cret = args[1].get_ident()->get_alias(),
                *css  = args[2].get_ident()->get_alias();
        if (!cret || !css) {
            ret.set_int(0);
            return;
        }
        cs_value result{cs}, tback{cs};
        bool rc = true;
        try {
            cs.run(args[0].get_code(), result);
        } catch (cs_error const &e) {
            result.set_str(e.what());
            if (e.get_stack().get()) {
                cs_charbuf buf{cs};
                cs_print_stack(std::back_inserter(buf), e.get_stack());
                tback.set_str(buf.str());
            }
            rc = false;
        }
        ret.set_int(rc);
        static_cast<cs_alias_impl *>(cret)->set_alias(cs, result);
        static_cast<cs_alias_impl *>(css)->set_alias(cs, tback);
    });

    gcs.new_command("?", "ttt", [](auto &, auto args, auto &res) {
        if (args[0].get_bool()) {
            res = args[1];
        } else {
            res = args[2];
        }
    });

    gcs.new_command("cond", "ee2V", [](auto &cs, auto args, auto &res) {
        for (size_t i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run(args[i].get_code()).get_bool()) {
                    cs.run(args[i + 1].get_code(), res);
                    break;
                }
            } else {
                cs.run(args[i].get_code(), res);
                break;
            }
        }
    });

    gcs.new_command("case", "ite2V", [](auto &cs, auto args, auto &res) {
        cs_int val = args[0].get_int();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_int() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("casef", "fte2V", [](auto &cs, auto args, auto &res) {
        cs_float val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("cases", "ste2V", [](auto &cs, auto args, auto &res) {
        cs_strref val = args[0].get_str();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == cs_value_type::NONE) ||
                (args[i].get_str() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("pushif", "rte", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias() || (idv.get_alias()->get_index() < MaxArguments)) {
            return;
        }
        if (args[1].get_bool()) {
            idv = args[1];
            idv.push();
            cs.run(args[2].get_code(), res);
        }
    });

    gcs.new_command("loop", "rie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    gcs.new_command("loop+", "riie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop*", "riie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop+*", "riiie", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    gcs.new_command("loopwhile", "riee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    gcs.new_command("loopwhile+", "riiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile*", "riiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile+*", "riiiee", [](auto &cs, auto args, auto &) {
        cs_do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    gcs.new_command("while", "ee", [](auto &cs, auto args, auto &) {
        cs_bcode *cond = args[0].get_code(), *body = args[1].get_code();
        while (cs.run(cond).get_bool()) {
            switch (cs.run_loop(body)) {
                case cs_loop_state::BREAK:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    gcs.new_command("loopconcat", "rie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    gcs.new_command("loopconcat+", "riie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat*", "riie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat+*", "riiie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[3].get_int(), args[2].get_int(), args[4].get_code(), true
        );
    });

    gcs.new_command("loopconcatword", "rie", [](auto &cs, auto args, auto &res) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword*", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+*", "riiie", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });

    gcs.new_command("push", "rte", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias() || (idv.get_alias()->get_index() < MaxArguments)) {
            return;
        }
        idv = args[1];
        idv.push();
        cs.run(args[2].get_code(), res);
    });

    gcs.new_command("resetvar", "s", [](auto &cs, auto args, auto &) {
        cs.reset_var(args[0].get_str());
    });

    gcs.new_command("alias", "st", [](auto &cs, auto args, auto &) {
        cs.set_alias(args[0].get_str(), args[1]);
    });

    gcs.new_command("getvarmin", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.get_var_min_int(args[0].get_str()).value_or(0));
    });
    gcs.new_command("getvarmax", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.get_var_max_int(args[0].get_str()).value_or(0));
    });
    gcs.new_command("getfvarmin", "s", [](auto &cs, auto args, auto &res) {
        res.set_float(cs.get_var_min_float(args[0].get_str()).value_or(0.0f));
    });
    gcs.new_command("getfvarmax", "s", [](auto &cs, auto args, auto &res) {
        res.set_float(cs.get_var_max_float(args[0].get_str()).value_or(0.0f));
    });

    gcs.new_command("identexists", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(cs.have_ident(args[0].get_str()));
    });

    gcs.new_command("getalias", "s", [](auto &cs, auto args, auto &res) {
        auto s0 = cs.get_alias_val(args[0].get_str());
        if (s0) {
            res.set_str(*s0);
        } else {
            res.set_str("");
        }
    });
}

} /* namespace cscript */
