#include <cubescript/cubescript.hh>

#include <iterator>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"
#include "cs_error.hh"

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
        if (cond && !cond.call(cs).get_bool()) {
            break;
        }
        switch (body.call_loop(cs)) {
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
        switch (body.call_loop(cs, v)) {
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

    new_cmd_quiet(gcs, "pcall", "bvvvb", [](auto &cs, auto args, auto &ret) {
        auto &ts = state_p{cs}.ts();
        auto &cret = args[1].get_ident(cs);
        if (cret.type() != ident_type::ALIAS) {
            throw error_p::make(cs, "'%s' is not an alias", cret.name().data());
        }
        auto *ra = static_cast<alias *>(&cret);
        any_value result{};
        try {
            result = args[0].get_code().call(cs);
        } catch (error const &e) {
            auto val = any_value{e.what(), cs};
            auto tb = any_value{};
            val.set_string(e.what(), cs);
            ts.get_astack(ra).set_alias(ra, ts, val);
            if (auto nds = e.stack(); !nds.empty()) {
                auto bc = args[4].get_code();
                if (!bc.empty()) {
                    alias_local ist{cs, args[2].get_ident(cs)};
                    alias_local vst{cs, args[3].get_ident(cs)};
                    any_value idv{};
                    for (auto &nd: nds) {
                        idv.set_integer(integer_type(nd.index));
                        ist.set(idv);
                        idv.set_string(nd.id.name().data(), cs);
                        vst.set(idv);
                        bc.call(cs);
                    }
                }
            }
            ret.set_integer(0);
            return;
        }
        ret.set_integer(1);
        ts.get_astack(ra).set_alias(ra, ts, result);
    });

    new_cmd_quiet(gcs, "assert", "ss#", [](auto &s, auto args, auto &ret) {
        auto val = args[0];
        val.force_code(s);
        if (!val.get_code().call(s).get_bool()) {
            if (args[2].get_integer() > 1) {
                throw error_p::make(
                    s, "assertion failed: [%s] (%s)",
                    args[0].get_string(s).data(), args[1].get_string(s).data()
                );
            } else {
                throw error_p::make(
                    s, "assertion failed: [%s]",
                    args[0].get_string(s).data()
                );
            }
        }
        ret = std::move(args[0]);
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
                if (args[i].get_code().call(cs).get_bool()) {
                    res = args[i + 1].get_code().call(cs);
                    break;
                }
            } else {
                res = args[i].get_code().call(cs);
                break;
            }
        }
    });

    new_cmd_quiet(gcs, "case", "iab2...", [](auto &cs, auto args, auto &res) {
        integer_type val = args[0].get_integer();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].type() == value_type::NONE) ||
                (args[i].get_integer() == val)
            ) {
                res = args[i + 1].get_code().call(cs);
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "casef", "fab2...", [](auto &cs, auto args, auto &res) {
        float_type val = args[0].get_float();
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].type() == value_type::NONE) ||
                (args[i].get_float() == val)
            ) {
                res = args[i + 1].get_code().call(cs);
                return;
            }
        }
    });

    new_cmd_quiet(gcs, "cases", "sab2...", [](auto &cs, auto args, auto &res) {
        string_ref val = args[0].get_string(cs);
        for (size_t i = 1; (i + 1) < args.size(); i += 2) {
            if (
                (args[i].type() == value_type::NONE) ||
                (args[i].get_string(cs) == val)
            ) {
                res = args[i + 1].get_code().call(cs);
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
            res = args[2].get_code().call(cs);
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
        while (cond.call(cs).get_bool()) {
            switch (body.call_loop(cs)) {
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
        res = args[2].get_code().call(cs);
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
        if (id.type() != ident_type::ALIAS) {
            throw error_p::make(cs, "'%s' is not an alias", id.name().data());
        }
        if (ident_p{id}.impl().p_flags & IDENT_FLAG_UNKNOWN) {
            return;
        }
        res = static_cast<alias &>(id).value(cs);
    });
}

} /* namespace cubescript */
