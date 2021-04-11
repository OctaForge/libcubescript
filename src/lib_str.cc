#include <functional>
#include <iterator>

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_strman.hh"
#include "cs_thread.hh"

namespace cubescript {

template<typename F>
static inline void str_cmp_by(
    state &cs, std::span<any_value> args, any_value &res, F cfunc
) {
    bool val;
    if (args.size() >= 2) {
        val = cfunc(args[0].get_string(cs), args[1].get_string(cs));
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cfunc(args[i - 1].get_string(cs), args[i].get_string(cs));
        }
    } else {
        val = cfunc(
            !args.empty() ? args[0].get_string(cs) : std::string_view(),
            std::string_view()
        );
    }
    res.set_integer(integer_type(val));
}

LIBCUBESCRIPT_EXPORT void std_init_string(state &cs) {
    new_cmd_quiet(cs, "strstr", "ss", [](auto &ccs, auto args, auto &res) {
        std::string_view a = args[0].get_string(ccs);
        std::string_view b = args[1].get_string(ccs);
        auto pos = a.find(b);
        if (pos == a.npos) {
            res.set_integer(-1);
        } else {
            res.set_integer(integer_type(pos));
        }
    });

    new_cmd_quiet(cs, "strlen", "s", [](auto &ccs, auto args, auto &res) {
        res.set_integer(integer_type(args[0].get_string(ccs).size()));
    });

    new_cmd_quiet(cs, "strcode", "si", [](auto &ccs, auto args, auto &res) {
        std::string_view str = args[0].get_string(ccs);
        integer_type i = args[1].get_integer();
        if (i >= integer_type(str.size())) {
            res.set_integer(0);
        } else {
            res.set_integer(static_cast<unsigned char>(str[i]));
        }
    });

    new_cmd_quiet(cs, "codestr", "i", [](auto &ccs, auto args, auto &res) {
        char const p[2] = { char(args[0].get_integer()), '\0' };
        res.set_string(std::string_view{static_cast<char const *>(p)}, ccs);
    });

    new_cmd_quiet(cs, "strlower", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = args[0].get_string(ccs);
        auto *ics = state_p{ccs}.ts().istate;
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (std::size_t i = 0; i < inps.size(); ++i) {
            buf[i] = char(tolower(inps.data()[i]));
        }
        res.set_string(ics->strman->steal(buf));
    });

    new_cmd_quiet(cs, "strupper", "s", [](auto &ccs, auto args, auto &res) {
        auto inps = args[0].get_string(ccs);
        auto *ics = state_p{ccs}.ts().istate;
        auto *buf = ics->strman->alloc_buf(inps.size());
        for (std::size_t i = 0; i < inps.size(); ++i) {
            buf[i] = char(toupper(inps.data()[i]));
        }
        res.set_string(ics->strman->steal(buf));
    });

    new_cmd_quiet(cs, "escape", "s", [](auto &ccs, auto args, auto &res) {
        charbuf s{ccs};
        escape_string(std::back_inserter(s), args[0].get_string(ccs));
        res.set_string(s.str(), ccs);
    });

    new_cmd_quiet(cs, "unescape", "s", [](auto &ccs, auto args, auto &res) {
        charbuf s{ccs};
        unescape_string(std::back_inserter(s), args[0].get_string(ccs));
        res.set_string(s.str(), ccs);
    });

    new_cmd_quiet(cs, "concat", "V", [](auto &ccs, auto args, auto &res) {
        res.set_string(concat_values(ccs, args, " "));
    });

    new_cmd_quiet(cs, "concatword", "V", [](auto &ccs, auto args, auto &res) {
        res.set_string(concat_values(ccs, args));
    });

    new_cmd_quiet(cs, "format", "V", [](auto &ccs, auto args, auto &res) {
        if (args.empty()) {
            return;
        }
        charbuf s{ccs};
        string_ref fs = args[0].get_string(ccs);
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
                        s.append(args[i].get_string(ccs));
                    }
                } else {
                    s.push_back(ic);
                }
            } else {
                s.push_back(c);
            }
        }
        res.set_string(s.str(), ccs);
    });

    new_cmd_quiet(cs, "tohex", "ii", [](auto &ccs, auto args, auto &res) {
        char buf[32];
        /* use long long as the largest signed integer type */
        auto val = static_cast<long long>(args[0].get_integer());
        int prec = std::max(int(args[1].get_integer()), 1);
        int n = snprintf(buf, sizeof(buf), "0x%.*llX", prec, val);
        if (n >= int(sizeof(buf))) {
            charbuf s{ccs};
            s.reserve(n + 1);
            s.data()[0] = '\0';
            int nn = snprintf(s.data(), n + 1, "0x%.*llX", prec, val);
            if ((nn > 0) && (nn <= n)) {
                res.set_string(
                    std::string_view{s.data(), std::size_t(nn)}, ccs
                );
                return;
            }
        } else if (n > 0) {
            res.set_string(static_cast<char const *>(buf), ccs);
            return;
        }
        /* should pretty much be unreachable */
        throw internal_error{"format error"};
    });

    new_cmd_quiet(cs, "substr", "siiN", [](auto &ccs, auto args, auto &res) {
        std::string_view s = args[0].get_string(ccs);
        auto start = args[1].get_integer(), count = args[2].get_integer();
        auto numargs = args[3].get_integer();
        auto len = integer_type(s.size());
        auto offset = std::clamp(start, integer_type(0), len);
        res.set_string(std::string_view{
            &s[offset],
            ((numargs >= 3)
                ? size_t(std::clamp(count, integer_type(0), len - offset))
                : size_t(len - offset))
        }, ccs);
    });

    new_cmd_quiet(cs, "strcmp", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::equal_to<std::string_view>());
    });
    new_cmd_quiet(cs, "=s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::equal_to<std::string_view>());
    });
    new_cmd_quiet(cs, "!=s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::not_equal_to<std::string_view>());
    });
    new_cmd_quiet(cs, "<s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::less<std::string_view>());
    });
    new_cmd_quiet(cs, ">s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::greater<std::string_view>());
    });
    new_cmd_quiet(cs, "<=s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::less_equal<std::string_view>());
    });
    new_cmd_quiet(cs, ">=s", "s1V", [](auto &ccs, auto args, auto &res) {
        str_cmp_by(ccs, args, res, std::greater_equal<std::string_view>());
    });

    new_cmd_quiet(cs, "strreplace", "ssss", [](
        auto &ccs, auto args, auto &res
    ) {
        std::string_view s = args[0].get_string(ccs);
        std::string_view oldval = args[1].get_string(ccs),
                         newval = args[2].get_string(ccs),
                         newval2 = args[3].get_string(ccs);
        if (newval2.empty()) {
            newval2 = newval;
        }
        if (oldval.empty()) {
            res.set_string(s, ccs);
            return;
        }
        charbuf buf{ccs};
        for (size_t i = 0;; ++i) {
            auto p = s.find(oldval);
            if (p == s.npos) {
                buf.append(s);
                res.set_string(s, ccs);
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

    new_cmd_quiet(cs, "strsplice", "ssii", [](
        auto &ccs, auto args, auto &res
    ) {
        std::string_view s = args[0].get_string(ccs);
        std::string_view vals = args[1].get_string(ccs);
        integer_type skip  = args[2].get_integer(),
              count  = args[3].get_integer();
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
        res.set_string(p.str(), ccs);
    });
}

} /* namespace cubescript */
