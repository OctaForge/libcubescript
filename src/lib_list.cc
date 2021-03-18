#include <functional>

#include <cubescript/cubescript.hh>
#include "cs_util.hh"

namespace cscript {

template<typename T>
struct cs_arg_val;

template<>
struct cs_arg_val<cs_int> {
    static cs_int get(cs_value &tv) {
        return tv.get_int();
    }
};

template<>
struct cs_arg_val<cs_float> {
    static cs_float get(cs_value &tv) {
        return tv.get_float();
    }
};

template<>
struct cs_arg_val<ostd::string_range> {
    static ostd::string_range get(cs_value &tv) {
        return tv.get_str();
    }
};

template<typename T, typename F>
static inline void cs_list_find(
    cs_state &cs, cs_value_r args, cs_value &res, F cmp
) {
    cs_int n = 0, skip = args[2].get_int();
    T val = cs_arg_val<T>::get(args[1]);
    for (cs_list_parse_state p{args[0].get_str()}; list_parse(p, cs); ++n) {
        if (cmp(p, val)) {
            res.set_int(n);
            return;
        }
        for (int i = 0; i < skip; ++i) {
            if (!list_parse(p, cs)) {
                goto notfound;
            }
            ++n;
        }
    }
notfound:
    res.set_int(-1);
}

template<typename T, typename F>
static inline void cs_list_assoc(
    cs_state &cs, cs_value_r args, cs_value &res, F cmp
) {
    T val = cs_arg_val<T>::get(args[1]);
    for (cs_list_parse_state p{args[0].get_str()}; list_parse(p, cs);) {
        if (cmp(p, val)) {
            if (list_parse(p, cs)) {
                res.set_str(list_get_item(p, cs));
            }
            return;
        }
        if (!list_parse(p, cs)) {
            break;
        }
    }
}

static void cs_loop_list_conc(
    cs_state &cs, cs_value &res, cs_ident *id, ostd::string_range list,
    cs_bcode *body, bool space
) {
    cs_stacked_value idv{cs, id};
    if (!idv.has_alias()) {
        return;
    }
    cs_string r;
    int n = 0;
    for (cs_list_parse_state p{list}; list_parse(p, cs); ++n) {
        idv.set_str(list_get_item(p, cs));
        idv.push();
        if (n && space) {
            r += ' ';
        }
        cs_value v{cs};
        switch (cs.run_loop(body, v)) {
            case cs_loop_state::BREAK:
                goto end;
            case cs_loop_state::CONTINUE:
                continue;
            default:
                break;
        }
        r += ostd::string_range{v.get_str()};
    }
end:
    res.set_str(r);
}

int cs_list_includes(
    cs_state &cs, ostd::string_range list, ostd::string_range needle
) {
    int offset = 0;
    for (cs_list_parse_state p{list}; list_parse(p, cs);) {
        if (p.item == needle) {
            return offset;
        }
        ++offset;
    }
    return -1;
}

template<bool PushList, bool Swap, typename F>
static inline void cs_list_merge(
    cs_state &cs, cs_value_r args, cs_value &res, F cmp
) {
    ostd::string_range list = args[0].get_str();
    ostd::string_range elems = args[1].get_str();
    cs_string buf;
    if (PushList) {
        buf += list;
    }
    if (Swap) {
        std::swap(list, elems);
    }
    for (cs_list_parse_state p{list}; list_parse(p, cs);) {
        if (cmp(cs_list_includes(cs, elems, p.item), 0)) {
            if (!buf.empty()) {
                buf += ' ';
            }
            buf += p.quoted_item;
        }
    }
    res.set_str(buf);
}

static void cs_init_lib_list_sort(cs_state &cs);

void cs_init_lib_list(cs_state &gcs) {
    gcs.new_command("listlen", "s", [](auto &cs, auto args, auto &res) {
        cs_list_parse_state p{args[0].get_str()};
        res.set_int(cs_int(list_count(p, cs)));
    });

    gcs.new_command("at", "si1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        cs_strref str = args[0].get_str();
        cs_list_parse_state p{str};
        p.item = str;
        for (size_t i = 1; i < args.size(); ++i) {
            p.input = str;
            cs_int pos = args[i].get_int();
            for (; pos > 0; --pos) {
                if (!list_parse(p, cs)) {
                    break;
                }
            }
            if (pos > 0 || !list_parse(p, cs)) {
                p.item = p.quoted_item = ostd::string_range();
            }
        }
        res.set_str(list_get_item(p, cs));
    });

    gcs.new_command("sublist", "siiN", [](auto &cs, auto args, auto &res) {
        cs_int skip   = args[1].get_int(),
              count   = args[2].get_int(),
              numargs = args[3].get_int();

        cs_int offset = std::max(skip, cs_int(0)),
              len = (numargs >= 3) ? std::max(count, cs_int(0)) : -1;

        cs_list_parse_state p{args[0].get_str()};
        for (cs_int i = 0; i < offset; ++i) {
            if (!list_parse(p, cs)) break;
        }
        if (len < 0) {
            if (offset > 0) {
                list_find_item(p);
            }
            res.set_str(p.input);
            return;
        }

        char const *list = p.input.data();
        p.quoted_item = ostd::string_range();
        if (len > 0 && list_parse(p, cs)) {
            while (--len > 0 && list_parse(p, cs));
        }
        ostd::string_range quote = p.quoted_item;
        char const *qend = !quote.empty() ? &quote[quote.size()] : list;
        res.set_str(ostd::string_range{list, qend});
    });

    gcs.new_command("listfind", "rse", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias()) {
            res.set_int(-1);
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (cs_list_parse_state p{args[1].get_str()}; list_parse(p, cs);) {
            ++n;
            idv.set_str(p.item);
            idv.push();
            if (cs.run_bool(body)) {
                res.set_int(cs_int(n));
                return;
            }
        }
        res.set_int(-1);
    });

    gcs.new_command("listassoc", "rse", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (cs_list_parse_state p{args[1].get_str()}; list_parse(p, cs);) {
            ++n;
            idv.set_str(p.item);
            idv.push();
            if (cs.run_bool(body)) {
                if (list_parse(p, cs)) {
                    res.set_str(list_get_item(p, cs));
                }
                break;
            }
            if (!list_parse(p, cs)) {
                break;
            }
        }
    });

    gcs.new_command("listfind=", "i", [](auto &cs, auto args, auto &res) {
        cs_list_find<cs_int>(
            cs, args, res, [](cs_list_parse_state const &p, cs_int val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    gcs.new_command("listfind=f", "f", [](auto &cs, auto args, auto &res) {
        cs_list_find<cs_float>(
            cs, args, res, [](cs_list_parse_state const &p, cs_float val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    gcs.new_command("listfind=s", "s", [](auto &cs, auto args, auto &res) {
        cs_list_find<ostd::string_range>(
            cs, args, res, [](cs_list_parse_state const &p, ostd::string_range val) {
                return p.item == val;
            }
        );
    });

    gcs.new_command("listassoc=", "i", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<cs_int>(
            cs, args, res, [](cs_list_parse_state const &p, cs_int val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    gcs.new_command("listassoc=f", "f", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<cs_float>(
            cs, args, res, [](cs_list_parse_state const &p, cs_float val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    gcs.new_command("listassoc=s", "s", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<ostd::string_range>(
            cs, args, res, [](cs_list_parse_state const &p, ostd::string_range val) {
                return p.item == val;
            }
        );
    });

    gcs.new_command("looplist", "rse", [](auto &cs, auto args, auto &) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0;
        for (cs_list_parse_state p{args[1].get_str()}; list_parse(p, cs); ++n) {
            idv.set_str(list_get_item(p, cs));
            idv.push();
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

    gcs.new_command("looplist2", "rrse", [](auto &cs, auto args, auto &) {
        cs_stacked_value idv1{cs, args[0].get_ident()};
        cs_stacked_value idv2{cs, args[1].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias()) {
            return;
        }
        auto body = args[3].get_code();
        int n = 0;
        for (cs_list_parse_state p{args[2].get_str()}; list_parse(p, cs); n += 2) {
            idv1.set_str(list_get_item(p, cs));
            if (list_parse(p, cs)) {
                idv2.set_str(list_get_item(p, cs));
            } else {
                idv2.set_str("");
            }
            idv1.push();
            idv2.push();
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

    gcs.new_command("looplist3", "rrrse", [](auto &cs, auto args, auto &) {
        cs_stacked_value idv1{cs, args[0].get_ident()};
        cs_stacked_value idv2{cs, args[1].get_ident()};
        cs_stacked_value idv3{cs, args[2].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias() || !idv3.has_alias()) {
            return;
        }
        auto body = args[4].get_code();
        int n = 0;
        for (cs_list_parse_state p{args[3].get_str()}; list_parse(p, cs); n += 3) {
            idv1.set_str(list_get_item(p, cs));
            if (list_parse(p, cs)) {
                idv2.set_str(list_get_item(p, cs));
            } else {
                idv2.set_str("");
            }
            if (list_parse(p, cs)) {
                idv3.set_str(list_get_item(p, cs));
            } else {
                idv3.set_str("");
            }
            idv1.push();
            idv2.push();
            idv3.push();
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

    gcs.new_command("looplistconcat", "rse", [](auto &cs, auto args, auto &res) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_str(),
            args[2].get_code(), true
        );
    });

    gcs.new_command("looplistconcatword", "rse", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_str(),
            args[2].get_code(), false
        );
    });

    gcs.new_command("listfilter", "rse", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        cs_string r;
        int n = 0;
        for (cs_list_parse_state p{args[1].get_str()}; list_parse(p, cs); ++n) {
            idv.set_str(p.item);
            idv.push();
            if (cs.run_bool(body)) {
                if (r.size()) {
                    r += ' ';
                }
                r += p.quoted_item;
            }
        }
        res.set_str(r);
    });

    gcs.new_command("listcount", "rse", [](auto &cs, auto args, auto &res) {
        cs_stacked_value idv{cs, args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0, r = 0;
        for (cs_list_parse_state p{args[1].get_str()}; list_parse(p, cs); ++n) {
            idv.set_str(p.item);
            idv.push();
            if (cs.run_bool(body)) {
                r++;
            }
        }
        res.set_int(r);
    });

    gcs.new_command("prettylist", "ss", [](auto &cs, auto args, auto &res) {
        auto buf = ostd::appender<cs_string>();
        ostd::string_range s = args[0].get_str();
        ostd::string_range conj = args[1].get_str();
        cs_list_parse_state p{s};
        size_t len = list_count(p, cs);
        size_t n = 0;
        for (p.input = s; list_parse(p, cs); ++n) {
            if (!p.quoted_item.empty() && (p.quoted_item.front() == '"')) {
                util::unescape_string(buf, p.item);
            } else {
                ostd::range_put_all(buf, p.item);
            }
            if ((n + 1) < len) {
                if ((len > 2) || conj.empty()) {
                    buf.put(',');
                }
                if ((n + 2 == len) && !conj.empty()) {
                    buf.put(' ');
                    ostd::range_put_all(buf, conj);
                }
                buf.put(' ');
            }
        }
        res.set_str(buf.get());
    });

    gcs.new_command("indexof", "ss", [](auto &cs, auto args, auto &res) {
        res.set_int(
            cs_list_includes(cs, args[0].get_str(), args[1].get_str())
        );
    });

    gcs.new_command("listdel", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<false, false>(cs, args, res, std::less<int>());
    });
    gcs.new_command("listintersect", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<false, false>(cs, args, res, std::greater_equal<int>());
    });
    gcs.new_command("listunion", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<true, true>(cs, args, res, std::less<int>());
    });

    gcs.new_command("listsplice", "ssii", [](auto &cs, auto args, auto &res) {
        cs_int offset = std::max(args[2].get_int(), cs_int(0));
        cs_int len    = std::max(args[3].get_int(), cs_int(0));
        ostd::string_range s = args[0].get_str();
        ostd::string_range vals = args[1].get_str();
        char const *list = s.data();
        cs_list_parse_state p{s};
        for (cs_int i = 0; i < offset; ++i) {
            if (!list_parse(p, cs)) {
                break;
            }
        }
        ostd::string_range quote = p.quoted_item;
        char const *qend = !quote.empty() ? &quote[quote.size()] : list;
        cs_string buf;
        if (qend > list) {
            buf += ostd::string_range(list, qend);
        }
        if (!vals.empty()) {
            if (!buf.empty()) {
                buf += ' ';
            }
            buf += vals;
        }
        for (cs_int i = 0; i < len; ++i) {
            if (!list_parse(p, cs)) {
                break;
            }
        }
        list_find_item(p);
        if (!p.input.empty()) {
            switch (p.input.front()) {
                case ')':
                case ']':
                    break;
                default:
                    if (!buf.empty()) {
                        buf += ' ';
                    }
                    buf += p.input;
                    break;
            }
        }
        res.set_str(buf);
    });

    cs_init_lib_list_sort(gcs);
}

struct ListSortItem {
    ostd::string_range str;
    ostd::string_range quote;
};

struct ListSortFun {
    cs_state &cs;
    cs_stacked_value &xv, &yv;
    cs_bcode *body;

    bool operator()(ListSortItem const &xval, ListSortItem const &yval) {
        xv.set_str(xval.str);
        yv.set_str(yval.str);
        xv.push();
        yv.push();
        return cs.run_bool(body);
    }
};

static void cs_list_sort(
    cs_state &cs, cs_value &res, ostd::string_range list,
    cs_ident *x, cs_ident *y, cs_bcode *body, cs_bcode *unique
) {
    if (x == y || !x->is_alias() || !y->is_alias()) {
        return;
    }

    cs_alias *xa = static_cast<cs_alias *>(x), *ya = static_cast<cs_alias *>(y);

    cs_vector<ListSortItem> items;
    size_t total = 0;

    for (cs_list_parse_state p{list}; list_parse(p, cs);) {
        ListSortItem item = { p.item, p.quoted_item };
        items.push_back(item);
        total += item.quote.size();
    }

    if (items.empty()) {
        res.set_str(list);
        return;
    }

    cs_stacked_value xval{cs, xa}, yval{cs, ya};
    xval.set_none();
    yval.set_none();
    xval.push();
    yval.push();

    size_t totaluniq = total;
    size_t nuniq = items.size();
    if (body) {
        ListSortFun f = { cs, xval, yval, body };
        ostd::sort_cmp(ostd::iter(items), f);
        if (!cs_code_is_empty(unique)) {
            f.body = unique;
            totaluniq = items[0].quote.size();
            nuniq = 1;
            for (size_t i = 1; i < items.size(); i++) {
                ListSortItem &item = items[i];
                if (f(items[i - 1], item)) {
                    item.quote = nullptr;
                } else {
                    totaluniq += item.quote.size();
                    ++nuniq;
                }
            }
        }
    } else {
        ListSortFun f = { cs, xval, yval, unique };
        totaluniq = items[0].quote.size();
        nuniq = 1;
        for (size_t i = 1; i < items.size(); i++) {
            ListSortItem &item = items[i];
            for (size_t j = 0; j < i; ++j) {
                ListSortItem &prev = items[j];
                if (!prev.quote.empty() && f(item, prev)) {
                    item.quote = nullptr;
                    break;
                }
            }
            if (!item.quote.empty()) {
                totaluniq += item.quote.size();
                ++nuniq;
            }
        }
    }

    xval.pop();
    yval.pop();

    cs_string sorted;
    sorted.reserve(totaluniq + std::max(nuniq - 1, size_t(0)));
    for (size_t i = 0; i < items.size(); ++i) {
        ListSortItem &item = items[i];
        if (item.quote.empty()) {
            continue;
        }
        if (i) {
            sorted += ' ';
        }
        sorted += item.quote;
    }
    res.set_str(sorted);
}

static void cs_init_lib_list_sort(cs_state &gcs) {
    gcs.new_command("sortlist", "srree", [](auto &cs, auto args, auto &res) {
        cs_list_sort(
            cs, res, args[0].get_str(), args[1].get_ident(),
            args[2].get_ident(), args[3].get_code(), args[4].get_code()
        );
    });
    gcs.new_command("uniquelist", "srre", [](auto &cs, auto args, auto &res) {
        cs_list_sort(
            cs, res, args[0].get_str(), args[1].get_ident(),
            args[2].get_ident(), nullptr, args[3].get_code()
        );
    });
}

} /* namespace cscript */
