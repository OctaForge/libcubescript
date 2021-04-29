#include <cstdlib>
#include <cmath>
#include <climits>
#include <functional>
#include <algorithm>

#include <cubescript/cubescript.hh>

#include "cs_state.hh"

namespace cubescript {

static constexpr float_type PI = float_type(3.14159265358979323846);
static constexpr float_type LN2 = float_type(0.693147180559945309417);
static constexpr float_type RAD = PI / float_type(180.0);

template<typename T>
struct math_val;

template<>
struct math_val<integer_type> {
    static integer_type get(any_value &tv) {
        return tv.get_integer();
    }
    static void set(any_value &res, integer_type val) {
        res.set_integer(val);
    }
};

template<>
struct math_val<float_type> {
    static float_type get(any_value &tv) {
        return tv.get_float();
    }
    static void set(any_value &res, float_type val) {
        res.set_float(val);
    }
};

template<typename T>
struct math_noop {
    T operator()(T arg) {
        return arg;
    }
};

template<typename T, typename F1, typename F2>
static inline void math_op(
    span_type<any_value> args, any_value &res, T initval,
    F1 binop, F2 unop
) {
    T val;
    if (args.size() >= 2) {
        val = binop(math_val<T>::get(args[0]), math_val<T>::get(args[1]));
        for (size_t i = 2; i < args.size(); ++i) {
            val = binop(val, math_val<T>::get(args[i]));
        }
    } else {
        val = unop(!args.empty() ? math_val<T>::get(args[0]) : initval);
    }
    math_val<T>::set(res, val);
}

template<typename T, typename F>
static inline void cmp_op(span_type<any_value> args, any_value &res, F cmp) {
    bool val;
    if (args.size() >= 2) {
        val = cmp(math_val<T>::get(args[0]), math_val<T>::get(args[1]));
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cmp(math_val<T>::get(args[i - 1]), math_val<T>::get(args[i]));
        }
    } else {
        val = cmp(!args.empty() ? math_val<T>::get(args[0]) : T(0), T(0));
    }
    res.set_integer(integer_type(val));
}

LIBCUBESCRIPT_EXPORT void std_init_math(state &cs) {
    new_cmd_quiet(cs, "sin", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::sin(args[0].get_float() * RAD));
    });
    new_cmd_quiet(cs, "cos", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::cos(args[0].get_float() * RAD));
    });
    new_cmd_quiet(cs, "tan", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::tan(args[0].get_float() * RAD));
    });

    new_cmd_quiet(cs, "asin", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::asin(args[0].get_float()) / RAD);
    });
    new_cmd_quiet(cs, "acos", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::acos(args[0].get_float()) / RAD);
    });
    new_cmd_quiet(cs, "atan", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::atan(args[0].get_float()) / RAD);
    });
    new_cmd_quiet(cs, "atan2", "ff", [](auto &, auto args, auto &res) {
        res.set_float(std::atan2(args[0].get_float(), args[1].get_float()) / RAD);
    });

    new_cmd_quiet(cs, "sqrt", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::sqrt(args[0].get_float()));
    });
    new_cmd_quiet(cs, "loge", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log(args[0].get_float()));
    });
    new_cmd_quiet(cs, "log2", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log(args[0].get_float()) / LN2);
    });
    new_cmd_quiet(cs, "log10", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::log10(args[0].get_float()));
    });

    new_cmd_quiet(cs, "exp", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::exp(args[0].get_float()));
    });

    new_cmd_quiet(cs, "min", "i1...", [](auto &, auto args, auto &res) {
        integer_type v = (!args.empty() ? args[0].get_integer() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_integer());
        }
        res.set_integer(v);
    });
    new_cmd_quiet(cs, "max", "i1...", [](auto &, auto args, auto &res) {
        integer_type v = (!args.empty() ? args[0].get_integer() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::max(v, args[i].get_integer());
        }
        res.set_integer(v);
    });
    new_cmd_quiet(cs, "minf", "f1...", [](auto &, auto args, auto &res) {
        float_type v = (!args.empty() ? args[0].get_float() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_float());
        }
        res.set_float(v);
    });
    new_cmd_quiet(cs, "maxf", "f1...", [](auto &, auto args, auto &res) {
        float_type v = (!args.empty() ? args[0].get_float() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::max(v, args[i].get_float());
        }
        res.set_float(v);
    });

    new_cmd_quiet(cs, "abs", "i", [](auto &, auto args, auto &res) {
        res.set_integer(std::abs(args[0].get_integer()));
    });
    new_cmd_quiet(cs, "absf", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::abs(args[0].get_float()));
    });

    new_cmd_quiet(cs, "floor", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::floor(args[0].get_float()));
    });
    new_cmd_quiet(cs, "ceil", "f", [](auto &, auto args, auto &res) {
        res.set_float(std::ceil(args[0].get_float()));
    });

    new_cmd_quiet(cs, "round", "ff", [](auto &, auto args, auto &res) {
        float_type step = args[1].get_float();
        float_type r = args[0].get_float();
        if (step > 0) {
            r += float_type(step * ((r < 0) ? -0.5 : 0.5));
            r -= float_type(std::fmod(r, step));
        } else {
            r = float_type((r < 0) ? std::ceil(r - 0.5) : std::floor(r + 0.5));
        }
        res.set_float(r);
    });

    new_cmd_quiet(cs, "+", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(args, res, 0, std::plus<integer_type>(), math_noop<integer_type>());
    });
    new_cmd_quiet(cs, "*", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 1, std::multiplies<integer_type>(), math_noop<integer_type>()
        );
    });
    new_cmd_quiet(cs, "-", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, std::minus<integer_type>(), std::negate<integer_type>()
        );
    });

    new_cmd_quiet(cs, "^", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, std::bit_xor<integer_type>(), [](integer_type val) { return ~val; }
        );
    });
    new_cmd_quiet(cs, "~", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, std::bit_xor<integer_type>(), [](integer_type val) { return ~val; }
        );
    });
    new_cmd_quiet(cs, "&", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, std::bit_and<integer_type>(), math_noop<integer_type>()
        );
    });
    new_cmd_quiet(cs, "|", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, std::bit_or<integer_type>(), math_noop<integer_type>()
        );
    });

    /* special combined cases */
    new_cmd_quiet(cs, "^~", "i1...", [](auto &, auto args, auto &res) {
        integer_type val;
        if (args.size() >= 2) {
            val = args[0].get_integer() ^ ~args[1].get_integer();
            for (size_t i = 2; i < args.size(); ++i) {
                val ^= ~args[i].get_integer();
            }
        } else {
            val = !args.empty() ? args[0].get_integer() : 0;
        }
        res.set_integer(val);
    });
    new_cmd_quiet(cs, "&~", "i1...", [](auto &, auto args, auto &res) {
        integer_type val;
        if (args.size() >= 2) {
            val = args[0].get_integer() & ~args[1].get_integer();
            for (size_t i = 2; i < args.size(); ++i) {
                val &= ~args[i].get_integer();
            }
        } else {
            val = !args.empty() ? args[0].get_integer() : 0;
        }
        res.set_integer(val);
    });
    new_cmd_quiet(cs, "|~", "i1...", [](auto &, auto args, auto &res) {
        integer_type val;
        if (args.size() >= 2) {
            val = args[0].get_integer() | ~args[1].get_integer();
            for (size_t i = 2; i < args.size(); ++i) {
                val |= ~args[i].get_integer();
            }
        } else {
            val = !args.empty() ? args[0].get_integer() : 0;
        }
        res.set_integer(val);
    });

    new_cmd_quiet(cs, "<<", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, [](integer_type val1, integer_type val2) {
                return (val2 < integer_type(sizeof(integer_type) * CHAR_BIT))
                    ? (val1 << std::max(val2, integer_type(0)))
                    : 0;
            }, math_noop<integer_type>()
        );
    });
    new_cmd_quiet(cs, ">>", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, [](integer_type val1, integer_type val2) {
                return val1 >> std::clamp(
                    val2, integer_type(0), integer_type(sizeof(integer_type) * CHAR_BIT)
                );
            }, math_noop<integer_type>()
        );
    });

    new_cmd_quiet(cs, "+f", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 0, std::plus<float_type>(), math_noop<float_type>()
        );
    });
    new_cmd_quiet(cs, "*f", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 1, std::multiplies<float_type>(), math_noop<float_type>()
        );
    });
    new_cmd_quiet(cs, "-f", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 0, std::minus<float_type>(), std::negate<float_type>()
        );
    });

    new_cmd_quiet(cs, "div", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, [](integer_type val1, integer_type val2) {
                if (val2) {
                    return val1 / val2;
                }
                return integer_type(0);
            }, math_noop<integer_type>()
        );
    });
    new_cmd_quiet(cs, "mod", "i1...", [](auto &, auto args, auto &res) {
        math_op<integer_type>(
            args, res, 0, [](integer_type val1, integer_type val2) {
                if (val2) {
                    return val1 % val2;
                }
                return integer_type(0);
            }, math_noop<integer_type>()
        );
    });
    new_cmd_quiet(cs, "divf", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 0, [](float_type val1, float_type val2) {
                if (val2) {
                    return val1 / val2;
                }
                return float_type(0);
            }, math_noop<float_type>()
        );
    });
    new_cmd_quiet(cs, "modf", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 0, [](float_type val1, float_type val2) {
                if (val2) {
                    return float_type(fmod(val1, val2));
                }
                return float_type(0);
            }, math_noop<float_type>()
        );
    });

    new_cmd_quiet(cs, "pow", "f1...", [](auto &, auto args, auto &res) {
        math_op<float_type>(
            args, res, 0, [](float_type val1, float_type val2) {
                return float_type(pow(val1, val2));
            }, math_noop<float_type>()
        );
    });

    new_cmd_quiet(cs, "=", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::equal_to<integer_type>());
    });
    new_cmd_quiet(cs, "!=", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::not_equal_to<integer_type>());
    });
    new_cmd_quiet(cs, "<", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::less<integer_type>());
    });
    new_cmd_quiet(cs, ">", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::greater<integer_type>());
    });
    new_cmd_quiet(cs, "<=", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::less_equal<integer_type>());
    });
    new_cmd_quiet(cs, ">=", "i1...", [](auto &, auto args, auto &res) {
        cmp_op<integer_type>(args, res, std::greater_equal<integer_type>());
    });

    new_cmd_quiet(cs, "=f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::equal_to<float_type>());
    });
    new_cmd_quiet(cs, "!=f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::not_equal_to<float_type>());
    });
    new_cmd_quiet(cs, "<f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::less<float_type>());
    });
    new_cmd_quiet(cs, ">f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::greater<float_type>());
    });
    new_cmd_quiet(cs, "<=f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::less_equal<float_type>());
    });
    new_cmd_quiet(cs, ">=f", "f1...", [](auto &, auto args, auto &res) {
        cmp_op<float_type>(args, res, std::greater_equal<float_type>());
    });
}

} /* namespace cubescript */
