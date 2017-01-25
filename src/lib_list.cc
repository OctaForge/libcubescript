#include "cubescript/cubescript.hh"
#include "cs_util.hh"

namespace cscript {

template<typename T>
struct CsArgVal;

template<>
struct CsArgVal<CsInt> {
    static CsInt get(CsValue &tv) {
        return tv.get_int();
    }
};

template<>
struct CsArgVal<CsFloat> {
    static CsFloat get(CsValue &tv) {
        return tv.get_float();
    }
};

template<>
struct CsArgVal<ostd::ConstCharRange> {
    static ostd::ConstCharRange get(CsValue &tv) {
        return tv.get_strr();
    }
};

template<typename T, typename F>
static inline void cs_list_find(
    CsState &cs, CsValueRange args, CsValue &res, F cmp
) {
    CsInt n = 0, skip = args[2].get_int();
    T val = CsArgVal<T>::get(args[1]);
    for (util::ListParser p(cs, args[0].get_strr()); p.parse(); ++n) {
        if (cmp(p, val)) {
            res.set_int(n);
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
    res.set_int(-1);
}

template<typename T, typename F>
static inline void cs_list_assoc(
    CsState &cs, CsValueRange args, CsValue &res, F cmp
) {
    T val = CsArgVal<T>::get(args[1]);
    for (util::ListParser p(cs, args[0].get_strr()); p.parse();) {
        if (cmp(p, val)) {
            if (p.parse()) {
                res.set_str(p.get_item());
            }
            return;
        }
        if (!p.parse()) {
            break;
        }
    }
}

static void cs_loop_list_conc(
    CsState &cs, CsValue &res, CsIdent *id, ostd::ConstCharRange list,
    CsBytecode *body, bool space
) {
    CsStackedValue idv{id};
    if (!idv.has_alias()) {
        return;
    }
    CsString r;
    int n = 0;
    for (util::ListParser p(cs, list); p.parse(); ++n) {
        idv.set_str(p.get_item());
        idv.push();
        if (n && space) {
            r += ' ';
        }
        CsValue v;
        switch (cs.run_loop(body, v)) {
            case CsLoopState::Break:
                goto end;
            case CsLoopState::Continue:
                continue;
            default:
                break;
        }
        r += v.get_str();
    }
end:
    res.set_str(std::move(r));
}

int cs_list_includes(
    CsState &cs, ostd::ConstCharRange list, ostd::ConstCharRange needle
) {
    int offset = 0;
    for (util::ListParser p(cs, list); p.parse();) {
        if (p.get_raw_item() == needle) {
            return offset;
        }
        ++offset;
    }
    return -1;
}

template<bool PushList, bool Swap, typename F>
static inline void cs_list_merge(
    CsState &cs, CsValueRange args, CsValue &res, F cmp
) {
    ostd::ConstCharRange list = args[0].get_strr();
    ostd::ConstCharRange elems = args[1].get_strr();
    CsString buf;
    if (PushList) {
        buf += list;
    }
    if (Swap) {
        ostd::swap(list, elems);
    }
    for (util::ListParser p(cs, list); p.parse();) {
        if (cmp(cs_list_includes(cs, elems, p.get_raw_item()), 0)) {
            if (!buf.empty()) {
                buf += ' ';
            }
            buf += p.get_raw_item(true);
        }
    }
    res.set_str(std::move(buf));
}

static void cs_init_lib_list_sort(CsState &cs);

void cs_init_lib_list(CsState &gcs) {
    gcs.new_command("listlen", "s", [](auto &cs, auto args, auto &res) {
        res.set_int(CsInt(util::ListParser(cs, args[0].get_strr()).count()));
    });

    gcs.new_command("at", "si1V", [](auto &cs, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        CsString str = std::move(args[0].get_str());
        util::ListParser p(cs, str);
        p.get_raw_item() = str;
        for (ostd::Size i = 1; i < args.size(); ++i) {
            p.get_input() = str;
            CsInt pos = args[i].get_int();
            for (; pos > 0; --pos) {
                if (!p.parse()) {
                    break;
                }
            }
            if (pos > 0 || !p.parse()) {
                p.get_raw_item() = p.get_raw_item(true) = ostd::ConstCharRange();
            }
        }
        res.set_str(p.get_item());
    });

    gcs.new_command("sublist", "siiN", [](auto &cs, auto args, auto &res) {
        CsInt skip    = args[1].get_int(),
              count   = args[2].get_int(),
              numargs = args[3].get_int();

        CsInt offset = ostd::max(skip, CsInt(0)),
              len = (numargs >= 3) ? ostd::max(count, CsInt(0)) : -1;

        util::ListParser p(cs, args[0].get_strr());
        for (CsInt i = 0; i < offset; ++i) {
            if (!p.parse()) break;
        }
        if (len < 0) {
            if (offset > 0) {
                p.skip();
            }
            res.set_str(p.get_input());
            return;
        }

        char const *list = p.get_input().data();
        p.get_raw_item(true) = ostd::ConstCharRange();
        if (len > 0 && p.parse()) {
            while (--len > 0 && p.parse());
        }
        ostd::ConstCharRange quote = p.get_raw_item(true);
        char const *qend = !quote.empty() ? &quote[quote.size()] : list;
        res.set_str(ostd::ConstCharRange(list, qend - list));
    });

    gcs.new_command("listfind", "rse", [](auto &cs, auto args, auto &res) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            res.set_int(-1);
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (util::ListParser p(cs, args[1].get_strr()); p.parse();) {
            ++n;
            idv.set_str(p.get_raw_item());
            idv.push();
            if (cs.run_bool(body)) {
                res.set_int(CsInt(n));
                return;
            }
        }
        res.set_int(-1);
    });

    gcs.new_command("listassoc", "rse", [](auto &cs, auto args, auto &res) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (util::ListParser p(cs, args[1].get_strr()); p.parse();) {
            ++n;
            idv.set_str(p.get_raw_item());
            idv.push();
            if (cs.run_bool(body)) {
                if (p.parse()) {
                    res.set_str(p.get_item());
                }
                break;
            }
            if (!p.parse()) {
                break;
            }
        }
    });

    gcs.new_command("listfind=", "i", [](auto &cs, auto args, auto &res) {
        cs_list_find<CsInt>(
            cs, args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.get_raw_item()) == val;
            }
        );
    });
    gcs.new_command("listfind=f", "f", [](auto &cs, auto args, auto &res) {
        cs_list_find<CsFloat>(
            cs, args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.get_raw_item()) == val;
            }
        );
    });
    gcs.new_command("listfind=s", "s", [](auto &cs, auto args, auto &res) {
        cs_list_find<ostd::ConstCharRange>(
            cs, args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.get_raw_item() == val;
            }
        );
    });

    gcs.new_command("listassoc=", "i", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<CsInt>(
            cs, args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.get_raw_item()) == val;
            }
        );
    });
    gcs.new_command("listassoc=f", "f", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<CsFloat>(
            cs, args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.get_raw_item()) == val;
            }
        );
    });
    gcs.new_command("listassoc=s", "s", [](auto &cs, auto args, auto &res) {
        cs_list_assoc<ostd::ConstCharRange>(
            cs, args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.get_raw_item() == val;
            }
        );
    });

    gcs.new_command("looplist", "rse", [](auto &cs, auto args, auto &) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0;
        for (util::ListParser p(cs, args[1].get_strr()); p.parse(); ++n) {
            idv.set_str(p.get_item());
            idv.push();
            switch (cs.run_loop(body)) {
                case CsLoopState::Break:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    gcs.new_command("looplist2", "rrse", [](auto &cs, auto args, auto &) {
        CsStackedValue idv1{args[0].get_ident()}, idv2{args[1].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias()) {
            return;
        }
        auto body = args[3].get_code();
        int n = 0;
        for (util::ListParser p(cs, args[2].get_strr()); p.parse(); n += 2) {
            idv1.set_str(p.get_item());
            if (p.parse()) {
                idv2.set_str(p.get_item());
            } else {
                idv2.set_str("");
            }
            idv1.push();
            idv2.push();
            switch (cs.run_loop(body)) {
                case CsLoopState::Break:
                    goto end;
                default: /* continue and normal */
                    break;
            }
        }
end:
        return;
    });

    gcs.new_command("looplist3", "rrrse", [](auto &cs, auto args, auto &) {
        CsStackedValue idv1{args[0].get_ident()};
        CsStackedValue idv2{args[1].get_ident()};
        CsStackedValue idv3{args[2].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias() || !idv3.has_alias()) {
            return;
        }
        auto body = args[4].get_code();
        int n = 0;
        for (util::ListParser p(cs, args[3].get_strr()); p.parse(); n += 3) {
            idv1.set_str(p.get_item());
            if (p.parse()) {
                idv2.set_str(p.get_item());
            } else {
                idv2.set_str("");
            }
            if (p.parse()) {
                idv3.set_str(p.get_item());
            } else {
                idv3.set_str("");
            }
            idv1.push();
            idv2.push();
            idv3.push();
            switch (cs.run_loop(body)) {
                case CsLoopState::Break:
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
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), true
        );
    });

    gcs.new_command("looplistconcatword", "rse", [](
        auto &cs, auto args, auto &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), false
        );
    });

    gcs.new_command("listfilter", "rse", [](auto &cs, auto args, auto &res) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        CsString r;
        int n = 0;
        for (util::ListParser p(cs, args[1].get_strr()); p.parse(); ++n) {
            idv.set_str(p.get_raw_item());
            idv.push();
            if (cs.run_bool(body)) {
                if (r.size()) {
                    r += ' ';
                }
                r += p.get_raw_item(true);
            }
        }
        res.set_str(std::move(r));
    });

    gcs.new_command("listcount", "rse", [](auto &cs, auto args, auto &res) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0, r = 0;
        for (util::ListParser p(cs, args[1].get_strr()); p.parse(); ++n) {
            idv.set_str(p.get_raw_item());
            idv.push();
            if (cs.run_bool(body)) {
                r++;
            }
        }
        res.set_int(r);
    });

    gcs.new_command("prettylist", "ss", [](auto &cs, auto args, auto &res) {
        auto buf = ostd::appender<CsString>();
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange conj = args[1].get_strr();
        ostd::Size len = util::ListParser(cs, s).count();
        ostd::Size n = 0;
        for (util::ListParser p(cs, s); p.parse(); ++n) {
            if (!p.get_raw_item(true).empty() &&
                (p.get_raw_item(true).front() == '"')) {
                util::unescape_string(buf, p.get_raw_item());
            } else {
                buf.put_n(p.get_raw_item().data(), p.get_raw_item().size());
            }
            if ((n + 1) < len) {
                if ((len > 2) || conj.empty()) {
                    buf.put(',');
                }
                if ((n + 2 == len) && !conj.empty()) {
                    buf.put(' ');
                    buf.put_n(conj.data(), conj.size());
                }
                buf.put(' ');
            }
        }
        res.set_str(std::move(buf.get()));
    });

    gcs.new_command("indexof", "ss", [](auto &cs, auto args, auto &res) {
        res.set_int(
            cs_list_includes(cs, args[0].get_strr(), args[1].get_strr())
        );
    });

    gcs.new_command("listdel", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<false, false>(cs, args, res, ostd::Less<int>());
    });
    gcs.new_command("listintersect", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<false, false>(cs, args, res, ostd::GreaterEqual<int>());
    });
    gcs.new_command("listunion", "ss", [](auto &cs, auto args, auto &res) {
        cs_list_merge<true, true>(cs, args, res, ostd::Less<int>());
    });

    gcs.new_command("listsplice", "ssii", [](auto &cs, auto args, auto &res) {
        CsInt offset = ostd::max(args[2].get_int(), CsInt(0));
        CsInt len    = ostd::max(args[3].get_int(), CsInt(0));
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange vals = args[1].get_strr();
        char const *list = s.data();
        util::ListParser p(cs, s);
        for (CsInt i = 0; i < offset; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        ostd::ConstCharRange quote = p.get_raw_item(true);
        char const *qend = !quote.empty() ? &quote[quote.size()] : list;
        CsString buf;
        if (qend > list) {
            buf += ostd::ConstCharRange(list, qend - list);
        }
        if (!vals.empty()) {
            if (!buf.empty()) {
                buf += ' ';
            }
            buf += vals;
        }
        for (CsInt i = 0; i < len; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        p.skip();
        if (!p.get_input().empty()) {
            switch (p.get_input().front()) {
                case ')':
                case ']':
                    break;
                default:
                    if (!buf.empty()) {
                        buf += ' ';
                    }
                    buf += p.get_input();
                    break;
            }
        }
        res.set_str(std::move(buf));
    });

    cs_init_lib_list_sort(gcs);
}

struct ListSortItem {
    ostd::ConstCharRange str;
    ostd::ConstCharRange quote;
};

struct ListSortFun {
    CsState &cs;
    CsStackedValue &xv, &yv;
    CsBytecode *body;

    bool operator()(ListSortItem const &xval, ListSortItem const &yval) {
        xv.set_cstr(xval.str);
        yv.set_cstr(yval.str);
        xv.push();
        yv.push();
        return cs.run_bool(body);
    }
};

static void cs_list_sort(
    CsState &cs, CsValue &res, ostd::ConstCharRange list,
    CsIdent *x, CsIdent *y, CsBytecode *body, CsBytecode *unique
) {
    if (x == y || !x->is_alias() || !y->is_alias()) {
        return;
    }

    CsAlias *xa = static_cast<CsAlias *>(x), *ya = static_cast<CsAlias *>(y);

    CsVector<ListSortItem> items;
    ostd::Size total = 0;

    for (util::ListParser p(cs, list); p.parse();) {
        ListSortItem item = { p.get_raw_item(), p.get_raw_item(true) };
        items.push_back(item);
        total += item.quote.size();
    }

    if (items.empty()) {
        res.set_str(list);
        return;
    }

    CsStackedValue xval{xa}, yval{ya};
    xval.set_null();
    yval.set_null();
    xval.push();
    yval.push();

    ostd::Size totaluniq = total;
    ostd::Size nuniq = items.size();
    if (body) {
        ListSortFun f = { cs, xval, yval, body };
        ostd::sort_cmp(ostd::iter(items), f);
        if (!cs_code_is_empty(unique)) {
            f.body = unique;
            totaluniq = items[0].quote.size();
            nuniq = 1;
            for (ostd::Size i = 1; i < items.size(); i++) {
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
        for (ostd::Size i = 1; i < items.size(); i++) {
            ListSortItem &item = items[i];
            for (ostd::Size j = 0; j < i; ++j) {
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

    CsString sorted;
    sorted.reserve(totaluniq + ostd::max(nuniq - 1, ostd::Size(0)));
    for (ostd::Size i = 0; i < items.size(); ++i) {
        ListSortItem &item = items[i];
        if (item.quote.empty()) {
            continue;
        }
        if (i) {
            sorted += ' ';
        }
        sorted += item.quote;
    }
    res.set_str(std::move(sorted));
}

static void cs_init_lib_list_sort(CsState &gcs) {
    gcs.new_command("sortlist", "srree", [](auto &cs, auto args, auto &res) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), args[3].get_code(), args[4].get_code()
        );
    });
    gcs.new_command("uniquelist", "srre", [](auto &cs, auto args, auto &res) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), nullptr, args[3].get_code()
        );
    });
}

} /* namespace cscript */
