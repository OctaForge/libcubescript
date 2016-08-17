#include <limits.h>
#include <math.h>

#include <ostd/functional.hh>

#include "cubescript.hh"

namespace cscript {

static constexpr CsFloat PI = 3.14159265358979f;
static constexpr CsFloat RAD = PI / 180.0f;

template<typename T>
struct CsMathVal;

template<>
struct CsMathVal<CsInt> {
    static CsInt get(TaggedValue &tv) {
        return tv.get_int();
    }
    static void set(TaggedValue &res, CsInt val) {
        res.set_int(val);
    }
};

template<>
struct CsMathVal<CsFloat> {
    static CsFloat get(TaggedValue &tv) {
        return tv.get_float();
    }
    static void set(TaggedValue &res, CsFloat val) {
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
    TvalRange args, TaggedValue &res, T initval,
    F1 binop, F2 unop
) {
    T val;
    if (args.size() >= 2) {
        val = binop(CsMathVal<T>::get(args[0]), CsMathVal<T>::get(args[1]));
        for (ostd::Size i = 2; i < args.size(); ++i) {
            val = binop(val, CsMathVal<T>::get(args[i]));
        }
    } else {
        val = unop(!args.empty() ? CsMathVal<T>::get(args[0]) : initval);
    }
    CsMathVal<T>::set(res, val);
}

template<typename T, typename F>
static inline void cs_cmpop(TvalRange args, TaggedValue &res, F cmp) {
    bool val;
    if (args.size() >= 2) {
        val = cmp(CsMathVal<T>::get(args[0]), CsMathVal<T>::get(args[1]));
        for (ostd::Size i = 2; (i < args.size()) && val; ++i) {
            val = cmp(val, CsMathVal<T>::get(args[i]));
        }
    } else {
        val = cmp(!args.empty() ? CsMathVal<T>::get(args[0]) : T(0), T(0));
    }
    res.set_int(CsInt(val));
}

void cs_init_lib_math(CsState &cs) {
    cs.add_command("sin", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(sin(args[0].get_float() * RAD));
    });
    cs.add_command("cos", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(cos(args[0].get_float() * RAD));
    });
    cs.add_command("tan", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(tan(args[0].get_float() * RAD));
    });

    cs.add_command("asin", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(asin(args[0].get_float()) / RAD);
    });
    cs.add_command("acos", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(acos(args[0].get_float()) / RAD);
    });
    cs.add_command("atan", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(atan(args[0].get_float()) / RAD);
    });
    cs.add_command("atan2", "ff", [](TvalRange args, TaggedValue &res) {
        res.set_float(atan2(args[0].get_float(), args[1].get_float()) / RAD);
    });

    cs.add_command("sqrt", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(sqrt(args[0].get_float()));
    });
    cs.add_command("loge", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(log(args[0].get_float()));
    });
    cs.add_command("log2", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(log(args[0].get_float()) / M_LN2);
    });
    cs.add_command("log10", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(log10(args[0].get_float()));
    });

    cs.add_command("exp", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(exp(args[0].get_float()));
    });

    cs.add_command("min", "i1V", [](TvalRange args, TaggedValue &res) {
        CsInt v = (!args.empty() ? args[0].get_int() : 0);
        for (ostd::Size i = 1; i < args.size(); ++i) {
            v = ostd::min(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.add_command("max", "i1V", [](TvalRange args, TaggedValue &res) {
        CsInt v = (!args.empty() ? args[0].get_int() : 0);
        for (ostd::Size i = 1; i < args.size(); ++i) {
            v = ostd::max(v, args[i].get_int());
        }
        res.set_int(v);
    });
    cs.add_command("minf", "f1V", [](TvalRange args, TaggedValue &res) {
        CsFloat v = (!args.empty() ? args[0].get_float() : 0);
        for (ostd::Size i = 1; i < args.size(); ++i) {
            v = ostd::min(v, args[i].get_float());
        }
        res.set_float(v);
    });
    cs.add_command("maxf", "f1V", [](TvalRange args, TaggedValue &res) {
        CsFloat v = (!args.empty() ? args[0].get_float() : 0);
        for (ostd::Size i = 1; i < args.size(); ++i) {
            v = ostd::max(v, args[i].get_float());
        }
        res.set_float(v);
    });

    cs.add_command("abs", "i", [](TvalRange args, TaggedValue &res) {
        res.set_int(abs(args[0].get_int()));
    });
    cs.add_command("absf", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(fabs(args[0].get_float()));
    });

    cs.add_command("floor", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(floor(args[0].get_float()));
    });
    cs.add_command("ceil", "f", [](TvalRange args, TaggedValue &res) {
        res.set_float(ceil(args[0].get_float()));
    });

    cs.add_command("round", "ff", [](TvalRange args, TaggedValue &res) {
        double step = args[1].get_float();
        double r = args[0].get_float();
        if (step > 0) {
            r += step * ((r < 0) ? -0.5 : 0.5);
            r -= fmod(r, step);
        } else {
            r = (r < 0) ? ceil(r - 0.5) : floor(r + 0.5);
        }
        res.set_float(CsFloat(r));
    });

    cs.add_command("+", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(args, res, 0, ostd::Add<CsInt>(), CsMathNoop<CsInt>());
    });
    cs.add_command("*", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 1, ostd::Multiply<CsInt>(), CsMathNoop<CsInt>()
        );
    });
    cs.add_command("-", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, ostd::Subtract<CsInt>(), ostd::Negate<CsInt>()
        );
    });

    cs.add_command("^", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, ostd::BitXor<CsInt>(), [](CsInt val) { return ~val; }
        );
    });
    cs.add_command("~", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, ostd::BitXor<CsInt>(), [](CsInt val) { return ~val; }
        );
    });
    cs.add_command("&", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, ostd::BitAnd<CsInt>(), CsMathNoop<CsInt>()
        );
    });
    cs.add_command("|", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, ostd::BitOr<CsInt>(), CsMathNoop<CsInt>()
        );
    });

    /* special combined cases */
    cs.add_command("^~", "i1V", [](TvalRange args, TaggedValue &res) {
        CsInt val;
        if (args.size() >= 2) {
            val = args[0].get_int() ^ ~args[1].get_int();
            for (ostd::Size i = 2; i < args.size(); ++i) {
                val ^= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });
    cs.add_command("&~", "i1V", [](TvalRange args, TaggedValue &res) {
        CsInt val;
        if (args.size() >= 2) {
            val = args[0].get_int() & ~args[1].get_int();
            for (ostd::Size i = 2; i < args.size(); ++i) {
                val &= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });
    cs.add_command("|~", "i1V", [](TvalRange args, TaggedValue &res) {
        CsInt val;
        if (args.size() >= 2) {
            val = args[0].get_int() | ~args[1].get_int();
            for (ostd::Size i = 2; i < args.size(); ++i) {
                val |= ~args[i].get_int();
            }
        } else {
            val = !args.empty() ? args[0].get_int() : 0;
        }
        res.set_int(val);
    });

    cs.add_command("<<", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                return (val2 < CsInt(sizeof(CsInt) * CHAR_BIT))
                    ? (val1 << ostd::max(val2, 0))
                    : 0;
            }, CsMathNoop<CsInt>()
        );
    });
    cs.add_command(">>", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                return val1 >> ostd::clamp(
                    val2, 0, CsInt(sizeof(CsInt) * CHAR_BIT)
                );
            }, CsMathNoop<CsInt>()
        );
    });

    cs.add_command("+f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 0, ostd::Add<CsFloat>(), CsMathNoop<CsFloat>()
        );
    });
    cs.add_command("*f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 1, ostd::Multiply<CsFloat>(), CsMathNoop<CsFloat>()
        );
    });
    cs.add_command("-f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 0, ostd::Subtract<CsFloat>(), ostd::Negate<CsFloat>()
        );
    });

    cs.add_command("div", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                if (val2) {
                    return val1 / val2;
                }
                return 0;
            }, CsMathNoop<CsInt>()
        );
    });
    cs.add_command("mod", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsInt>(
            args, res, 0, [](CsInt val1, CsInt val2) {
                if (val2) {
                    return val1 % val2;
                }
                return 0;
            }, CsMathNoop<CsInt>()
        );
    });
    cs.add_command("divf", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                if (val2) {
                    return val1 / val2;
                }
                return CsFloat(0);
            }, CsMathNoop<CsFloat>()
        );
    });
    cs.add_command("modf", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                if (val2) {
                    return CsFloat(fmod(val1, val2));
                }
                return CsFloat(0);
            }, CsMathNoop<CsFloat>()
        );
    });

    cs.add_command("pow", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_mathop<CsFloat>(
            args, res, 0, [](CsFloat val1, CsFloat val2) {
                return CsFloat(pow(val1, val2));
            }, CsMathNoop<CsFloat>()
        );
    });

    cs.add_command("=", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::Equal<CsInt>());
    });
    cs.add_command("!=", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::NotEqual<CsInt>());
    });
    cs.add_command("<", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::Less<CsInt>());
    });
    cs.add_command(">", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::Greater<CsInt>());
    });
    cs.add_command("<=", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::LessEqual<CsInt>());
    });
    cs.add_command(">=", "i1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsInt>(args, res, ostd::GreaterEqual<CsInt>());
    });

    cs.add_command("=f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::Equal<CsFloat>());
    });
    cs.add_command("!=f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::NotEqual<CsFloat>());
    });
    cs.add_command("<f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::Less<CsFloat>());
    });
    cs.add_command(">f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::Greater<CsFloat>());
    });
    cs.add_command("<=f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::LessEqual<CsFloat>());
    });
    cs.add_command(">=f", "f1V", [](TvalRange args, TaggedValue &res) {
        cs_cmpop<CsFloat>(args, res, ostd::GreaterEqual<CsFloat>());
    });
}

} /* namespace cscript */
