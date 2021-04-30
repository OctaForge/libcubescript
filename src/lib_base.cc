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
        if (cond && !cs.call(cond).get_bool()) {
            break;
        }
        switch (cs.call_loop(body)) {
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
        switch (cs.call_loop(body, v)) {
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

    new_cmd_quiet(gcs, "pcall", "bvv", [](auto &cs, auto args, auto &ret) {
        auto &cret = args[1].get_ident(cs);
        auto &css = args[2].get_ident(cs);
        if (!cret.is_alias()) {
            throw error{cs, "'%s' is not an alias", cret.get_name().data()};
        }
        if (!css.is_alias()) {
            throw error{cs, "'%s' is not an alias", css.get_name().data()};
        }
        any_value result{}, tback{};
        bool rc = true;
        try {
            result = cs.call(args[0].get_code());
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
        auto *reta = static_cast<alias *>(&cret);
        auto *ssa = static_cast<alias *>(&css);
        ts.get_astack(reta).set_alias(reta, ts, result);
        ts.get_astack(ssa).set_alias(ssa, ts, tback);
    });

    new_cmd_quiet(gcs, "?", "aaa", [](auto &, auto args, auto &res) {
        if (args[0].get_bool()) {
            res = std::move(args[1]);
        } else {
            res = std::move(args[2]);
        }
    });

    new_cmd_quiet(gcs, "cond", "bb2...", [](auto &cs, auto args, auto &res) {
        for (size_t i = 0; i < args.size(); i += 2) {
            if ((i + 1) < args.size()) {
                if (cs.call(args[i].get_code()).get_bool()) {
                    res = cs.call(args[i + 1].get_code());
                    break;
                }
            } else {
                res = cs.call(args[i].get_code());
                break;
            }
        }
    });

    new_cmd_quiet(gcs, "case", "iab2...", [](auto &cs, auto args, auto &res) {
        integer_type val = args[0].get_integer();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_integer() == val)
            ) {
                res = cs.call(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "casef", "fab2...", [](auto &cs, auto args, auto &res) {
        float_type val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                res = cs.call(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "cases", "sab2...", [](auto &cs, auto args, auto &res) {
        string_ref val = args[0].get_string(cs);
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].get_type() == value_type::NONE) ||
                (args[i].get_string(cs) == val)
            ) {
                res = cs.call(args[i + 1].get_code());
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "pushif", "vab", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        if (st.get_alias().is_arg()) {
            throw error{cs, "cannot push an argument"};
        }
        if (args[1].get_bool()) {
            st.set(args[1]);
            res = cs.call(args[2].get_code());
        }
    });

    new_cmd_quiet(gcs, "loop", "vab", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            bcode_ref{}, args[2].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop+", "viib", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, bcode_ref{}, args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop*", "viib", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(),
            args[2].get_integer(), bcode_ref{}, args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loop+*", "viiib", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            bcode_ref{}, args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile", "vibb", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), args[3].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile+", "viibb", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile*", "viibb", [](auto &cs, auto args, auto &) {
        do_loop(
            cs, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), args[4].get_code()
        );
    });

    new_cmd_quiet(gcs, "loopwhile+*", "viiibb", [](
        auto &cs, auto args, auto &
    ) {
        do_loop(
            cs, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(), args[4].get_code(),
            args[5].get_code()
        );
    });

    new_cmd_quiet(gcs, "while", "bb", [](auto &cs, auto args, auto &) {
        auto cond = args[0].get_code();
        auto body = args[1].get_code();
        while (cs.call(cond).get_bool()) {
            switch (cs.call_loop(body)) {
                case loop_state::BREAK:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    new_cmd_quiet(gcs, "loopconcat", "vib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat+", "viib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat*", "viib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcat+*", "viiib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            args[4].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "loopconcatword", "vib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[1].get_integer(), 1,
            args[2].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword+", "viib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[2].get_integer(), 1, args[3].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword*", "viib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), 0, args[2].get_integer(),
            args[1].get_integer(), args[3].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "loopconcatword+*", "viiib", [](
        auto &cs, auto args, auto &res
    ) {
        do_loop_conc(
            cs, res, args[0].get_ident(cs), args[1].get_integer(),
            args[3].get_integer(), args[2].get_integer(),
            args[4].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "push", "vab", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        if (st.get_alias().is_arg()) {
            throw error{cs, "cannot push an argument"};
        }
        st.set(args[1]);
        res = cs.call(args[2].get_code());
    });

    new_cmd_quiet(gcs, "resetvar", "s", [](auto &cs, auto args, auto &) {
        cs.reset_value(args[0].get_string(cs));
    });

    new_cmd_quiet(gcs, "alias", "sa", [](auto &cs, auto args, auto &) {
        cs.assign_value(args[0].get_string(cs), args[1]);
    });

    new_cmd_quiet(gcs, "identexists", "s", [](auto &cs, auto args, auto &res) {
        res.set_integer(cs.get_ident(args[0].get_string(cs)) != std::nullopt);
    });

    new_cmd_quiet(gcs, "getalias", "s", [](auto &cs, auto args, auto &res) {
        auto &id = cs.new_ident(args[0].get_string(cs));
        if (id.get_type() != ident_type::ALIAS) {
            throw error{cs, "'%s' is not an alias", id.get_name().data()};
        }
        if (ident_p{id}.impl().p_flags & IDENT_FLAG_UNKNOWN) {
            return;
        }
        res = static_cast<alias &>(id).get_value(cs);
    });
}

} /* namespace cubescript */
