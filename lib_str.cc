#include <ostd/functional.hh>

#include "cubescript.hh"

namespace cscript {

template<typename F>
static inline void cs_strgcmp(TvalRange args, TaggedValue &res, F cfunc) {
    bool val;
    if (args.size() >= 2) {
        val = cfunc(args[0].get_strr(), args[1].get_strr());
        for (ostd::Size i = 2; (i < args.size()) && val; ++i) {
            val = cfunc(args[i - 1].get_strr(), args[i].get_strr());
        }
    } else {
        val = cfunc(!args.empty() ? args[0].get_strr() : ostd::ConstCharRange(), ostd::ConstCharRange());
    }
    res.set_int(CsInt(val));
};

void cs_init_lib_string(CsState &cs) {
    cs.add_command("strstr", "ss", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange a = args[0].get_strr(), b = args[1].get_strr();
        ostd::ConstCharRange s = a;
        for (CsInt i = 0; b.size() <= s.size(); ++i) {
            if (b == s.slice(0, b.size())) {
                res.set_int(i);
                return;
            }
            ++s;
        }
        res.set_int(-1);
    });

    cs.add_command("strlen", "s", [](TvalRange args, TaggedValue &res) {
        res.set_int(CsInt(args[0].get_strr().size()));
    });

    cs.add_command("strcode", "si", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange str = args[0].get_strr();
        CsInt i = args[1].get_int();
        if (i >= CsInt(str.size())) {
            res.set_int(0);
        } else {
            res.set_int(ostd::byte(str[i]));
        }
    });

    cs.add_command("codestr", "i", [](TvalRange args, TaggedValue &res) {
        char *s = new char[2];
        s[0] = char(args[0].get_int());
        s[1] = '\0';
        res.set_mstr(s);
    });

    cs.add_command("strlower", "s", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        char *buf = new char[s.size() + 1];
        for (auto i: ostd::range(s.size())) {
            buf[i] = tolower(s[i]);
        }
        buf[s.size()] = '\0';
        res.set_mstr(ostd::CharRange(buf, s.size()));
    });

    cs.add_command("strupper", "s", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        char *buf = new char[s.size() + 1];
        for (auto i: ostd::range(s.size())) {
            buf[i] = toupper(s[i]);
        }
        buf[s.size()] = '\0';
        res.set_mstr(ostd::CharRange(buf, s.size()));
    });

    cs.add_command("escape", "s", [](TvalRange args, TaggedValue &res) {
        auto x = ostd::appender<ostd::String>();
        util::escape_string(x, args[0].get_strr());
        ostd::Size len = x.size();
        res.set_mstr(ostd::CharRange(x.get().disown(), len));
    });

    cs.add_command("unescape", "s", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        char *buf = new char[s.size() + 1];
        auto writer = ostd::CharRange(buf, s.size() + 1);
        util::unescape_string(writer, s);
        writer.put('\0');
        res.set_mstr(ostd::CharRange(buf, s.size()));
    });

    cs.add_command("concat", "V", [](TvalRange args, TaggedValue &res) {
        auto s = ostd::appender<ostd::String>();
        cscript::util::tvals_concat(s, args, " ");
        res.set_mstr(s.get().iter());
        s.get().disown();
    });

    cs.add_command("concatword", "V", [](TvalRange args, TaggedValue &res) {
        auto s = ostd::appender<ostd::String>();
        cscript::util::tvals_concat(s, args);
        res.set_mstr(s.get().iter());
        s.get().disown();
    });

    cs.add_command("format", "V", [](TvalRange args, TaggedValue &res) {
        if (args.empty()) {
            return;
        }
        ostd::Vector<char> s;
        ostd::String fs = ostd::move(args[0].get_str());
        ostd::ConstCharRange f = fs.iter();
        while (!f.empty()) {
            char c = *f;
            ++f;
            if ((c == '%') && !f.empty()) {
                char ic = *f;
                ++f;
                if (ic >= '1' && ic <= '9') {
                    int i = ic - '0';
                    ostd::String sub = ostd::move(
                        (ostd::Size(i) < args.size())
                            ? args[i].get_str()
                            : ostd::String("")
                    );
                    s.push_n(sub.data(), sub.size());
                } else {
                    s.push(ic);
                }
            } else {
                s.push(c);
            }
        }
        s.push('\0');
        ostd::Size len = s.size() - 1;
        res.set_mstr(ostd::CharRange(s.disown(), len));
    });

    cs.add_command("tohex", "ii", [](TvalRange args, TaggedValue &res) {
        auto r = ostd::appender<ostd::Vector<char>>();
        ostd::format(
            r, "0x%.*X", ostd::max(args[1].get_int(), 1), args[0].get_int()
        );
        r.put('\0');
        ostd::Size len = r.size() - 1;
        res.set_mstr(ostd::CharRange(r.get().disown(), len));
    });

    cs.add_command("substr", "siiN", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        CsInt start = args[1].get_int(), count = args[2].get_int();
        CsInt numargs = args[3].get_int();
        CsInt len = CsInt(s.size()), offset = ostd::clamp(start, 0, len);
        res.set_str(ostd::ConstCharRange(
            &s[offset],
            (numargs >= 3) ? ostd::clamp(count, 0, len - offset) : (len - offset)
        ));
    });

    cs.add_command("strcmp", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::Equal<ostd::ConstCharRange>());
    });
    cs.add_command("=s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::Equal<ostd::ConstCharRange>());
    });
    cs.add_command("!=s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::NotEqual<ostd::ConstCharRange>());
    });
    cs.add_command("<s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::Less<ostd::ConstCharRange>());
    });
    cs.add_command(">s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::Greater<ostd::ConstCharRange>());
    });
    cs.add_command("<=s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::LessEqual<ostd::ConstCharRange>());
    });
    cs.add_command(">=s", "s1V", [](TvalRange args, TaggedValue &res) {
        cs_strgcmp(args, res, ostd::GreaterEqual<ostd::ConstCharRange>());
    });

    cs.add_command("strreplace", "ssss", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange oldval = args[1].get_strr(),
                             newval = args[2].get_strr(),
                             newval2 = args[3].get_strr();
        if (newval2.empty()) {
            newval2 = newval;
        }
        ostd::Vector<char> buf;
        if (!oldval.size()) {
            res.set_str(s);
            return;
        }
        for (ostd::Size i = 0;; ++i) {
            ostd::ConstCharRange found;
            ostd::ConstCharRange trys = s;
            for (; oldval.size() <= trys.size(); ++trys) {
                if (trys.slice(0, oldval.size()) == oldval) {
                    found = trys;
                    break;
                }
            }
            if (!found.empty()) {
                auto bef = ostd::slice_until(s, found);
                for (; !bef.empty(); ++bef) {
                    buf.push(*bef);
                }
                auto use = (i & 1) ? newval2 : newval;
                for (; !use.empty(); ++use) {
                    buf.push(*use);
                }
                s = found + oldval.size();
            } else {
                for (; !s.empty(); ++s) {
                    buf.push(*s);
                }
                buf.push('\0');
                ostd::Size len = buf.size() - 1;
                res.set_mstr(ostd::CharRange(buf.disown(), len));
                return;
            }
        }
    });

    cs.add_command("strsplice", "ssii", [](TvalRange args, TaggedValue &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange vals = args[1].get_strr();
        CsInt skip   = args[2].get_int(),
              count  = args[3].get_int();
        CsInt slen   = CsInt(s.size()),
              vlen   = CsInt(vals.size());
        CsInt offset = ostd::clamp(skip, 0, slen),
              len    = ostd::clamp(count, 0, slen - offset);
        char *p = new char[slen - len + vlen + 1];
        if (offset) {
            memcpy(p, s.data(), offset);
        }
        if (vlen) {
            memcpy(&p[offset], vals.data(), vlen);
        }
        if (offset + len < slen) {
            memcpy(&p[offset + vlen], &s[offset + len], slen - (offset + len));
        }
        p[slen - len + vlen] = '\0';
        res.set_mstr(ostd::CharRange(p, slen - len + vlen));
    });
}

} /* namespace cscript */
