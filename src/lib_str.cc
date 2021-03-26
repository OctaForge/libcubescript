#include <functional>
#include <iterator>

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_strman.hh"
#include "cs_thread.hh"

namespace cubescript {

template<typename F>
static inline void str_cmp_by(
    std::span<any_value> args, any_value &res, F cfunc
) {
    bool val;
    if (args.size() >= 2) {
        val = cfunc(args[0].get_str(), args[1].get_str());
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cfunc(args[i - 1].get_str(), args[i].get_str());
        }
    } else {
        val = cfunc(
            !args.empty() ? args[0].get_str() : std::string_view(),
            std::string_view()
        );
    }
    res.set_int(integer_type(val));
};

void init_lib_string(state &cs) {
    cs.new_command("strstr", "ss", [](auto &, auto args, auto &res) {
        std::string_view a = args[0].get_str(), b = args[1].get_str();
        auto pos = a.find(b);
        if (pos == a.npos) {
            res.set_int(-1);
        } else {
            res.set_int(integer_type(pos));
        }
    });

    cs.new_command("strlen", "s", [](auto &, auto args, auto &res) {
        res.set_int(integer_type(args[0].get_str().size()));
    });

    cs.new_command("strcode", "si", [](auto &, auto args, auto &res) {
        std::string_view str = args[0].get_str();
        integer_type i = args[1].get_int();
        if (i >= integer_type(str.size())) {
            res.set_int(0);
        } else {
            res.set_int(static_cast<unsigned char>(str[i]));
        }
    });

    cs.new_command("codestr", "i", [](auto &, auto args, auto &res) {
        char const p[2] = { char(args[0].get_int()), '\0' };
        res.set_str(std::string_view{static_cast<char const *>(p)});
    });

    cs.new_command("strlower", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = std::string_view{args[0].get_str()};
        auto *ics = ccs.thread_pointer()->istate;
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (std::size_t i = 0; i < inps.size(); ++i) {
            buf[i] = tolower(inps[i]);
        }
        res.set_str(ics->strman->steal(buf));
    });

    cs.new_command("strupper", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = std::string_view{args[0].get_str()};
        auto *ics = ccs.thread_pointer()->istate;
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (std::size_t i = 0; i < inps.size(); ++i) {
            buf[i] = toupper(inps[i]);
        }
        res.set_str(ics->strman->steal(buf));
    });

    cs.new_command("escape", "s", [](auto &ccs, auto args, auto &res) {
        charbuf s{ccs};
        escape_string(std::back_inserter(s), args[0].get_str());
        res.set_str(s.str());
    });

    cs.new_command("unescape", "s", [](auto &ccs, auto args, auto &res) {
        charbuf s{ccs};
        unescape_string(std::back_inserter(s), args[0].get_str());
        res.set_str(s.str());
    });

    cs.new_command("concat", "V", [](auto &ccs, auto args, auto &res) {
        res.set_str(concat_values(ccs, args, " "));
    });

    cs.new_command("concatword", "V", [](auto &ccs, auto args, auto &res) {
        res.set_str(concat_values(ccs, args));
    });

    cs.new_command("format", "V", [](auto &ccs, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        charbuf s{ccs};
        string_ref fs = args[0].get_str();
        std::string_view f{fs};
        for (auto it = f.begin(); it != f.end(); ++it) {
            char c = *it;
            ++it;
            if ((c == '%') && (it != f.end())) {
                char ic = *it;
                ++it;
                if ((ic >= '1') && (ic <= '9')) {
                    int i = ic - '0';
                    if (std::size_t(i) < args.size()) {
                        s.append(args[i].get_str());
                    }
                } else {
                    s.push_back(ic);
                }
            } else {
                s.push_back(c);
            }
        }
        res.set_str(s.str());
    });

    cs.new_command("tohex", "ii", [](auto &ccs, auto args, auto &res) {
        char buf[32];
        /* use long long as the largest signed integer type */
        auto val = static_cast<long long>(args[0].get_int());
        int prec = std::max(int(args[1].get_int()), 1);
        int n = snprintf(buf, sizeof(buf), "0x%.*llX", prec, val);
        if (n >= int(sizeof(buf))) {
            charbuf s{ccs};
            s.reserve(n + 1);
            s.data()[0] = '\0';
            int nn = snprintf(s.data(), n + 1, "0x%.*llX", prec, val);
            if ((nn > 0) && (nn <= n)) {
                res.set_str(std::string_view{s.data(), std::size_t(nn)});
                return;
            }
        } else if (n > 0) {
            res.set_str(static_cast<char const *>(buf));
            return;
        }
        /* should pretty much be unreachable */
        throw internal_error{"format error"};
    });

    cs.new_command("substr", "siiN", [](auto &, auto args, auto &res) {
        std::string_view s = args[0].get_str();
        integer_type start = args[1].get_int(), count = args[2].get_int();
        integer_type numargs = args[3].get_int();
        integer_type len = integer_type(s.size()), offset = std::clamp(start, integer_type(0), len);
        res.set_str(std::string_view{
            &s[offset],
            ((numargs >= 3)
                ? size_t(std::clamp(count, integer_type(0), len - offset))
                : size_t(len - offset))
        });
    });

    cs.new_command("strcmp", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::equal_to<std::string_view>());
    });
    cs.new_command("=s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::equal_to<std::string_view>());
    });
    cs.new_command("!=s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::not_equal_to<std::string_view>());
    });
    cs.new_command("<s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::less<std::string_view>());
    });
    cs.new_command(">s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::greater<std::string_view>());
    });
    cs.new_command("<=s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::less_equal<std::string_view>());
    });
    cs.new_command(">=s", "s1V", [](auto &, auto args, auto &res) {
        str_cmp_by(args, res, std::greater_equal<std::string_view>());
    });

    cs.new_command("strreplace", "ssss", [](auto &ccs, auto args, auto &res) {
        std::string_view s = args[0].get_str();
        std::string_view oldval = args[1].get_str(),
                         newval = args[2].get_str(),
                         newval2 = args[3].get_str();
        if (newval2.empty()) {
            newval2 = newval;
        }
        if (oldval.empty()) {
            res.set_str(s);
            return;
        }
        charbuf buf{ccs};
        for (size_t i = 0;; ++i) {
            std::string_view found;
            auto p = s.find(oldval);
            if (p == s.npos) {
                buf.append(s);
                res.set_str(s);
                return;
            }
            buf.append(s.substr(0, p));
            buf.append((i & 1) ? newval2 : newval);
            buf.append(s.substr(
                p + oldval.size(),
                s.size() - p - oldval.size()
            ));
        }
    });

    cs.new_command("strsplice", "ssii", [](auto &ccs, auto args, auto &res) {
        std::string_view s = args[0].get_str();
        std::string_view vals = args[1].get_str();
        integer_type skip  = args[2].get_int(),
              count  = args[3].get_int();
        integer_type offset = std::clamp(skip, integer_type(0), integer_type(s.size())),
              len     = std::clamp(count, integer_type(0), integer_type(s.size()) - offset);
        charbuf p{ccs};
        p.reserve(s.size() - len + vals.size());
        if (offset) {
            p.append(s.substr(0, offset));
        }
        p.append(vals);
        if ((offset + len) < integer_type(s.size())) {
            p.append(s.substr(offset + len, s.size() - offset - len));
        }
        res.set_str(p.str());
    });
}

} /* namespace cubescript */
