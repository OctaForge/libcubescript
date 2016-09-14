#include "cubescript/cubescript.hh"
#include "cs_util.hh"

namespace cscript {

char *cs_dup_ostr(ostd::ConstCharRange s);

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
static inline void cs_list_find(CsValueRange args, CsValue &res, F cmp) {
    CsInt n = 0, skip = args[2].get_int();
    T val = CsArgVal<T>::get(args[1]);
    for (util::ListParser p(args[0].get_strr()); p.parse(); ++n) {
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
static inline void cs_list_assoc(CsValueRange args, CsValue &res, F cmp) {
    T val = CsArgVal<T>::get(args[1]);
    for (util::ListParser p(args[0].get_strr()); p.parse();) {
        if (cmp(p, val)) {
            if (p.parse()) {
                auto elem = p.element();
                auto er = elem.iter();
                elem.disown();
                res.set_mstr(er);
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
    CsVector<char> r;
    int n = 0;
    for (util::ListParser p(list); p.parse(); ++n) {
        char *val = p.element().disown();
        idv.set_mstr(val);
        idv.push();
        if (n && space) {
            r.push(' ');
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
        CsString vstr = ostd::move(v.get_str());
        r.push_n(vstr.data(), vstr.size());
    }
end:
    r.push('\0');
    ostd::Size len = r.size();
    res.set_mstr(ostd::CharRange(r.disown(), len - 1));
}

int cs_list_includes(ostd::ConstCharRange list, ostd::ConstCharRange needle) {
    int offset = 0;
    for (util::ListParser p(list); p.parse();) {
        if (p.item == needle) {
            return offset;
        }
        ++offset;
    }
    return -1;
}

template<bool PushList, bool Swap, typename F>
static inline void cs_list_merge(CsValueRange args, CsValue &res, F cmp) {
    ostd::ConstCharRange list = args[0].get_strr();
    ostd::ConstCharRange elems = args[1].get_strr();
    CsVector<char> buf;
    if (PushList) {
        buf.push_n(list.data(), list.size());
    }
    if (Swap) {
        ostd::swap(list, elems);
    }
    for (util::ListParser p(list); p.parse();) {
        if (cmp(cs_list_includes(elems, p.item), 0)) {
            if (!buf.empty()) {
                buf.push(' ');
            }
            buf.push_n(p.quote.data(), p.quote.size());
        }
    }
    buf.push('\0');
    ostd::Size len = buf.size() - 1;
    res.set_mstr(ostd::CharRange(buf.disown(), len));
}

static void cs_init_lib_list_sort(CsState &cs);

void cs_init_lib_list(CsState &gcs) {
    gcs.new_command("listlen", "s", [](CsState &, CsValueRange args, CsValue &res) {
        res.set_int(CsInt(util::list_length(args[0].get_strr())));
    });

    gcs.new_command("at", "si1V", [](CsState &, CsValueRange args, CsValue &res) {
        if (args.empty()) {
            return;
        }
        CsString str = ostd::move(args[0].get_str());
        util::ListParser p(str);
        p.item = str;
        for (ostd::Size i = 1; i < args.size(); ++i) {
            p.input = str;
            CsInt pos = args[i].get_int();
            for (; pos > 0; --pos) {
                if (!p.parse()) {
                    break;
                }
            }
            if (pos > 0 || !p.parse()) {
                p.item = p.quote = ostd::ConstCharRange();
            }
        }
        auto elem = p.element();
        auto er = p.element().iter();
        elem.disown();
        res.set_mstr(er);
    });

    gcs.new_command("sublist", "siiN", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        CsInt skip    = args[1].get_int(),
              count   = args[2].get_int(),
              numargs = args[2].get_int();

        CsInt offset = ostd::max(skip, CsInt(0)),
              len = (numargs >= 3) ? ostd::max(count, CsInt(0)) : -1;

        util::ListParser p(args[0].get_strr());
        for (CsInt i = 0; i < offset; ++i) {
            if (!p.parse()) break;
        }
        if (len < 0) {
            if (offset > 0) {
                p.skip();
            }
            res.set_str(p.input);
            return;
        }

        char const *list = p.input.data();
        p.quote = ostd::ConstCharRange();
        if (len > 0 && p.parse()) {
            while (--len > 0 && p.parse());
        }
        char const *qend = !p.quote.empty() ? &p.quote[p.quote.size()] : list;
        res.set_str(ostd::ConstCharRange(list, qend - list));
    });

    gcs.new_command("listfind", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            res.set_int(-1);
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (util::ListParser p(args[1].get_strr()); p.parse();) {
            ++n;
            idv.set_mstr(cs_dup_ostr(p.item));
            idv.push();
            if (cs.run_bool(body)) {
                res.set_int(CsInt(n));
                return;
            }
        }
        res.set_int(-1);
    });

    gcs.new_command("listassoc", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = -1;
        for (util::ListParser p(args[1].get_strr()); p.parse();) {
            ++n;
            idv.set_mstr(cs_dup_ostr(p.item));
            idv.push();
            if (cs.run_bool(body)) {
                if (p.parse()) {
                    auto elem = p.element();
                    auto er = elem.iter();
                    elem.disown();
                    res.set_mstr(er);
                }
                break;
            }
            if (!p.parse()) {
                break;
            }
        }
    });

    gcs.new_command("listfind=", "i", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_find<CsInt>(
            args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    gcs.new_command("listfind=f", "f", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_find<CsFloat>(
            args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    gcs.new_command("listfind=s", "s", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_find<ostd::ConstCharRange>(
            args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.item == val;
            }
        );
    });

    gcs.new_command("listassoc=", "i", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_assoc<CsInt>(
            args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    gcs.new_command("listassoc=f", "f", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_assoc<CsFloat>(
            args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    gcs.new_command("listassoc=s", "s", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_assoc<ostd::ConstCharRange>(
            args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.item == val;
            }
        );
    });

    gcs.new_command("looplist", "rse", [](
        CsState &cs, CsValueRange args, CsValue &
    ) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            idv.set_mstr(p.element().disown());
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

    gcs.new_command("looplist2", "rrse", [](
        CsState &cs, CsValueRange args, CsValue &
    ) {
        CsStackedValue idv1{args[0].get_ident()}, idv2{args[1].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias()) {
            return;
        }
        auto body = args[3].get_code();
        int n = 0;
        for (util::ListParser p(args[2].get_strr()); p.parse(); n += 2) {
            idv1.set_mstr(p.element().disown());
            idv2.set_mstr(p.parse() ? p.element().disown() : cs_dup_ostr(""));
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

    gcs.new_command("looplist3", "rrrse", [](
        CsState &cs, CsValueRange args, CsValue &
    ) {
        CsStackedValue idv1{args[0].get_ident()};
        CsStackedValue idv2{args[1].get_ident()};
        CsStackedValue idv3{args[2].get_ident()};
        if (!idv1.has_alias() || !idv2.has_alias() || !idv3.has_alias()) {
            return;
        }
        auto body = args[4].get_code();
        int n = 0;
        for (util::ListParser p(args[3].get_strr()); p.parse(); n += 3) {
            idv1.set_mstr(p.element().disown());
            idv2.set_mstr(p.parse() ? p.element().disown() : cs_dup_ostr(""));
            idv3.set_mstr(p.parse() ? p.element().disown() : cs_dup_ostr(""));
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

    gcs.new_command("looplistconcat", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), true
        );
    });

    gcs.new_command("looplistconcatword", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), false
        );
    });

    gcs.new_command("listfilter", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        CsVector<char> r;
        int n = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            idv.set_mstr(val);
            idv.push();
            if (cs.run_bool(body)) {
                if (r.size()) {
                    r.push(' ');
                }
                r.push_n(p.quote.data(), p.quote.size());
            }
        }
        r.push('\0');
        ostd::Size len = r.size() - 1;
        res.set_mstr(ostd::CharRange(r.disown(), len));
    });

    gcs.new_command("listcount", "rse", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        CsStackedValue idv{args[0].get_ident()};
        if (!idv.has_alias()) {
            return;
        }
        auto body = args[2].get_code();
        int n = 0, r = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            idv.set_mstr(val);
            idv.push();
            if (cs.run_bool(body)) {
                r++;
            }
        }
        res.set_int(r);
    });

    gcs.new_command("prettylist", "ss", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        CsVector<char> buf;
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange conj = args[1].get_strr();
        ostd::Size len = util::list_length(s);
        ostd::Size n = 0;
        for (util::ListParser p(s); p.parse(); ++n) {
            if (!p.quote.empty() && (p.quote.front() == '"')) {
                buf.reserve(buf.size() + p.item.size());
                auto writer = ostd::CharRange(
                    &buf[buf.size()], buf.capacity() - buf.size()
                );
                ostd::Size adv = util::unescape_string(writer, p.item);
                writer.put('\0');
                buf.advance(adv);
            } else {
                buf.push_n(p.item.data(), p.item.size());
            }
            if ((n + 1) < len) {
                if ((len > 2) || conj.empty()) {
                    buf.push(',');
                }
                if ((n + 2 == len) && !conj.empty()) {
                    buf.push(' ');
                    buf.push_n(conj.data(), conj.size());
                }
                buf.push(' ');
            }
        }
        buf.push('\0');
        ostd::Size slen = buf.size() - 1;
        res.set_mstr(ostd::CharRange(buf.disown(), slen));
    });

    gcs.new_command("indexof", "ss", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        res.set_int(
            cs_list_includes(args[0].get_strr(), args[1].get_strr())
        );
    });

    gcs.new_command("listdel", "ss", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_merge<false, false>(args, res, ostd::Less<int>());
    });
    gcs.new_command("listintersect", "ss", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_merge<false, false>(args, res, ostd::GreaterEqual<int>());
    });
    gcs.new_command("listunion", "ss", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        cs_list_merge<true, true>(args, res, ostd::Less<int>());
    });

    gcs.new_command("listsplice", "ssii", [](
        CsState &, CsValueRange args, CsValue &res
    ) {
        CsInt offset = ostd::max(args[2].get_int(), CsInt(0));
        CsInt len    = ostd::max(args[3].get_int(), CsInt(0));
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange vals = args[1].get_strr();
        char const *list = s.data();
        util::ListParser p(s);
        for (CsInt i = 0; i < offset; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        char const *qend = !p.quote.empty() ? &p.quote[p.quote.size()] : list;
        CsVector<char> buf;
        if (qend > list) {
            buf.push_n(list, qend - list);
        }
        if (!vals.empty()) {
            if (!buf.empty()) {
                buf.push(' ');
            }
            buf.push_n(vals.data(), vals.size());
        }
        for (CsInt i = 0; i < len; ++i) {
            if (!p.parse()) {
                break;
            }
        }
        p.skip();
        if (!p.input.empty()) {
            switch (p.input.front()) {
                case ')':
                case ']':
                    break;
                default:
                    if (!buf.empty()) {
                        buf.push(' ');
                    }
                    buf.push_n(p.input.data(), p.input.size());
                    break;
            }
        }
        buf.push('\0');
        ostd::Size slen = buf.size() - 1;
        res.set_mstr(ostd::CharRange(buf.disown(), slen));
    });

    cs_init_lib_list_sort(gcs);
}

struct ListSortItem {
    char const *str;
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
    ostd::Size clen = list.size();
    ostd::Size total = 0;

    char *cstr = cs_dup_ostr(list);
    for (util::ListParser p(list); p.parse();) {
        cstr[&p.item[p.item.size()] - list.data()] = '\0';
        ListSortItem item = { &cstr[p.item.data() - list.data()], p.quote };
        items.push(item);
        total += item.quote.size();
    }

    if (items.empty()) {
        res.set_mstr(cstr);
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
        ostd::sort_cmp(items.iter(), f);
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

    char *sorted = cstr;
    ostd::Size sortedlen = totaluniq + ostd::max(nuniq - 1, ostd::Size(0));
    if (clen < sortedlen) {
        delete[] cstr;
        sorted = new char[sortedlen + 1];
    }

    ostd::Size offset = 0;
    for (ostd::Size i = 0; i < items.size(); ++i) {
        ListSortItem &item = items[i];
        if (item.quote.empty()) {
            continue;
        }
        if (i) {
            sorted[offset++] = ' ';
        }
        memcpy(&sorted[offset], item.quote.data(), item.quote.size());
        offset += item.quote.size();
    }
    sorted[offset] = '\0';

    res.set_mstr(sorted);
}

static void cs_init_lib_list_sort(CsState &gcs) {
    gcs.new_command("sortlist", "srree", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), args[3].get_code(), args[4].get_code()
        );
    });
    gcs.new_command("uniquelist", "srre", [](
        CsState &cs, CsValueRange args, CsValue &res
    ) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), nullptr, args[3].get_code()
        );
    });
}

} /* namespace cscript */
