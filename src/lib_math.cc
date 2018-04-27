#include <cstdlib>
#include <cmath>
#include <climits>
#include <functional>
#include <algorithm>

#include <cubescript/cubescript.hh>

namespace cscript {

static constexpr cs_float PI = 3.14159265358979f;
static constexpr cs_float RAD = PI / 180.0f;

template<typename T>
struct cs_math_val;

template<>
struct cs_math_val<cs_int> {
    static cs_int get(cs_value &tv) {
        return tv.get_int();
    }
    static void set(cs_value &res, cs_int val) {
        res.set_int(val);
    }
};

template<>
struct cs_math_val<cs_float> {
    static cs_float get(cs_value &tv) {
        return tv.get_float();
    }
    static void set(cs_value &res, cs_float val) {
        res.set_float(val);
    }
};

template<typename T>
struct cs_math_noop {
    T operator()(T arg) {
        return arg;
    }
};

template<typename T, typename F1, typename F2>
static inline void cs_mathop(
    cs_value_r args, cs_value &res, T initval,
    F1 binop, F2 unop
) {
    T val;
    if (args.size() >= 2) {
        val = binop(cs_math_val<T>::get(args[0]), cs_math_val<T>::get(args[1]));
        for (size_t i = 2; i < args.size(); ++i) {
            val = binop(val, cs_math_val<T>::get(args[i]));
        }
    } else {
        val = unop(!args.empty() ? cs_math_val<T>::get(args[0]) : initval);
    }
    cs_math_val<T>::set(res, val);
}

template<typename T, typename F>
static inline void cs_cmpop(cs_value_r args, cs_value &res, F cmp) {
    bool val;
    if (args.size() >= 2) {
        val = cmp(cs_math_val<T>::get(args[0]), cs_math_val<T>::get(args[1]));
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cmp(cs_math_val<T>::get(args[i - 1]), cs_math_val<T>::get(args[i]));
        }
    } else {
        val = cmp(!args.empty() ? cs_math_val<T>::get(args[0]) : T(0), T(0));
    }
    res.set_int(cs_int(val));
}

void cs_init_lib_math(cs_state &cs) {
    cs.new_command("sin", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::sin(args[0].get_float() * RAD));
    });
    cs.new_command("cos", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::cos(args[0].get_float() * RAD));
    });
    cs.new_command("tan", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::tan(args[0].get_float() * RAD));
    });

    cs.new_command("asin", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::asin(args[0].get_float()) / RAD);
    });
    cs.new_command("acos", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::acos(args[0].get_float()) / RAD);
    });
    cs.new_command("atan", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::atan(args[0].get_float()) / RAD);
    });
    cs.new_command("atan2", "ff", [](auto &, auto args, auto &res) {
        res.set_float(std::atan2(args[0].get_float(), args[1].get_float()) / RAD);
    });

    cs.new_command("sqrt", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::sqrt(args[0].get_float()));
    });
    cs.new_command("loge", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log(args[0].get_float()));
    });
    cs.new_command("log2", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log(args[0].get_float()) / M_LN2);
    });
    cs.new_command("log10", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log10(args[0].get_float()));
    });

    cs.new_command("exp", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::exp(args[0].get_float()));
    });

    cs.new_command("min", "i1V", [](auto &, auto args, auto &res) {
        cs_int v = (!args.empty() ? args[0].get_int() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.new_command("max", "i1V", [](auto &, auto args, auto &res) {
        cs_int v = (!args.empty() ? args[0].get_int() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::max(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.new_command("minf", "f1V", [](auto &, auto args, auto &res) {
        cs_float v = (!args.empty() ? args[0].get_float() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_float());
        }
        res.set_float(v);
    });
    cs.new_command("maxf", "f1V", [](auto &, auto args, auto &res) {
        cs_float v = (!args.empty() ? args[0].get_float() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::max(v, args[i].get_float());
        }
        res.set_float(v);
    });

    cs.new_command("abs", "i", [](auto &, auto args, auto &res) {
        res.set_int(std::abs(args[0].get_int()));
    });
    cs.new_command("absf", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::abs(args[0].get_float()));
    });

    cs.new_command("floor", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::floor(args[0].get_float()));
    });
    cs.new_command("ceil", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::ceil(args[0].get_float()));
    });

    cs.new_command("round", "ff", [](auto &, auto args, auto &res) {
        cs_float step = args[1].get_float();
        cs_float r = args[0].get_float();
        if (step > 0) {
            r += step * ((r < 0) ? -0.5 : 0.5);
            r -= std::fmod(r, step);
        } else {
            r = (r < 0) ? std::ceil(r - 0.5) : std::floor(r + 0.5);
        }
        res.set_float(r);
    });

    cs.new_command("+", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(args, res, 0, std::plus<cs_int>(), cs_math_noop<cs_int>());
    });
    cs.new_command("*", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 1, std::multiplies<cs_int>(), cs_math_noop<cs_int>()
        );
    });
    cs.new_command("-", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, std::minus<cs_int>(), std::negate<cs_int>()
        );
    });

    cs.new_command("^", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, std::bit_xor<cs_int>(), [](cs_int val) { return ~val; }
        );
    });
    cs.new_command("~", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, std::bit_xor<cs_int>(), [](cs_int val) { return ~val; }
        );
    });
    cs.new_command("&", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, std::bit_and<cs_int>(), cs_math_noop<cs_int>()
        );
    });
    cs.new_command("|", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, std::bit_or<cs_int>(), cs_math_noop<cs_int>()
        );
    });

    /* special combined cases */
    cs.new_command("^~", "i1V", [](auto &, auto args, auto &res) {
        cs_int val;
        if (args.size() >= 2) {
            val = args[0].get_int() ^ ~args[1].get_int();
            for (size_t i = 2; i < args.size(); ++i) {
                val ^= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });
    cs.new_command("&~", "i1V", [](auto &, auto args, auto &res) {
        cs_int val;
        if (args.size() >= 2) {
            val = args[0].get_int() & ~args[1].get_int();
            for (size_t i = 2; i < args.size(); ++i) {
                val &= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });
    cs.new_command("|~", "i1V", [](auto &, auto args, auto &res) {
        cs_int val;
        if (args.size() >= 2) {
            val = args[0].get_int() | ~args[1].get_int();
            for (size_t i = 2; i < args.size(); ++i) {
                val |= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });

    cs.new_command("<<", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, [](cs_int val1, cs_int val2) {
                return (val2 < cs_int(sizeof(cs_int) * CHAR_BIT))
                    ? (val1 << std::max(val2, cs_int(0)))
                    : 0;
            }, cs_math_noop<cs_int>()
        );
    });
    cs.new_command(">>", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, [](cs_int val1, cs_int val2) {
                return val1 >> std::clamp(
                    val2, cs_int(0), cs_int(sizeof(cs_int) * CHAR_BIT)
                );
            }, cs_math_noop<cs_int>()
        );
    });

    cs.new_command("+f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 0, std::plus<cs_float>(), cs_math_noop<cs_float>()
        );
    });
    cs.new_command("*f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 1, std::multiplies<cs_float>(), cs_math_noop<cs_float>()
        );
    });
    cs.new_command("-f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 0, std::minus<cs_float>(), std::negate<cs_float>()
        );
    });

    cs.new_command("div", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, [](cs_int val1, cs_int val2) {
                if (val2) {
                    return val1 / val2;
                }
                return cs_int(0);
            }, cs_math_noop<cs_int>()
        );
    });
    cs.new_command("mod", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_int>(
            args, res, 0, [](cs_int val1, cs_int val2) {
                if (val2) {
                    return val1 % val2;
                }
                return cs_int(0);
            }, cs_math_noop<cs_int>()
        );
    });
    cs.new_command("divf", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 0, [](cs_float val1, cs_float val2) {
                if (val2) {
                    return val1 / val2;
                }
                return cs_float(0);
            }, cs_math_noop<cs_float>()
        );
    });
    cs.new_command("modf", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 0, [](cs_float val1, cs_float val2) {
                if (val2) {
                    return cs_float(fmod(val1, val2));
                }
                return cs_float(0);
            }, cs_math_noop<cs_float>()
        );
    });

    cs.new_command("pow", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<cs_float>(
            args, res, 0, [](cs_float val1, cs_float val2) {
                return cs_float(pow(val1, val2));
            }, cs_math_noop<cs_float>()
        );
    });

    cs.new_command("=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::equal_to<cs_int>());
    });
    cs.new_command("!=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::not_equal_to<cs_int>());
    });
    cs.new_command("<", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::less<cs_int>());
    });
    cs.new_command(">", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::greater<cs_int>());
    });
    cs.new_command("<=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::less_equal<cs_int>());
    });
    cs.new_command(">=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_int>(args, res, std::greater_equal<cs_int>());
    });

    cs.new_command("=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::equal_to<cs_float>());
    });
    cs.new_command("!=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::not_equal_to<cs_float>());
    });
    cs.new_command("<f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::less<cs_float>());
    });
    cs.new_command(">f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::greater<cs_float>());
    });
    cs.new_command("<=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::less_equal<cs_float>());
    });
    cs.new_command(">=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<cs_float>(args, res, std::greater_equal<cs_float>());
    });
}

} /* namespace cscript */
