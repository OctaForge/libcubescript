#include <cubescript/cubescript.hh>

#include <iterator>

#include "cs_std.hh"
#include "cs_ident.hh"

namespace cubescript {

static inline void do_loop(
    state &cs, ident &id, integer_type offset, integer_type n, integer_type step,
    bcode_ref &&cond, bcode_ref &&body
) {
    if (n <= 0) {
        return;
    }
    if (alias_local st{cs, &id}; st) {
        any_value idv{cs};
        for (integer_type i = 0; i < n; ++i) {
            idv.set_int(offset + i * step);
            st.set(idv);
            if (cond && !cs.run(cond).get_bool()) {
                break;
            }
            switch (cs.run_loop(body)) {
                case loop_state::BREAK:
                    return;
                default: /* continue and normal */
                    break;
            }
        }
    }
}

static inline void do_loop_conc(
    state &cs, any_value &res, ident &id, integer_type offset, integer_type n,
    integer_type step, bcode_ref &&body, bool space
) {
    if (n <= 0) {
        return;
    }
    if (alias_local st{cs, &id}; st) {
        charbuf s{cs};
        any_value idv{cs};
        for (integer_type i = 0; i < n; ++i) {
            idv.set_int(offset + i * step);
            st.set(idv);
            any_value v{cs};
            switch (cs.run_loop(body, v)) {
                case loop_state::BREAK:
                    goto end;
                case loop_state::CONTINUE:
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
}

void init_lib_base(state &gcs) {
    gcs.new_command("error", "s", [](auto &cs, auto args, auto &) {
        throw error{cs, args[0].get_str()};
    });

    gcs.new_command("pcall", "err", [](auto &cs, auto args, auto &ret) {
        alias *cret = args[1].get_ident()->get_alias();
        alias *css = args[2].get_ident()->get_alias();
        if (!cret || !css) {
            ret.set_int(0);
            return;
        }
        any_value result{cs}, tback{cs};
        bool rc = true;
        try {
            cs.run(args[0].get_code(), result);
        } catch (error const &e) {
            result.set_str(e.what());
            if (e.get_stack().get()) {
                charbuf buf{cs};
                print_stack(std::back_inserter(buf), e.get_stack());
                tback.set_str(buf.str());
            }
            rc = false;
        }
        ret.set_int(rc);
        static_cast<alias_impl *>(cret)->set_alias(
            *cs.thread_pointer(), result
        );
        static_cast<alias_impl *>(css)->set_alias(
            *cs.thread_pointer(), tback
        );
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
        integer_type val = args[0].get_int();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_int() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("casef", "fte2V", [](auto &cs, auto args, auto &res) {
        float_type val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("cases", "ste2V", [](auto &cs, auto args, auto &res) {
        string_ref val = args[0].get_str();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_str() == val)
            ) {
                cs.run(args[i + 1].get_code(), res);
                return;
            }
        }
    });

    gcs.new_command("pushif", "rte", [](auto &cs, auto args, auto &res) {
        if (alias_local st{cs, args[0].get_ident()}; st) {
            if (st.get_alias()->get_flags() & IDENT_FLAG_ARG) {
                return;
            }
            if (args[1].get_bool()) {
                st.set(args[1]);
                cs.run(args[2].get_code(), res);
            }
        }
    });

    gcs.new_command("loop", "rie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1, nullptr,
            args[2].get_code()
        );
    });

    gcs.new_command("loop+", "riie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop*", "riie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), args[2].get_int(),
            nullptr, args[3].get_code()
        );
    });

    gcs.new_command("loop+*", "riiie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), nullptr, args[4].get_code()
        );
    });

    gcs.new_command("loopwhile", "riee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    gcs.new_command("loopwhile+", "riiee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[2].get_int(), 1,
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile*", "riiee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), 0, args[2].get_int(), args[1].get_int(),
            args[3].get_code(), args[4].get_code()
        );
    });

    gcs.new_command("loopwhile+*", "riiiee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), args[5].get_code()
        );
    });

    gcs.new_command("while", "ee", [](auto &cs, auto args, auto &) {
        auto cond = args[0].get_code();
        auto body = args[1].get_code();
        while (cs.run(cond).get_bool()) {
            switch (cs.run_loop(body)) {
                case loop_state::BREAK:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    gcs.new_command("loopconcat", "rie", [](auto &cs, auto args, auto &res) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), true
        );
    });

    gcs.new_command("loopconcat+", "riie", [](auto &cs, auto args, auto &res) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat*", "riie", [](auto &cs, auto args, auto &res) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), true
        );
    });

    gcs.new_command("loopconcat+*", "riiie", [](auto &cs, auto args, auto &res) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[3].get_int(), args[2].get_int(), args[4].get_code(), true
        );
    });

    gcs.new_command("loopconcatword", "rie", [](auto &cs, auto args, auto &res) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[1].get_int(), 1,
            args[2].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(),
            args[2].get_int(), 1, args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword*", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), 0, args[2].get_int(),
            args[1].get_int(), args[3].get_code(), false
        );
    });

    gcs.new_command("loopconcatword+*", "riiie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, *args[0].get_ident(), args[1].get_int(), args[3].get_int(),
            args[2].get_int(), args[4].get_code(), false
        );
    });

    gcs.new_command("push", "rte", [](auto &cs, auto args, auto &res) {
        if (alias_local st{cs, args[0].get_ident()}; st) {
            if (st.get_alias()->get_flags() & IDENT_FLAG_ARG) {
                return;
            }
            st.set(args[1]);
            cs.run(args[2].get_code(), res);
        }
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

} /* namespace cubescript */
