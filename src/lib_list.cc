#include <functional>
#include <iterator>

#include <cubescript/cubescript.hh>
#include "cs_std.hh"
#include "cs_parser.hh"
#include "cs_thread.hh"

namespace cubescript {

template<typename T>
struct arg_val;

template<>
struct arg_val<integer_type> {
    static integer_type get(any_value &tv, state &) {
        return tv.get_integer();
    }
};

template<>
struct arg_val<float_type> {
    static float_type get(any_value &tv, state &) {
        return tv.get_float();
    }
};

template<>
struct arg_val<std::string_view> {
    static std::string_view get(any_value &tv, state &cs) {
        return tv.get_string(cs);
    }
};

template<typename T, typename F>
static inline void list_find(
    state &cs, span_type<any_value> args, any_value &res, F cmp
) {
    integer_type n = 0, skip = args[2].get_integer();
    T val = arg_val<T>::get(args[1], cs);
    for (list_parser p{cs, args[0].get_string(cs)}; p.parse(); ++n) {
        if (cmp(p, val)) {
            res.set_integer(n);
            return;
        }
        for (int i = 0; i < skip; ++i) {
            if (!p.parse()) {
                goto notfound;
            }
            ++n;
        }
    }
notfound:
    res.set_integer(-1);
}

template<typename T, typename F>
static inline void list_assoc(
    state &cs, span_type<any_value> args, any_value &res, F cmp
) {
    T val = arg_val<T>::get(args[1], cs);
    for (list_parser p{cs, args[0].get_string(cs)}; p.parse();) {
        if (cmp(p, val)) {
            if (p.parse()) {
                res.set_string(p.get_item());
            }
            return;
        }
        if (!p.parse()) {
            break;
        }
    }
}

static void loop_list_conc(
    state &cs, any_value &res, ident &id, std::string_view list,
    bcode_ref &&body, bool space
) {
    alias_local st{cs, id};
    any_value idv{};
    charbuf r{cs};
    int n = 0;
    for (list_parser p{cs, list}; p.parse(); ++n) {
        idv.set_string(p.get_item());
        st.set(std::move(idv));
        if (n && space) {
            r.push_back(' ');
        }
        any_value v{};
        switch (body.call_loop(cs, v)) {
            case loop_state::BREAK:
                goto end;
            case loop_state::CONTINUE:
                continue;
            default:
                break;
        }
        r.append(v.get_string(cs));
    }
end:
    res.set_string(r.str(), cs);
}

int list_includes(
    state &cs, std::string_view list, std::string_view needle
) {
    int offset = 0;
    for (list_parser p{cs, list}; p.parse();) {
        if (p.raw_item() == needle) {
            return offset;
        }
        ++offset;
    }
    return -1;
}

template<bool PushList, bool Swap, typename F>
static inline void list_merge(
    state &cs, span_type<any_value> args, any_value &res, F cmp
) {
    std::string_view list = args[0].get_string(cs);
    std::string_view elems = args[1].get_string(cs);
    charbuf buf{cs};
    if (PushList) {
        buf.append(list);
    }
    if (Swap) {
        std::swap(list, elems);
    }
    for (list_parser p{cs, list}; p.parse();) {
        if (cmp(list_includes(cs, elems, p.raw_item()), 0)) {
            if (!buf.empty()) {
                buf.push_back(' ');
            }
            buf.append(p.quoted_item());
        }
    }
    res.set_string(buf.str(), cs);
}

static void init_lib_list_sort(state &cs);

LIBCUBESCRIPT_EXPORT void std_init_list(state &gcs) {
    new_cmd_quiet(gcs, "listlen", "s", [](auto &cs, auto args, auto &res) {
        res.set_integer(
            integer_type(list_parser{cs, args[0].get_string(cs)}.count())
        );
    });

    new_cmd_quiet(gcs, "at", "si1...", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        if (args.size() <= 1) {
            res = args[0];
            return;
        }
        auto str = args[0].get_string(cs);
        list_parser p{cs, str};
        for (size_t i = 1; i < args.size(); ++i) {
            p.set_input(str);
            integer_type pos = args[i].get_integer();
            for (; pos > 0; --pos) {
                if (!p.parse()) {
                    break;
                }
            }
            if (pos > 0 || !p.parse()) {
                p.set_input("");
            }
        }
        res.set_string(p.get_item());
    });

    new_cmd_quiet(gcs, "sublist", "sii#", [](auto &cs, auto args, auto &res) {
        integer_type skip   = args[1].get_integer(),
              count   = args[2].get_integer(),
              numargs = args[3].get_integer();

        integer_type offset = std::max(skip, integer_type(0)),
              len = (numargs >= 3) ? std::max(count, integer_type(0)) : -1;

        list_parser p{cs, args[0].get_string(cs)};
        for (integer_type i = 0; i < offset; ++i) {
            if (!p.parse()) break;
        }
        if (len < 0) {
            if (offset > 0) {
                p.skip_until_item();
            }
            res.set_string(p.input(), cs);
            return;
        }

        char const *list = p.input().data();
        if (len > 0 && p.parse()) {
            while (--len > 0 && p.parse());
        } else {
            res.set_string("", cs);
            return;
        }
        auto quote = p.quoted_item();
        auto *qend = quote.data() + quote.size();
        res.set_string(make_str_view(list, qend), cs);
    });

    new_cmd_quiet(gcs, "listfind", "vsb", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        any_value idv{};
        auto body = args[2].get_code();
        int n = -1;
        for (list_parser p{cs, args[1].get_string(cs)}; p.parse();) {
            ++n;
            idv.set_string(p.raw_item(), cs);
            st.set(std::move(idv));
            if (body.call(cs).get_bool()) {
                res.set_integer(integer_type(n));
                return;
            }
        }
        res.set_integer(-1);
    });

    new_cmd_quiet(gcs, "listassoc", "vsb", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        any_value idv{};
        auto body = args[2].get_code();
        for (list_parser p{cs, args[1].get_string(cs)}; p.parse();) {
            idv.set_string(p.raw_item(), cs);
            st.set(std::move(idv));
            if (body.call(cs).get_bool()) {
                if (p.parse()) {
                    res.set_string(p.get_item());
                }
                break;
            }
            if (!p.parse()) {
                break;
            }
        }
    });

    new_cmd_quiet(gcs, "listfind=", "i", [](auto &cs, auto args, auto &res) {
        list_find<integer_type>(
            cs, args, res, [](list_parser const &p, integer_type val) {
                return parse_int(p.raw_item()) == val;
            }
        );
    });
    new_cmd_quiet(gcs, "listfind=f", "f", [](auto &cs, auto args, auto &res) {
        list_find<float_type>(
            cs, args, res, [](list_parser const &p, float_type val) {
                return parse_float(p.raw_item()) == val;
            }
        );
    });
    new_cmd_quiet(gcs, "listfind=s", "s", [](auto &cs, auto args, auto &res) {
        list_find<std::string_view>(
            cs, args, res, [](list_parser const &p, std::string_view val) {
                return p.raw_item() == val;
            }
        );
    });

    new_cmd_quiet(gcs, "listassoc=", "i", [](auto &cs, auto args, auto &res) {
        list_assoc<integer_type>(
            cs, args, res, [](list_parser const &p, integer_type val) {
                return parse_int(p.raw_item()) == val;
            }
        );
    });
    new_cmd_quiet(gcs, "listassoc=f", "f", [](auto &cs, auto args, auto &res) {
        list_assoc<float_type>(
            cs, args, res, [](list_parser const &p, float_type val) {
                return parse_float(p.raw_item()) == val;
            }
        );
    });
    new_cmd_quiet(gcs, "listassoc=s", "s", [](auto &cs, auto args, auto &res) {
        list_assoc<std::string_view>(
            cs, args, res, [](list_parser const &p, std::string_view val) {
                return p.raw_item() == val;
            }
        );
    });

    new_cmd_quiet(gcs, "looplist", "vsb", [](auto &cs, auto args, auto &) {
        alias_local st{cs, args[0]};
        any_value idv{};
        auto body = args[2].get_code();
        for (list_parser p{cs, args[1].get_string(cs)}; p.parse();) {
            idv.set_string(p.get_item());
            st.set(std::move(idv));
            switch (body.call_loop(cs)) {
                case loop_state::BREAK:
                    return;
                default: /* continue and normal */
                    break;
            }
        }
    });

    new_cmd_quiet(gcs, "looplist2", "vvsb", [](auto &cs, auto args, auto &) {
        alias_local st1{cs, args[0]};
        alias_local st2{cs, args[1]};
        any_value idv{};
        auto body = args[3].get_code();
        for (list_parser p{cs, args[2].get_string(cs)}; p.parse();) {
            idv.set_string(p.get_item());
            st1.set(std::move(idv));
            if (p.parse()) {
                idv.set_string(p.get_item());
            } else {
                idv.set_string("", cs);
            }
            st2.set(std::move(idv));
            switch (body.call_loop(cs)) {
                case loop_state::BREAK:
                    return;
                default: /* continue and normal */
                    break;
            }
        }
    });

    new_cmd_quiet(gcs, "looplist3", "vvvsb", [](auto &cs, auto args, auto &) {
        alias_local st1{cs, args[0]};
        alias_local st2{cs, args[1]};
        alias_local st3{cs, args[2]};
        any_value idv{};
        auto body = args[4].get_code();
        for (list_parser p{cs, args[3].get_string(cs)}; p.parse();) {
            idv.set_string(p.get_item());
            st1.set(std::move(idv));
            if (p.parse()) {
                idv.set_string(p.get_item());
            } else {
                idv.set_string("", cs);
            }
            st2.set(std::move(idv));
            if (p.parse()) {
                idv.set_string(p.get_item());
            } else {
                idv.set_string("", cs);
            }
            st3.set(std::move(idv));
            switch (body.call_loop(cs)) {
                case loop_state::BREAK:
                    return;
                default: /* continue and normal */
                    break;
            }
        }
    });

    new_cmd_quiet(gcs, "looplistconcat", "vsb", [](
        auto &cs, auto args, auto &res
    ) {
        loop_list_conc(
            cs, res, args[0].get_ident(cs), args[1].get_string(cs),
            args[2].get_code(), true
        );
    });

    new_cmd_quiet(gcs, "looplistconcatword", "vsb", [](
        auto &cs, auto args, auto &res
    ) {
        loop_list_conc(
            cs, res, args[0].get_ident(cs), args[1].get_string(cs),
            args[2].get_code(), false
        );
    });

    new_cmd_quiet(gcs, "listfilter", "vsb", [](
        auto &cs, auto args, auto &res
    ) {
        alias_local st{cs, args[0]};
        any_value idv{};
        auto body = args[2].get_code();
        charbuf r{cs};
        for (list_parser p{cs, args[1].get_string(cs)}; p.parse();) {
            idv.set_string(p.raw_item(), cs);
            st.set(std::move(idv));
            if (body.call(cs).get_bool()) {
                if (r.size()) {
                    r.push_back(' ');
                }
                r.append(p.quoted_item());
            }
        }
        res.set_string(r.str(), cs);
    });

    new_cmd_quiet(gcs, "listcount", "vsb", [](auto &cs, auto args, auto &res) {
        alias_local st{cs, args[0]};
        any_value idv{};
        auto body = args[2].get_code();
        int r = 0;
        for (list_parser p{cs, args[1].get_string(cs)}; p.parse();) {
            idv.set_string(p.raw_item(), cs);
            st.set(std::move(idv));
            if (body.call(cs).get_bool()) {
                r++;
            }
        }
        res.set_integer(r);
    });

    new_cmd_quiet(gcs, "prettylist", "ss", [](auto &cs, auto args, auto &res) {
        charbuf buf{cs};
        std::string_view s = args[0].get_string(cs);
        std::string_view conj = args[1].get_string(cs);
        list_parser p{cs, s};
        size_t len = p.count();
        size_t n = 0;
        for (p.set_input(s); p.parse(); ++n) {
            auto qi = p.quoted_item();
            if (!qi.empty() && (qi.front() == '"')) {
                unescape_string(std::back_inserter(buf), p.raw_item());
            } else {
                buf.append(p.raw_item());
            }
            if ((n + 1) < len) {
                if ((len > 2) || conj.empty()) {
                    buf.push_back(',');
                }
                if ((n + 2 == len) && !conj.empty()) {
                    buf.push_back(' ');
                    buf.append(conj);
                }
                buf.push_back(' ');
            }
        }
        res.set_string(buf.str(), cs);
    });

    new_cmd_quiet(gcs, "indexof", "ss", [](auto &cs, auto args, auto &res) {
        res.set_integer(
            list_includes(cs, args[0].get_string(cs), args[1].get_string(cs))
        );
    });

    new_cmd_quiet(gcs, "listdel", "ss", [](auto &cs, auto args, auto &res) {
        list_merge<false, false>(cs, args, res, std::less<int>());
    });
    new_cmd_quiet(gcs, "listintersect", "ss", [](
        auto &cs, auto args, auto &res
    ) {
        list_merge<false, false>(cs, args, res, std::greater_equal<int>());
    });
    new_cmd_quiet(gcs, "listunion", "ss", [](auto &cs, auto args, auto &res) {
        list_merge<true, true>(cs, args, res, std::less<int>());
    });

    new_cmd_quiet(gcs, "listsplice", "ssii", [](
        auto &cs, auto args, auto &res
    ) {
        integer_type offset = std::max(args[2].get_integer(), integer_type(0));
        integer_type len    = std::max(args[3].get_integer(), integer_type(0));
        std::string_view s = args[0].get_string(cs);
        std::string_view vals = args[1].get_string(cs);
        char const *list = s.data();
        list_parser p{cs, s};
        for (integer_type i = 0; i < offset; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        std::string_view quote = p.quoted_item();
        char const *qend = !quote.empty() ? quote.data() + quote.size() : list;
        charbuf buf{cs};
        if (qend > list) {
            buf.append(list, qend);
        }
        if (!vals.empty()) {
            if (!buf.empty()) {
                buf.push_back(' ');
            }
            buf.append(vals);
        }
        for (integer_type i = 0; i < len; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        p.skip_until_item();
        if (!p.input().empty()) {
            switch (p.input().front()) {
                case ')':
                case ']':
                    break;
                default:
                    if (!buf.empty()) {
                        buf.push_back(' ');
                    }
                    buf.append(p.input());
                    break;
            }
        }
        res.set_string(buf.str(), cs);
    });

    init_lib_list_sort(gcs);
}

struct ListSortItem {
    std::string_view str;
    std::string_view quote;
};

struct ListSortFun {
    state &cs;
    alias_local &xst, &yst;
    bcode_ref const *body;

    bool operator()(ListSortItem const &xval, ListSortItem const &yval) {
        any_value v{};
        v.set_string(xval.str, cs);
        xst.set(std::move(v));
        v.set_string(yval.str, cs);
        yst.set(std::move(v));
        return body->call(cs).get_bool();
    }
};

static void list_sort(
    state &cs, any_value &res, std::string_view list,
    ident &x, ident &y, bcode_ref &&body, bcode_ref &&unique
) {
    if (x == y) {
        return;
    }

    alias_local xst{cs, x}, yst{cs, y};

    valbuf<ListSortItem> items{state_p{cs}.ts().istate};
    size_t total = 0;

    for (list_parser p{cs, list}; p.parse();) {
        ListSortItem item = { p.raw_item(), p.quoted_item() };
        items.push_back(item);
        total += item.quote.size();
    }

    if (items.empty()) {
        res.set_string(list, cs);
        return;
    }

    size_t totaluniq = total;
    size_t nuniq = items.size();
    if (body) {
        ListSortFun f = { cs, xst, yst, &body };
        std::sort(items.buf.begin(), items.buf.end(), f);
        if (!unique.empty()) {
            f.body = &unique;
            totaluniq = items[0].quote.size();
            nuniq = 1;
            for (size_t i = 1; i < items.size(); i++) {
                ListSortItem &item = items[i];
                if (f(items[i - 1], item)) {
                    item.quote = std::string_view{};
                } else {
                    totaluniq += item.quote.size();
                    ++nuniq;
                }
            }
        }
    } else {
        ListSortFun f = { cs, xst, yst, &unique };
        totaluniq = items[0].quote.size();
        nuniq = 1;
        for (size_t i = 1; i < items.size(); i++) {
            ListSortItem &item = items[i];
            for (size_t j = 0; j < i; ++j) {
                ListSortItem &prev = items[j];
                if (!prev.quote.empty() && f(item, prev)) {
                    item.quote = std::string_view{};
                    break;
                }
            }
            if (!item.quote.empty()) {
                totaluniq += item.quote.size();
                ++nuniq;
            }
        }
    }

    charbuf sorted{cs};
    sorted.reserve(totaluniq + std::max(nuniq - 1, size_t(0)));
    for (size_t i = 0; i < items.size(); ++i) {
        ListSortItem &item = items[i];
        if (item.quote.empty()) {
            continue;
        }
        if (i) {
            sorted.push_back(' ');
        }
        sorted.append(item.quote);
    }
    res.set_string(sorted.str(), cs);
}

static void init_lib_list_sort(state &gcs) {
    new_cmd_quiet(gcs, "sortlist", "svvbb", [](
        auto &cs, auto args, auto &res
    ) {
        list_sort(
            cs, res, args[0].get_string(cs), args[1].get_ident(cs),
            args[2].get_ident(cs), args[3].get_code(), args[4].get_code()
        );
    });
    new_cmd_quiet(gcs, "uniquelist", "svvb", [](
        auto &cs, auto args, auto &res
     ) {
        list_sort(
            cs, res, args[0].get_string(cs), args[1].get_ident(cs),
            args[2].get_ident(cs), bcode_ref{}, args[3].get_code()
        );
    });
}

} /* namespace cubescript */
