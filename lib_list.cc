#include "cubescript.hh"
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

static inline void cs_set_iter(Alias &a, char *val, IdentStack &stack) {
    CsValue v;
    v.set_mstr(val);
    a.push_arg(v, stack);
}

static void cs_loop_list_conc(
    CsState &cs, CsValue &res, Ident *id, ostd::ConstCharRange list,
    Bytecode const *body, bool space
) {
    if (!id->is_alias()) {
        return;
    }
    IdentStack stack;
    CsVector<char> r;
    int n = 0;
    for (util::ListParser p(list); p.parse(); ++n) {
        char *val = p.element().disown();
        cs_set_iter(*static_cast<Alias *>(id), val, stack);
        if (n && space) {
            r.push(' ');
        }
        CsValue v;
        cs.run_ret(body, v);
        CsString vstr = ostd::move(v.get_str());
        r.push_n(vstr.data(), vstr.size());
        v.cleanup();
    }
    if (n) {
        static_cast<Alias *>(id)->pop_arg();
    }
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

void cs_init_lib_list(CsState &cs) {
    cs.add_command("listlen", "s", [](CsValueRange args, CsValue &res) {
        res.set_int(CsInt(util::list_length(args[0].get_strr())));
    });

    cs.add_command("at", "si1V", [](CsValueRange args, CsValue &res) {
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

    cs.add_command("sublist", "siiN", [](CsValueRange args, CsValue &res) {
        CsInt skip    = args[1].get_int(),
              count   = args[2].get_int(),
              numargs = args[2].get_int();

        CsInt offset = ostd::max(skip, 0),
              len = (numargs >= 3) ? ostd::max(count, 0) : -1;

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

    cs.add_command("listfind", "rse", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        auto body = args[2].get_code();
        if (!id->is_alias()) {
            res.set_int(-1);
            return;
        }
        IdentStack stack;
        int n = -1;
        for (util::ListParser p(args[1].get_strr()); p.parse();) {
            ++n;
            cs_set_iter(*static_cast<Alias *>(id), cs_dup_ostr(p.item), stack);
            if (cs.run_bool(body)) {
                res.set_int(CsInt(n));
                goto found;
            }
        }
        res.set_int(-1);
found:
        if (n >= 0) {
            static_cast<Alias *>(id)->pop_arg();
        }
    });

    cs.add_command("listassoc", "rse", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        auto body = args[2].get_code();
        if (!id->is_alias()) {
            return;
        }
        IdentStack stack;
        int n = -1;
        for (util::ListParser p(args[1].get_strr()); p.parse();) {
            ++n;
            cs_set_iter(*static_cast<Alias *>(id), cs_dup_ostr(p.item), stack);
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
        if (n >= 0) {
            static_cast<Alias *>(id)->pop_arg();
        }
    });

    cs.add_command("listfind=", "i", [](CsValueRange args, CsValue &res) {
        cs_list_find<CsInt>(
            args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    cs.add_command("listfind=f", "f", [](CsValueRange args, CsValue &res) {
        cs_list_find<CsFloat>(
            args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    cs.add_command("listfind=s", "s", [](CsValueRange args, CsValue &res) {
        cs_list_find<ostd::ConstCharRange>(
            args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.item == val;
            }
        );
    });

    cs.add_command("listassoc=", "i", [](CsValueRange args, CsValue &res) {
        cs_list_assoc<CsInt>(
            args, res, [](const util::ListParser &p, CsInt val) {
                return cs_parse_int(p.item) == val;
            }
        );
    });
    cs.add_command("listassoc=f", "f", [](CsValueRange args, CsValue &res) {
        cs_list_assoc<CsFloat>(
            args, res, [](const util::ListParser &p, CsFloat val) {
                return cs_parse_float(p.item) == val;
            }
        );
    });
    cs.add_command("listassoc=s", "s", [](CsValueRange args, CsValue &res) {
        cs_list_assoc<ostd::ConstCharRange>(
            args, res, [](const util::ListParser &p, ostd::ConstCharRange val) {
                return p.item == val;
            }
        );
    });

    cs.add_command("looplist", "rse", [&cs](CsValueRange args, CsValue &) {
        Ident *id = args[0].get_ident();
        auto body = args[2].get_code();
        if (!id->is_alias()) {
            return;
        }
        IdentStack stack;
        int n = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            cs_set_iter(*static_cast<Alias *>(id), p.element().disown(), stack);
            cs.run_int(body);
        }
        if (n) {
            static_cast<Alias *>(id)->pop_arg();
        }
    });

    cs.add_command("looplist2", "rrse", [&cs](CsValueRange args, CsValue &) {
        Ident *id = args[0].get_ident(), *id2 = args[1].get_ident();
        auto body = args[3].get_code();
        if (!id->is_alias() || !id2->is_alias()) {
            return;
        }
        IdentStack stack, stack2;
        int n = 0;
        for (util::ListParser p(args[2].get_strr()); p.parse(); n += 2) {
            cs_set_iter(*static_cast<Alias *>(id), p.element().disown(), stack);
            cs_set_iter(
                *static_cast<Alias *>(id2),
                p.parse() ? p.element().disown() : cs_dup_ostr(""), stack2
            );
            cs.run_int(body);
        }
        if (n) {
            static_cast<Alias *>(id)->pop_arg();
            static_cast<Alias *>(id2)->pop_arg();
        }
    });

    cs.add_command("looplist3", "rrrse", [&cs](CsValueRange args, CsValue &) {
        Ident *id = args[0].get_ident();
        Ident *id2 = args[1].get_ident();
        Ident *id3 = args[2].get_ident();
        auto body = args[4].get_code();
        if (!id->is_alias() || !id2->is_alias() || !id3->is_alias()) {
            return;
        }
        IdentStack stack, stack2, stack3;
        int n = 0;
        for (util::ListParser p(args[3].get_strr()); p.parse(); n += 3) {
            cs_set_iter(*static_cast<Alias *>(id), p.element().disown(), stack);
            cs_set_iter(
                *static_cast<Alias *>(id2),
                p.parse() ? p.element().disown() : cs_dup_ostr(""), stack2
            );
            cs_set_iter(
                *static_cast<Alias *>(id3),
                p.parse() ? p.element().disown() : cs_dup_ostr(""), stack3
            );
            cs.run_int(body);
        }
        if (n) {
            static_cast<Alias *>(id)->pop_arg();
            static_cast<Alias *>(id2)->pop_arg();
            static_cast<Alias *>(id3)->pop_arg();
        }
    });

    cs.add_command("looplistconcat", "rse", [&cs](
        CsValueRange args, CsValue &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), true
        );
    });

    cs.add_command("looplistconcatword", "rse", [&cs](
        CsValueRange args, CsValue &res
    ) {
        cs_loop_list_conc(
            cs, res, args[0].get_ident(), args[1].get_strr(),
            args[2].get_code(), false
        );
    });

    cs.add_command("listfilter", "rse", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        auto body = args[2].get_code();
        if (!id->is_alias()) {
            return;
        }
        IdentStack stack;
        CsVector<char> r;
        int n = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            cs_set_iter(*static_cast<Alias *>(id), val, stack);
            if (cs.run_bool(body)) {
                if (r.size()) {
                    r.push(' ');
                }
                r.push_n(p.quote.data(), p.quote.size());
            }
        }
        if (n) {
            static_cast<Alias *>(id)->pop_arg();
        }
        r.push('\0');
        ostd::Size len = r.size() - 1;
        res.set_mstr(ostd::CharRange(r.disown(), len));
    });

    cs.add_command("listcount", "rse", [&cs](CsValueRange args, CsValue &res) {
        Ident *id = args[0].get_ident();
        auto body = args[2].get_code();
        if (!id->is_alias()) {
            return;
        }
        IdentStack stack;
        int n = 0, r = 0;
        for (util::ListParser p(args[1].get_strr()); p.parse(); ++n) {
            char *val = cs_dup_ostr(p.item);
            cs_set_iter(*static_cast<Alias *>(id), val, stack);
            if (cs.run_bool(body)) {
                r++;
            }
        }
        if (n) {
            static_cast<Alias *>(id)->pop_arg();
        }
        res.set_int(r);
    });

    cs.add_command("prettylist", "ss", [](CsValueRange args, CsValue &res) {
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

    cs.add_command("indexof", "ss", [](CsValueRange args, CsValue &res) {
        res.set_int(
            cs_list_includes(args[0].get_strr(), args[1].get_strr())
        );
    });

    cs.add_command("listdel", "ss", [](CsValueRange args, CsValue &res) {
        cs_list_merge<false, false>(args, res, ostd::Less<int>());
    });
    cs.add_command("listintersect", "ss", [](CsValueRange args, CsValue &res) {
        cs_list_merge<false, false>(args, res, ostd::GreaterEqual<int>());
    });
    cs.add_command("listunion", "ss", [](CsValueRange args, CsValue &res) {
        cs_list_merge<true, true>(args, res, ostd::Less<int>());
    });

    cs.add_command("listsplice", "ssii", [](CsValueRange args, CsValue &res) {
        CsInt offset = ostd::max(args[2].get_int(), 0);
        CsInt len    = ostd::max(args[3].get_int(), 0);
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

    cs_init_lib_list_sort(cs);
}

struct ListSortItem {
    char const *str;
    ostd::ConstCharRange quote;
};

struct ListSortFun {
    CsState &cs;
    Alias *x, *y;
    Bytecode *body;

    bool operator()(ListSortItem const &xval, ListSortItem const &yval) {
        x->clean_code();
        x->set_value_cstr(xval.str);
        y->clean_code();
        y->set_value_cstr(yval.str);
        return cs.run_bool(body);
    }
};

static void cs_list_sort(
    CsState &cs, CsValue &res, ostd::ConstCharRange list, Ident *x, Ident *y,
    Bytecode *body, Bytecode *unique
) {
    if (x == y || !x->is_alias() || !y->is_alias()) {
        return;
    }

    Alias *xa = static_cast<Alias *>(x), *ya = static_cast<Alias *>(y);

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

    /* default null value, set later from callback */
    CsValue nv;
    nv.set_null();

    IdentStack xstack, ystack;
    xa->push_arg(nv, xstack);
    ya->push_arg(nv, ystack);

    ostd::Size totaluniq = total;
    ostd::Size nuniq = items.size();
    if (body) {
        ListSortFun f = { cs, xa, ya, body };
        ostd::sort_cmp(items.iter(), f);
        if (!code_is_empty(unique)) {
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
        ListSortFun f = { cs, xa, ya, unique };
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

    xa->pop_arg();
    ya->pop_arg();

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

static void cs_init_lib_list_sort(CsState &cs) {
    cs.add_command("sortlist", "srree", [&cs](
        CsValueRange args, CsValue &res
    ) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), args[3].get_code(), args[4].get_code()
        );
    });
    cs.add_command("uniquelist", "srre", [&cs](
        CsValueRange args, CsValue &res
    ) {
        cs_list_sort(
            cs, res, args[0].get_strr(), args[1].get_ident(),
            args[2].get_ident(), nullptr, args[3].get_code()
        );
    });
}

} /* namespace cscript */
