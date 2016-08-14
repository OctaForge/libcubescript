#include "cubescript.hh"

#include <math.h>

namespace cscript {

static constexpr float PI = 3.14159265358979f;
static constexpr float RAD = PI / 180.0f;

void cs_init_lib_math(CsState &cs) {
    cs.add_command("sin", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(sin(args[0].get_float() * RAD));
    });
    cs.add_command("cos", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(cos(args[0].get_float() * RAD));
    });
    cs.add_command("tan", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(tan(args[0].get_float() * RAD));
    });

    cs.add_command("asin", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(asin(args[0].get_float()) / RAD);
    });
    cs.add_command("acos", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(acos(args[0].get_float()) / RAD);
    });
    cs.add_command("atan", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(atan(args[0].get_float()) / RAD);
    });
    cs.add_command("atan2", "ff", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(atan2(args[0].get_float(), args[1].get_float()) / RAD);
    });

    cs.add_command("sqrt", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(sqrt(args[0].get_float()));
    });
    cs.add_command("loge", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(log(args[0].get_float()));
    });
    cs.add_command("log2", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(log(args[0].get_float()) / M_LN2);
    });
    cs.add_command("log10", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(log10(args[0].get_float()));
    });

    cs.add_command("exp", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(exp(args[0].get_float()));
    });

#define CS_CMD_MIN_MAX(name, fmt, type, op) \
    cs.add_command(#name, #fmt "1V", [&cs](TvalRange args, TaggedValue &res) { \
        type v = !args.empty() ? args[0].fmt : 0; \
        for (ostd::Size i = 1; i < args.size(); ++i) v = op(v, args[i].fmt); \
        res.set_##type(v); \
    })

    CS_CMD_MIN_MAX(min, i, int, ostd::min);
    CS_CMD_MIN_MAX(max, i, int, ostd::max);
    CS_CMD_MIN_MAX(minf, f, float, ostd::min);
    CS_CMD_MIN_MAX(maxf, f, float, ostd::max);

#undef CS_CMD_MIN_MAX

    cs.add_command("abs", "i", [&cs](TvalRange args, TaggedValue &res) {
        res.set_int(abs(args[0].get_int()));
    });
    cs.add_command("absf", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(fabs(args[0].get_float()));
    });

    cs.add_command("floor", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(floor(args[0].get_float()));
    });
    cs.add_command("ceil", "f", [&cs](TvalRange args, TaggedValue &res) {
        res.set_float(ceil(args[0].get_float()));
    });

    cs.add_command("round", "ff", [&cs](TvalRange args, TaggedValue &res) {
        double step = args[1].get_float();
        double r = args[0].get_float();
        if (step > 0) {
            r += step * ((r < 0) ? -0.5 : 0.5);
            r -= fmod(r, step);
        } else {
            r = (r < 0) ? ceil(r - 0.5) : floor(r + 0.5);
        }
        res.set_float(float(r));
    });

#define CS_CMD_MATH(name, fmt, type, op, initval, unaryop) \
    cs.add_command(name, #fmt "1V", [&cs](TvalRange args, TaggedValue &res) { \
        type val; \
        if (args.size() >= 2) { \
            val = args[0].fmt; \
            type val2 = args[1].fmt; \
            op; \
            for (ostd::Size i = 2; i < args.size(); ++i) { \
                val2 = args[i].fmt; \
                op; \
            } \
        } else { \
            val = (args.size() > 0) ? args[0].fmt : initval; \
            unaryop; \
        } \
        res.set_##type(val); \
    });

#define CS_CMD_MATHIN(name, op, initval, unaryop) \
    CS_CMD_MATH(#name, i, int, val = val op val2, initval, unaryop)

#define CS_CMD_MATHI(name, initval, unaryop) \
    CS_CMD_MATHIN(name, name, initval, unaryop)

#define CS_CMD_MATHFN(name, op, initval, unaryop) \
    CS_CMD_MATH(#name "f", f, float, val = val op val2, initval, unaryop)

#define CS_CMD_MATHF(name, initval, unaryop) \
    CS_CMD_MATHFN(name, name, initval, unaryop)

    CS_CMD_MATHI(+, 0, {});
    CS_CMD_MATHI(*, 1, {});
    CS_CMD_MATHI(-, 0, val = -val);

    CS_CMD_MATHI(^, 0, val = ~val);
    CS_CMD_MATHIN(~, ^, 0, val = ~val);
    CS_CMD_MATHI(&, 0, {});
    CS_CMD_MATHI(|, 0, {});
    CS_CMD_MATHI(^~, 0, {});
    CS_CMD_MATHI(&~, 0, {});
    CS_CMD_MATHI(|~, 0, {});

    CS_CMD_MATH("<<", i, int, {
        val = (val2 < 32) ? (val << ostd::max(val2, 0)) : 0;
    }, 0, {});
    CS_CMD_MATH(">>", i, int, val >>= ostd::clamp(val2, 0, 31), 0, {});

    CS_CMD_MATHF(+, 0, {});
    CS_CMD_MATHF(*, 1, {});
    CS_CMD_MATHF(-, 0, val = -val);

#define CS_CMD_DIV(name, fmt, type, op) \
    CS_CMD_MATH(#name, fmt, type, { if (val2) op; else val = 0; }, 0, {})

    CS_CMD_DIV(div, i, int, val /= val2);
    CS_CMD_DIV(mod, i, int, val %= val2);
    CS_CMD_DIV(divf, f, float, val /= val2);
    CS_CMD_DIV(modf, f, float, val = fmod(val, val2));

#undef CS_CMD_DIV

    CS_CMD_MATH("pow", f, float, val = pow(val, val2), 0, {});

#undef CS_CMD_MATHF
#undef CS_CMD_MATHFN
#undef CS_CMD_MATHI
#undef CS_CMD_MATHIN
#undef CS_CMD_MATH

#define CS_CMD_CMP(name, fmt, type, op) \
    cs.add_command(name, #fmt "1V", [&cs](TvalRange args, TaggedValue &res) { \
        bool val; \
        if (args.size() >= 2) { \
            val = args[0].fmt op args[1].fmt; \
            for (ostd::Size i = 2; i < args.size() && val; ++i) \
                val = args[i-1].fmt op args[i].fmt; \
        } else \
            val = ((args.size() > 0) ? args[0].fmt : 0) op 0; \
        res.set_int(int(val)); \
    })

#define CS_CMD_CMPIN(name, op) CS_CMD_CMP(#name, i, int, op)
#define CS_CMD_CMPI(name) CS_CMD_CMPIN(name, name)
#define CS_CMD_CMPFN(name, op) CS_CMD_CMP(#name "f", f, float, op)
#define CS_CMD_CMPF(name) CS_CMD_CMPFN(name, name)

    CS_CMD_CMPIN(=, ==);
    CS_CMD_CMPI(!=);
    CS_CMD_CMPI(<);
    CS_CMD_CMPI(>);
    CS_CMD_CMPI(<=);
    CS_CMD_CMPI(>=);

    CS_CMD_CMPFN(=, ==);
    CS_CMD_CMPF(!=);
    CS_CMD_CMPF(<);
    CS_CMD_CMPF(>);
    CS_CMD_CMPF(<=);
    CS_CMD_CMPF(>=);

#undef CS_CMD_CMPF
#undef CS_CMD_CMPFN
#undef CS_CMD_CMPI
#undef CS_CMD_CMPIN
#undef CS_CMD_CMP
}

} /* namespace cscript */
