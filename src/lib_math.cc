#include <cstdlib>
#include <cmath>
#include <climits>
#include <functional>
#include <algorithm>

#include "cubescript/cubescript.hh"

namespace cscript {

static constexpr CsFloat PI = 3.14159265358979f;
static constexpr CsFloat RAD = PI / 180.0f;

template<typename T>
struct CsMathVal;

template<>
struct CsMathVal<CsInt> {
    static CsInt get(CsValue &tv) {
        return tv.get_int();
    }
    static void set(CsValue &res, CsInt val) {
        res.set_int(val);
    }
};

template<>
struct CsMathVal<CsFloat> {
    static CsFloat get(CsValue &tv) {
        return tv.get_float();
    }
    static void set(CsValue &res, CsFloat val) {
        res.set_float(val);
    }
};

template<typename T>
struct CsMathNoop {
    T operator()(T arg) {
        return arg;
    }
};

template<typename T, typename F1, typename F2>
static inline void cs_mathop(
    CsValueRange args, CsValue &res, T initval,
    F1 binop, F2 unop
) {
    T val;
    if (args.size() >= 2) {
        val = binop(CsMathVal<T>::get(args[0]), CsMathVal<T>::get(args[1]));
        for (size_t i = 2; i < args.size(); ++i) {
            val = binop(val, CsMathVal<T>::get(args[i]));
        }
    } else {
        val = unop(!args.empty() ? CsMathVal<T>::get(args[0]) : initval);
    }
    CsMathVal<T>::set(res, val);
}

template<typename T, typename F>
static inline void cs_cmpop(CsValueRange args, CsValue &res, F cmp) {
    bool val;
    if (args.size() >= 2) {
        val = cmp(CsMathVal<T>::get(args[0]), CsMathVal<T>::get(args[1]));
        for (size_t i = 2; (i < args.size()) && val; ++i) {
            val = cmp(CsMathVal<T>::get(args[i - 1]), CsMathVal<T>::get(args[i]));
        }
    } else {
        val = cmp(!args.empty() ? CsMathVal<T>::get(args[0]) : T(0), T(0));
    }
    res.set_int(CsInt(val));
}

void cs_init_lib_math(CsState &cs) {
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
        CsInt v = (!args.empty() ? args[0].get_int() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.new_command("max", "i1V", [](auto &, auto args, auto &res) {
        CsInt v = (!args.empty() ? args[0].get_int() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::max(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.new_command("minf", "f1V", [](auto &, auto args, auto &res) {
        CsFloat v = (!args.empty() ? args[0].get_float() : 0);
        for (size_t i = 1; i < args.size(); ++i) {
            v = std::min(v, args[i].get_float());
        }
        res.set_float(v);
    });
    cs.new_command("maxf", "f1V", [](auto &, auto args, auto &res) {
        CsFloat v = (!args.empty() ? args[0].get_float() : 0);
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
        CsFloat step = args[1].get_float();
        CsFloat r = args[0].get_float();
        if (step > 0) {
            r += step * ((r < 0) ? -0.5 : 0.5);
            r -= std::fmod(r, step);
        } else {
            r = (r < 0) ? std::ceil(r - 0.5) : std::floor(r + 0.5);
        }
        res.set_float(r);
    });

    cs.new_command("+", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(args, res, 0, std::plus<CsInt>(), CsMathNoop<CsInt>());
    });
    cs.new_command("*", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 1, std::multiplies<CsInt>(), CsMathNoop<CsInt>()
        );
    });
    cs.new_command("-", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, std::minus<CsInt>(), std::negate<CsInt>()
        );
    });

    cs.new_command("^", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, std::bit_xor<CsInt>(), [](CsInt val) { return ~val; }
        );
    });
    cs.new_command("~", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, std::bit_xor<CsInt>(), [](CsInt val) { return ~val; }
        );
    });
    cs.new_command("&", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, std::bit_and<CsInt>(), CsMathNoop<CsInt>()
        );
    });
    cs.new_command("|", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, std::bit_or<CsInt>(), CsMathNoop<CsInt>()
        );
    });

    /* special combined cases */
    cs.new_command("^~", "i1V", [](auto &, auto args, auto &res) {
        CsInt val;
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
        CsInt val;
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
        CsInt val;
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
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                return (val2 < CsInt(sizeof(CsInt) * CHAR_BIT))
                    ? (val1 << std::max(val2, CsInt(0)))
                    : 0;
            }, CsMathNoop<CsInt>()
        );
    });
    cs.new_command(">>", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                return val1 >> std::clamp(
                    val2, CsInt(0), CsInt(sizeof(CsInt) * CHAR_BIT)
                );
            }, CsMathNoop<CsInt>()
        );
    });

    cs.new_command("+f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 0, std::plus<CsFloat>(), CsMathNoop<CsFloat>()
        );
    });
    cs.new_command("*f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 1, std::multiplies<CsFloat>(), CsMathNoop<CsFloat>()
        );
    });
    cs.new_command("-f", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 0, std::minus<CsFloat>(), std::negate<CsFloat>()
        );
    });

    cs.new_command("div", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                if (val2) {
                    return val1 / val2;
                }
                return CsInt(0);
            }, CsMathNoop<CsInt>()
        );
    });
    cs.new_command("mod", "i1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                if (val2) {
                    return val1 % val2;
                }
                return CsInt(0);
            }, CsMathNoop<CsInt>()
        );
    });
    cs.new_command("divf", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                if (val2) {
                    return val1 / val2;
                }
                return CsFloat(0);
            }, CsMathNoop<CsFloat>()
        );
    });
    cs.new_command("modf", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                if (val2) {
                    return CsFloat(fmod(val1, val2));
                }
                return CsFloat(0);
            }, CsMathNoop<CsFloat>()
        );
    });

    cs.new_command("pow", "f1V", [](auto &, auto args, auto &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                return CsFloat(pow(val1, val2));
            }, CsMathNoop<CsFloat>()
        );
    });

    cs.new_command("=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::equal_to<CsInt>());
    });
    cs.new_command("!=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::not_equal_to<CsInt>());
    });
    cs.new_command("<", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::less<CsInt>());
    });
    cs.new_command(">", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::greater<CsInt>());
    });
    cs.new_command("<=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::less_equal<CsInt>());
    });
    cs.new_command(">=", "i1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsInt>(args, res, std::greater_equal<CsInt>());
    });

    cs.new_command("=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::equal_to<CsFloat>());
    });
    cs.new_command("!=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::not_equal_to<CsFloat>());
    });
    cs.new_command("<f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::less<CsFloat>());
    });
    cs.new_command(">f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::greater<CsFloat>());
    });
    cs.new_command("<=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::less_equal<CsFloat>());
    });
    cs.new_command(">=f", "f1V", [](auto &, auto args, auto &res) {
        cs_cmpop<CsFloat>(args, res, std::greater_equal<CsFloat>());
    });
}

} /* namespace cscript */
