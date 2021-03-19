#include <functional>

#include <cubescript/cubescript.hh>

#include "cs_util.hh"

namespace cscript {

template<typename F>
static inline void cs_strgcmp(cs_value_r args, cs_value &res, F cfunc) {
    bool val;
    if (args.size() >= 2) {
        val = cfunc(args[0].get_str(), args[1].get_str());
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cfunc(args[i - 1].get_str(), args[i].get_str());
        }
    } else {
        val = cfunc(
            !args.empty() ? args[0].get_str() : ostd::string_range(),
            ostd::string_range()
        );
    }
    res.set_int(cs_int(val));
};

void cs_init_lib_string(cs_state &cs) {
    cs.new_command("strstr", "ss", [](auto &, auto args, auto &res) {
        ostd::string_range a = args[0].get_str(), b = args[1].get_str();
        ostd::string_range s = a;
        for (cs_int i = 0; b.size() <= s.size(); ++i) {
            if (b == s.slice(0, b.size())) {
                res.set_int(i);
                return;
            }
            ++s;
        }
        res.set_int(-1);
    });

    cs.new_command("strlen", "s", [](auto &, auto args, auto &res) {
        res.set_int(cs_int(args[0].get_str().size()));
    });

    cs.new_command("strcode", "si", [](auto &, auto args, auto &res) {
        ostd::string_range str = args[0].get_str();
        cs_int i = args[1].get_int();
        if (i >= cs_int(str.size())) {
            res.set_int(0);
        } else {
            res.set_int(static_cast<unsigned char>(str[i]));
        }
    });

    cs.new_command("codestr", "i", [](auto &, auto args, auto &res) {
        char const p[2] = { char(args[0].get_int()), '\0' };
        res.set_str(ostd::string_range{static_cast<char const *>(p)});
    });

    cs.new_command("strlower", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = ostd::string_range{args[0].get_str()};
        auto *ics = cs_get_sstate(ccs);
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (auto i: ostd::range(inps.size())) {
            buf[i] = tolower(inps[i]);
        }
        auto const *cbuf = ics->strman->steal(buf);
        auto sr = cs_make_strref(cbuf, *ics);
        ics->strman->unref(cbuf);
        res.set_str(sr);
    });

    cs.new_command("strupper", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = ostd::string_range{args[0].get_str()};
        auto *ics = cs_get_sstate(ccs);
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (auto i: ostd::range(inps.size())) {
            buf[i] = toupper(inps[i]);
        }
        auto const *cbuf = ics->strman->steal(buf);
        auto sr = cs_make_strref(cbuf, *ics);
        ics->strman->unref(cbuf);
        res.set_str(sr);
    });

    cs.new_command("escape", "s", [](auto &ccs, auto args, auto &res) {
        auto s = ostd::appender<cs_charbuf>(ccs);
        util::escape_string(s, args[0].get_str());
        res.set_str(s.get().str());
    });

    cs.new_command("unescape", "s", [](auto &ccs, auto args, auto &res) {
        auto s = ostd::appender<cs_charbuf>(ccs);
        util::unescape_string(s, args[0].get_str());
        res.set_str(s.get().str());
    });

    cs.new_command("concat", "V", [](auto &ccs, auto args, auto &res) {
        res.set_str(value_list_concat(ccs, args, " "));
    });

    cs.new_command("concatword", "V", [](auto &ccs, auto args, auto &res) {
        res.set_str(value_list_concat(ccs, args));
    });

    cs.new_command("format", "V", [](auto &, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        cs_string s;
        cs_strref fs = args[0].get_str();
        ostd::string_range f{fs};
        while (!f.empty()) {
            char c = *f;
            ++f;
            if ((c == '%') && !f.empty()) {
                char ic = *f;
                ++f;
                if (ic >= '1' && ic <= '9') {
                    int i = ic - '0';
                    if (size_t(i) < args.size()) {
                        s += ostd::string_range{args[i].get_str()};
                    }
                } else {
                    s += ic;
                }
            } else {
                s += c;
            }
        }
        res.set_str(s);
    });

    cs.new_command("tohex", "ii", [](auto &, auto args, auto &res) {
        auto r = ostd::appender<cs_string>();
        try {
            ostd::format(
                r, "0x%.*X", std::max(args[1].get_int(), cs_int(1)),
                args[0].get_int()
            );
        } catch (ostd::format_error const &e) {
            throw cs_internal_error{e.what()};
        }
        res.set_str(r.get());
    });

    cs.new_command("substr", "siiN", [](auto &, auto args, auto &res) {
        ostd::string_range s = args[0].get_str();
        cs_int start = args[1].get_int(), count = args[2].get_int();
        cs_int numargs = args[3].get_int();
        cs_int len = cs_int(s.size()), offset = std::clamp(start, cs_int(0), len);
        res.set_str(ostd::string_range{
            &s[offset],
            &s[offset] + ((numargs >= 3)
                ? size_t(std::clamp(count, cs_int(0), len - offset))
                : size_t(len - offset))
        });
    });

    cs.new_command("strcmp", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::equal_to<ostd::string_range>());
    });
    cs.new_command("=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::equal_to<ostd::string_range>());
    });
    cs.new_command("!=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::not_equal_to<ostd::string_range>());
    });
    cs.new_command("<s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::less<ostd::string_range>());
    });
    cs.new_command(">s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::greater<ostd::string_range>());
    });
    cs.new_command("<=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::less_equal<ostd::string_range>());
    });
    cs.new_command(">=s", "s1V", [](auto &, auto args, auto &res) {
        cs_strgcmp(args, res, std::greater_equal<ostd::string_range>());
    });

    cs.new_command("strreplace", "ssss", [](auto &, auto args, auto &res) {
        ostd::string_range s = args[0].get_str();
        ostd::string_range oldval = args[1].get_str(),
                             newval = args[2].get_str(),
                             newval2 = args[3].get_str();
        if (newval2.empty()) {
            newval2 = newval;
        }
        cs_string buf;
        if (!oldval.size()) {
            res.set_str(s);
            return;
        }
        for (size_t i = 0;; ++i) {
            ostd::string_range found;
            ostd::string_range trys = s;
            for (; oldval.size() <= trys.size(); ++trys) {
                if (trys.slice(0, oldval.size()) == oldval) {
                    found = trys;
                    break;
                }
            }
            if (!found.empty()) {
                buf += s.slice(0, &found[0] - &s[0]);
                buf += (i & 1) ? newval2 : newval;
                s = found.slice(oldval.size(), found.size());
            } else {
                buf += s;
                res.set_str(buf);
                return;
            }
        }
    });

    cs.new_command("strsplice", "ssii", [](auto &, auto args, auto &res) {
        ostd::string_range s = args[0].get_str();
        ostd::string_range vals = args[1].get_str();
        cs_int skip   = args[2].get_int(),
              count  = args[3].get_int();
        cs_int offset = std::clamp(skip, cs_int(0), cs_int(s.size())),
              len    = std::clamp(count, cs_int(0), cs_int(s.size()) - offset);
        cs_string p;
        p.reserve(s.size() - len + vals.size());
        if (offset) {
            p += s.slice(0, offset);
        }
        if (!vals.empty()) {
            p += vals;
        }
        if ((offset + len) < cs_int(s.size())) {
            p += s.slice(offset + len, s.size());
        }
        res.set_str(p);
    });
}

} /* namespace cscript */
