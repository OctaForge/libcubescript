#include <cubescript/cubescript.hh>

#include <iterator>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"

namespace cubescript {

static inline void do_loop(
    state &cs, ident &id, integer_type offset, integer_type n, integer_type step,
    bcode_ref &&cond, bcode_ref &&body
) {
    if (n <= 0) {
        return;
    }
    alias_local st{cs, id};
    any_value idv{};
    for (integer_type i = 0; i < n; ++i) {
        idv.set_integer(offset + i * step);
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

static inline void do_loop_conc(
    state &cs, any_value &res, ident &id, integer_type offset, integer_type n,
    integer_type step, bcode_ref &&body, bool space
) {
    if (n <= 0) {
        return;
    }
    alias_local st{cs, id};
    charbuf s{cs};
    any_value idv{};
    for (integer_type i = 0; i < n; ++i) {
        idv.set_integer(offset + i * step);
        st.set(idv);
        any_value v{};
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
        s.append(v.get_string(cs));
    }
end:
    res.set_string(s.str(), cs);
}

LIBCUBESCRIPT_EXPORT void std_init_base(state &gcs) {
    new_cmd_quiet(gcs, "error", "s", [](auto &cs, auto args, auto &) {
        throw error{cs, args[0].get_string(cs)};
    });

    new_cmd_quiet(gcs, "pcall", "err", [](auto &cs, auto args, auto &ret) {
        alias *cret = args[1].get_ident(cs).get_alias();
        alias *css = args[2].get_ident(cs).get_alias();
        if (!cret || !css) {
            ret.set_integer(0);
            return;
        }
        any_value result{}, tback{};
        bool rc = true;
        try {
            result = cs.run(args[0].get_code());
        } catch (error const &e) {
            result.set_string(e.what(), cs);
            if (e.get_stack().get()) {
                charbuf buf{cs};
                print_stack(std::back_inserter(buf), e.get_stack());
                tback.set_string(buf.str(), cs);
            }
            rc = false;
        }
        ret.set_integer(rc);
        auto &ts = state_p{cs}.ts();
        ts.get_astack(cret).set_alias(cret, ts, result);
        ts.get_astack(css).set_alias(css, ts, tback);
    });

    new_cmd_quiet(gcs, "?", "ttt", [](auto &, auto args, auto &res) {
        if (args[0].get_bool()) {
            res = std::move(args[1]);
        } else {
            res = std::move(args[2]);
        }
    });

    new_cmd_quiet(gcs, "cond", "ee2V", [](auto &cs, auto args, auto &res) {
        for (size_t i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.run(args[i].get_code()).get_bool()) {
                    res = cs.run(args[i + 1].get_code());
                    break;
                }
            } else {
                res = cs.run(args[i].get_code());
                break;
            }
        }
    });

    new_cmd_quiet(gcs, "case", "ite2V", [](auto &cs, auto args, auto &res) {
        integer_type val = args[0].get_integer();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_integer() == val)
            ) {
                res = cs.run(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "casef", "fte2V", [](auto &cs, auto args, auto &res) {
        float_type val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                res = cs.run(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "cases", "ste2V", [](auto &cs, auto args, auto &res) {
        string_ref val = args[0].get_string(cs);
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_string(cs) == val)
            ) {
                res = cs.run(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "pushif", "rte", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        if (st.get_alias()->is_arg()) {
            throw error{cs, "cannot push an argument"};
        }
        if (args[1].get_bool()) {
            st.set(args[1]);
            res = cs.run(args[2].get_code());
        }
    });

    new_cmd_quiet(gcs, "loop", "rie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            bcode_ref{}, args[2].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop+", "riie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, bcode_ref{}, args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop*", "riie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(),
            args[2].get_integer(), bcode_ref{}, args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop+*", "riiie", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            bcode_ref{}, args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile", "riee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile+", "riiee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile*", "riiee", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile+*", "riiiee", [](
        auto &cs, auto args, auto &
    ) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(), args[4].get_code(),
            args[5].get_code()
        );
    });

    new_cmd_quiet(gcs, "while", "ee", [](auto &cs, auto args, auto &) {
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

    new_cmd_quiet(gcs, "loopconcat", "rie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat+", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat*", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat+*", "riiie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            args[4].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcatword", "rie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword+", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword*", "riie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword+*", "riiie", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            args[4].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "push", "rte", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        if (st.get_alias()->is_arg()) {
            throw error{cs, "cannot push an argument"};
        }
        st.set(args[1]);
        res = cs.run(args[2].get_code());
    });

    new_cmd_quiet(gcs, "resetvar", "s", [](auto &cs, auto args, auto &) {
        cs.reset_var(args[0].get_string(cs));
    });

    new_cmd_quiet(gcs, "alias", "st", [](auto &cs, auto args, auto &) {
        cs.set_alias(args[0].get_string(cs), args[1]);
    });

    new_cmd_quiet(gcs, "identexists", "s", [](auto &cs, auto args, auto &res) {
        res.set_integer(cs.have_ident(args[0].get_string(cs)));
    });

    new_cmd_quiet(gcs, "getalias", "s", [](auto &cs, auto args, auto &res) {
        auto *id = cs.get_alias(args[0].get_string(cs));
        if (id) {
            res = id->get_value(cs);
        }
    });
}

} /* namespace cubescript */
