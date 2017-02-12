#include <functional>

#include "cubescript/cubescript.hh"

namespace cscript {

template<typename F>
static inline void cs_strgcmp(CsValueRange args, CsValue &res, F cfunc) {
    bool val;
    if (args.size() >= 2) {
        val = cfunc(args[0].get_strr(), args[1].get_strr());
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cfunc(args[i - 1].get_strr(), args[i].get_strr());
        }
    } else {
        val = cfunc(
            !args.empty() ? args[0].get_strr() : ostd::ConstCharRange(),
            ostd::ConstCharRange()
        );
    }
    res.set_int(CsInt(val));
};

void cs_init_lib_string(CsState &cs) {
    cs.new_command("strstr", "ss", [](auto &, auto args, auto &res) {
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

    cs.new_command("strlen", "s", [](auto &, auto args, auto &res) {
        res.set_int(CsInt(args[0].get_strr().size()));
    });

    cs.new_command("strcode", "si", [](auto &, auto args, auto &res) {
        ostd::ConstCharRange str = args[0].get_strr();
        CsInt i = args[1].get_int();
        if (i >= CsInt(str.size())) {
            res.set_int(0);
        } else {
            res.set_int(ostd::byte(str[i]));
        }
    });

    cs.new_command("codestr", "i", [](auto &, auto args, auto &res) {
        res.set_str(CsString(1, char(args[0].get_int())));
    });

    cs.new_command("strlower", "s", [](auto &, auto args, auto &res) {
        CsString s = args[0].get_str();
        for (auto i: ostd::range(s.size())) {
            s[i] = tolower(s[i]);
        }
        res.set_str(std::move(s));
    });

    cs.new_command("strupper", "s", [](auto &, auto args, auto &res) {
        CsString s = args[0].get_str();
        for (auto i: ostd::range(s.size())) {
            s[i] = toupper(s[i]);
        }
        res.set_str(std::move(s));
    });

    cs.new_command("escape", "s", [](auto &, auto args, auto &res) {
        auto s = ostd::appender<CsString>();
        util::escape_string(s, args[0].get_strr());
        res.set_str(std::move(s.get()));
    });

    cs.new_command("unescape", "s", [](auto &, auto args, auto &res) {
        auto s = ostd::appender<CsString>();
        util::unescape_string(s, args[0].get_strr());
        res.set_str(std::move(s.get()));
    });

    cs.new_command("concat", "V", [](auto &, auto args, auto &res) {
        auto s = ostd::appender<CsString>();
        cscript::util::tvals_concat(s, args, " ");
        res.set_str(std::move(s.get()));
    });

    cs.new_command("concatword", "V", [](auto &, auto args, auto &res) {
        auto s = ostd::appender<CsString>();
        cscript::util::tvals_concat(s, args);
        res.set_str(std::move(s.get()));
    });

    cs.new_command("format", "V", [](auto &, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        CsString s;
        CsString fs = args[0].get_str();
        ostd::ConstCharRange f = ostd::iter(fs);
        while (!f.empty()) {
            char c = *f;
            ++f;
            if ((c == '%') && !f.empty()) {
                char ic = *f;
                ++f;
                if (ic >= '1' && ic <= '9') {
                    int i = ic - '0';
                    if (size_t(i) < args.size()) {
                        s += args[i].get_str();
                    }
                } else {
                    s += ic;
                }
            } else {
                s += c;
            }
        }
        res.set_str(std::move(s));
    });

    cs.new_command("tohex", "ii", [](auto &, auto args, auto &res) {
        auto r = ostd::appender<CsString>();
        try {
            ostd::format(
                r, "0x%.*X", ostd::max(args[1].get_int(), CsInt(1)),
                args[0].get_int()
            );
        } catch (ostd::format_error const &e) {
            throw cs_internal_error{e.what()};
        }
        res.set_str(std::move(r.get()));
    });

    cs.new_command("substr", "siiN", [](auto &, auto args, auto &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        CsInt start = args[1].get_int(), count = args[2].get_int();
        CsInt numargs = args[3].get_int();
        CsInt len = CsInt(s.size()), offset = ostd::clamp(start, CsInt(0), len);
        res.set_str(CsString{
            &s[offset],
            (numargs >= 3)
                ? size_t(ostd::clamp(count, CsInt(0), len - offset))
                : size_t(len - offset)
        });
    });

    cs.new_command("strcmp", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::equal_to<ostd::ConstCharRange>());
    });
    cs.new_command("=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::equal_to<ostd::ConstCharRange>());
    });
    cs.new_command("!=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::not_equal_to<ostd::ConstCharRange>());
    });
    cs.new_command("<s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::less<ostd::ConstCharRange>());
    });
    cs.new_command(">s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::greater<ostd::ConstCharRange>());
    });
    cs.new_command("<=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::less_equal<ostd::ConstCharRange>());
    });
    cs.new_command(">=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::greater_equal<ostd::ConstCharRange>());
    });

    cs.new_command("strreplace", "ssss", [](auto &, auto args, auto &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange oldval = args[1].get_strr(),
                             newval = args[2].get_strr(),
                             newval2 = args[3].get_strr();
        if (newval2.empty()) {
            newval2 = newval;
        }
        CsString buf;
        if (!oldval.size()) {
            res.set_str(CsString{s});
            return;
        }
        for (size_t i = 0;; ++i) {
            ostd::ConstCharRange found;
            ostd::ConstCharRange trys = s;
            for (; oldval.size() <= trys.size(); ++trys) {
                if (trys.slice(0, oldval.size()) == oldval) {
                    found = trys;
                    break;
                }
            }
            if (!found.empty()) {
                buf += ostd::slice_until(s, found);
                buf += (i & 1) ? newval2 : newval;
                s = found + oldval.size();
            } else {
                buf += s;
                res.set_str(std::move(buf));
                return;
            }
        }
    });

    cs.new_command("strsplice", "ssii", [](auto &, auto args, auto &res) {
        ostd::ConstCharRange s = args[0].get_strr();
        ostd::ConstCharRange vals = args[1].get_strr();
        CsInt skip   = args[2].get_int(),
              count  = args[3].get_int();
        CsInt offset = ostd::clamp(skip, CsInt(0), CsInt(s.size())),
              len    = ostd::clamp(count, CsInt(0), CsInt(s.size()) - offset);
        CsString p;
        p.reserve(s.size() - len + vals.size());
        if (offset) {
            p += s.slice(0, offset);
        }
        if (!vals.empty()) {
            p += vals;
        }
        if ((offset + len) < CsInt(s.size())) {
            p += s.slice(offset + len, s.size());
        }
        res.set_str(std::move(p));
    });
}

} /* namespace cscript */
