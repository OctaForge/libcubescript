#include "cubescript.hh"
#include "cs_vm.hh"

#include <limits.h>

namespace cscript {

ostd::Uint32 const *forcecode(CsState &cs, TaggedValue &v);
void forcecond(CsState &cs, TaggedValue &v);

static ostd::Uint32 emptyblock[VAL_ANY][2] = {
    { CODE_START + 0x100, CODE_EXIT | RET_NULL },
    { CODE_START + 0x100, CODE_EXIT | RET_INT },
    { CODE_START + 0x100, CODE_EXIT | RET_FLOAT },
    { CODE_START + 0x100, CODE_EXIT | RET_STR }
};

static TaggedValue no_ret = null_value;

static inline void force_arg(TaggedValue &v, int type) {
    switch (type) {
    case RET_STR:
        if (v.get_type() != VAL_STR) v.force_str();
        break;
    case RET_INT:
        if (v.get_type() != VAL_INT) v.force_int();
        break;
    case RET_FLOAT:
        if (v.get_type() != VAL_FLOAT) v.force_float();
        break;
    }
}

static inline void free_args(TaggedValue *args, int &oldnum, int newnum) {
    for (int i = newnum; i < oldnum; i++) args[i].cleanup();
    oldnum = newnum;
}

static ostd::Uint32 const *skipcode(ostd::Uint32 const *code, TaggedValue *result = nullptr) {
    int depth = 0;
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
        case CODE_MACRO:
        case CODE_VAL|RET_STR: {
            ostd::Uint32 len = op >> 8;
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }
        case CODE_BLOCK:
        case CODE_JUMP:
        case CODE_JUMP_TRUE:
        case CODE_JUMP_FALSE:
        case CODE_JUMP_RESULT_TRUE:
        case CODE_JUMP_RESULT_FALSE: {
            ostd::Uint32 len = op >> 8;
            code += len;
            continue;
        }
        case CODE_ENTER:
        case CODE_ENTER_RESULT:
            ++depth;
            continue;
        case CODE_EXIT|RET_NULL:
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            if (depth <= 0) {
                if (result) {
                    force_arg(*result, op & CODE_RET_MASK);
                }
                return code;
            }
            --depth;
            continue;
        }
    }
}

void TaggedValue::copy_arg(TaggedValue &r) const {
    r.cleanup();
    switch (get_type()) {
        case VAL_INT:
        case VAL_FLOAT:
        case VAL_IDENT:
            r = *this;
            break;
        case VAL_STR:
        case VAL_CSTR:
        case VAL_MACRO:
            r.set_str(ostd::ConstCharRange(s, len));
            break;
        case VAL_CODE: {
            ostd::Uint32 const *bcode = reinterpret_cast<ostd::Uint32 const *>(code);
            ostd::Uint32 const *end = skipcode(bcode);
            ostd::Uint32 *dst = new ostd::Uint32[end - bcode + 1];
            *dst++ = CODE_START;
            memcpy(dst, bcode, (end - bcode) * sizeof(ostd::Uint32));
            r.set_code(reinterpret_cast<Bytecode const *>(dst));
            break;
        }
        default:
            r.set_null();
            break;
    }
}

static inline void callcommand(CsState &cs, Ident *id, TaggedValue *args, TaggedValue &res, int numargs, bool lookup = false) {
    int i = -1, fakeargs = 0;
    bool rep = false;
    for (char const *fmt = id->cargs; *fmt; fmt++) switch (*fmt) {
        case 'i':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(0);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'b':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_int(INT_MIN);
                fakeargs++;
            } else args[i].force_int();
            break;
        case 'f':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(0.0f);
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'F':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_float(args[i - 1].get_float());
                fakeargs++;
            } else args[i].force_float();
            break;
        case 'S':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_str("");
                fakeargs++;
            } else args[i].force_str();
            break;
        case 's':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_cstr("");
                fakeargs++;
            } else args[i].force_str();
            break;
        case 'T':
        case 't':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            }
            break;
        case 'E':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_null();
                fakeargs++;
            } else forcecond(cs, args[i]);
            break;
        case 'e':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_NULL] + 1));
                fakeargs++;
            } else forcecode(cs, args[i]);
            break;
        case 'r':
            if (++i >= numargs) {
                if (rep) break;
                args[i].set_ident(cs.dummy);
                fakeargs++;
            } else cs.force_ident(args[i]);
            break;
        case '$':
            if (++i < numargs) args[i].cleanup();
            args[i].set_ident(id);
            break;
        case 'N':
            if (++i < numargs) args[i].cleanup();
            args[i].set_int(lookup ? -1 : i - fakeargs);
            break;
        case 'C': {
            i = ostd::max(i + 1, numargs);
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(args, i), " ");
            TaggedValue tv;
            tv.set_mstr(buf.get().iter());
            id->cb_cftv(TvalRange(&tv, 1), res);
            goto cleanup;
        }
        case 'V':
            i = ostd::max(i + 1, numargs);
            id->cb_cftv(ostd::iter(args, i), res);
            goto cleanup;
        case '1':
        case '2':
        case '3':
        case '4':
            if (i + 1 < numargs) {
                fmt -= *fmt - '0' + 1;
                rep = true;
            }
            break;
        }
    ++i;
    id->cb_cftv(TvalRange(args, i), res);
cleanup:
    for (ostd::Size k = 0; k < ostd::Size(i); ++k) args[k].cleanup();
    for (; i < numargs; i++) args[i].cleanup();
}

static constexpr int MaxRunDepth = 255;
static thread_local int rundepth = 0;

static ostd::Uint32 const *runcode(CsState &cs, ostd::Uint32 const *code, TaggedValue &result) {
    result.set_null();
    if (rundepth >= MaxRunDepth) {
        cs_debug_code(cs, "exceeded recursion limit");
        return skipcode(code, (&result == &no_ret) ? nullptr : &result);
    }
    ++rundepth;
    int numargs = 0;
    TaggedValue args[MaxArguments + MaxResults];
    for (;;) {
        ostd::Uint32 op = *code++;
        switch (op & 0xFF) {
        case CODE_START:
        case CODE_OFFSET:
            continue;

#define RETOP(op, val) \
                case op: \
                    result.cleanup(); \
                    val; \
                    continue;

            RETOP(CODE_NULL | RET_NULL, result.set_null())
            RETOP(CODE_NULL | RET_STR, result.set_str(""))
            RETOP(CODE_NULL | RET_INT, result.set_int(0))
            RETOP(CODE_NULL | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_FALSE | RET_STR, result.set_str("0"))
        case CODE_FALSE|RET_NULL:
            RETOP(CODE_FALSE | RET_INT, result.set_int(0))
            RETOP(CODE_FALSE | RET_FLOAT, result.set_float(0.0f))

            RETOP(CODE_TRUE | RET_STR, result.set_str("1"))
        case CODE_TRUE|RET_NULL:
            RETOP(CODE_TRUE | RET_INT, result.set_int(1))
            RETOP(CODE_TRUE | RET_FLOAT, result.set_float(1.0f))

#define RETPOP(op, val) \
                RETOP(op, { --numargs; val; args[numargs].cleanup(); })

            RETPOP(CODE_NOT | RET_STR, result.set_str(args[numargs].get_bool() ? "0" : "1"))
        case CODE_NOT|RET_NULL:
                RETPOP(CODE_NOT | RET_INT, result.set_int(args[numargs].get_bool() ? 0 : 1))
                RETPOP(CODE_NOT | RET_FLOAT, result.set_float(args[numargs].get_bool() ? 0.0f : 1.0f))

            case CODE_POP:
                    args[--numargs].cleanup();
            continue;
        case CODE_ENTER:
            code = runcode(cs, code, args[numargs++]);
            continue;
        case CODE_ENTER_RESULT:
            result.cleanup();
            code = runcode(cs, code, result);
            continue;
        case CODE_EXIT|RET_STR:
        case CODE_EXIT|RET_INT:
        case CODE_EXIT|RET_FLOAT:
            force_arg(result, op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_EXIT|RET_NULL:
            goto exit;
        case CODE_RESULT_ARG|RET_STR:
        case CODE_RESULT_ARG|RET_INT:
        case CODE_RESULT_ARG|RET_FLOAT:
            force_arg(result, op & CODE_RET_MASK);
        /* fallthrough */
        case CODE_RESULT_ARG|RET_NULL:
            args[numargs++] = result;
            result.set_null();
            continue;
        case CODE_PRINT:
            cs.print_var(cs.identmap[op >> 8]);
            continue;

        case CODE_LOCAL: {
            result.cleanup();
            int numlocals = op >> 8, offset = numargs - numlocals;
            IdentStack locals[MaxArguments];
            for (int i = 0; i < numlocals; ++i) args[offset + i].id->push_alias(locals[i]);
            code = runcode(cs, code, result);
            for (int i = offset; i < numargs; i++) args[i].id->pop_alias();
            goto exit;
        }

        case CODE_DOARGS|RET_NULL:
        case CODE_DOARGS|RET_STR:
        case CODE_DOARGS|RET_INT:
        case CODE_DOARGS|RET_FLOAT:
            if (cs.stack != &cs.noalias) {
                cs_do_args(cs, [&]() {
                    result.cleanup();
                    cs.run_ret(args[--numargs].code, result);
                    args[numargs].cleanup();
                    force_arg(result, op & CODE_RET_MASK);
                });
                continue;
            }
        /* fallthrough */
        case CODE_DO|RET_NULL:
        case CODE_DO|RET_STR:
        case CODE_DO|RET_INT:
        case CODE_DO|RET_FLOAT:
            result.cleanup();
            cs.run_ret(args[--numargs].code, result);
            args[numargs].cleanup();
            force_arg(result, op & CODE_RET_MASK);
            continue;

        case CODE_JUMP: {
            ostd::Uint32 len = op >> 8;
            code += len;
            continue;
        }
        case CODE_JUMP_TRUE: {
            ostd::Uint32 len = op >> 8;
            if (args[--numargs].get_bool()) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_FALSE: {
            ostd::Uint32 len = op >> 8;
            if (!args[--numargs].get_bool()) code += len;
            args[numargs].cleanup();
            continue;
        }
        case CODE_JUMP_RESULT_TRUE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                cs.run_ret(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (result.get_bool()) code += len;
            continue;
        }
        case CODE_JUMP_RESULT_FALSE: {
            ostd::Uint32 len = op >> 8;
            result.cleanup();
            --numargs;
            if (args[numargs].get_type() == VAL_CODE) {
                cs.run_ret(args[numargs].code, result);
                args[numargs].cleanup();
            } else result = args[numargs];
            if (!result.get_bool()) code += len;
            continue;
        }

        case CODE_MACRO: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_macro(reinterpret_cast<Bytecode const *>(code), len);
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }

        case CODE_VAL|RET_STR: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_str(ostd::ConstCharRange(reinterpret_cast<char const *>(code), len));
            code += len / sizeof(ostd::Uint32) + 1;
            continue;
        }
        case CODE_VALI|RET_STR: {
            char s[4] = { char((op >> 8) & 0xFF), char((op >> 16) & 0xFF), char((op >> 24) & 0xFF), '\0' };
            args[numargs++].set_str(s);
            continue;
        }
        case CODE_VAL|RET_NULL:
        case CODE_VALI|RET_NULL:
            args[numargs++].set_null();
            continue;
        case CODE_VAL|RET_INT:
            args[numargs++].set_int(int(*code++));
            continue;
        case CODE_VALI|RET_INT:
            args[numargs++].set_int(int(op) >> 8);
            continue;
        case CODE_VAL|RET_FLOAT:
            args[numargs++].set_float(*reinterpret_cast<float const *>(code++));
            continue;
        case CODE_VALI|RET_FLOAT:
            args[numargs++].set_float(float(int(op) >> 8));
            continue;

        case CODE_DUP|RET_NULL:
            args[numargs - 1].get_val(args[numargs]);
            numargs++;
            continue;
        case CODE_DUP|RET_INT:
            args[numargs].set_int(args[numargs - 1].get_int());
            numargs++;
            continue;
        case CODE_DUP|RET_FLOAT:
            args[numargs].set_float(args[numargs - 1].get_float());
            numargs++;
            continue;
        case CODE_DUP|RET_STR:
            args[numargs].set_str(ostd::move(args[numargs - 1].get_str()));
            numargs++;
            continue;

        case CODE_FORCE|RET_STR:
            args[numargs - 1].force_str();
            continue;
        case CODE_FORCE|RET_INT:
            args[numargs - 1].force_int();
            continue;
        case CODE_FORCE|RET_FLOAT:
            args[numargs - 1].force_float();
            continue;

        case CODE_RESULT|RET_NULL:
            result.cleanup();
            result = args[--numargs];
            continue;
        case CODE_RESULT|RET_STR:
        case CODE_RESULT|RET_INT:
        case CODE_RESULT|RET_FLOAT:
            result.cleanup();
            result = args[--numargs];
            force_arg(result, op & CODE_RET_MASK);
            continue;

        case CODE_EMPTY|RET_NULL:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_NULL] + 1));
            break;
        case CODE_EMPTY|RET_STR:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_STR] + 1));
            break;
        case CODE_EMPTY|RET_INT:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_INT] + 1));
            break;
        case CODE_EMPTY|RET_FLOAT:
            args[numargs++].set_code(reinterpret_cast<Bytecode *>(emptyblock[VAL_FLOAT] + 1));
            break;
        case CODE_BLOCK: {
            ostd::Uint32 len = op >> 8;
            args[numargs++].set_code(reinterpret_cast<Bytecode const *>(code + 1));
            code += len;
            continue;
        }
        case CODE_COMPILE: {
            TaggedValue &arg = args[numargs - 1];
            GenState gs(cs);
            switch (arg.get_type()) {
            case VAL_INT:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_int(arg.i);
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            case VAL_FLOAT:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_float(arg.f);
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                gs.code.reserve(64);
                gs.gen_main(arg.s);
                arg.cleanup();
                break;
            default:
                gs.code.reserve(8);
                gs.code.push(CODE_START);
                gs.gen_null();
                gs.code.push(CODE_RESULT);
                gs.code.push(CODE_EXIT);
                break;
            }
            arg.set_code(reinterpret_cast<Bytecode const *>(gs.code.disown() + 1));
            continue;
        }
        case CODE_COND: {
            TaggedValue &arg = args[numargs - 1];
            switch (arg.get_type()) {
            case VAL_STR:
            case VAL_MACRO:
            case VAL_CSTR:
                if (arg.s[0]) {
                    GenState gs(cs);
                    gs.code.reserve(64);
                    gs.gen_main(arg.s);
                    arg.cleanup();
                    arg.set_code(reinterpret_cast<Bytecode const *>(gs.code.disown() + 1));
                } else arg.force_null();
                break;
            }
            continue;
        }

        case CODE_IDENT:
            args[numargs++].set_ident(cs.identmap[op >> 8]);
            continue;
        case CODE_IDENTARG: {
            Ident *id = cs.identmap[op >> 8];
            if (!(cs.stack->usedargs & (1 << id->index))) {
                id->push_arg(null_value, cs.stack->argstack[id->index], false);
                cs.stack->usedargs |= 1 << id->index;
            }
            args[numargs++].set_ident(id);
            continue;
        }
        case CODE_IDENTU: {
            TaggedValue &arg = args[numargs - 1];
            Ident *id = arg.get_type() == VAL_STR || arg.get_type() == VAL_MACRO || arg.get_type() == VAL_CSTR ? cs.new_ident(ostd::ConstCharRange(arg.cstr, arg.len)) : cs.dummy;
            if (id->index < MaxArguments && !(cs.stack->usedargs & (1 << id->index))) {
                id->push_arg(null_value, cs.stack->argstack[id->index], false);
                cs.stack->usedargs |= 1 << id->index;
            }
            arg.cleanup();
            arg.set_ident(id);
            continue;
        }

        case CODE_LOOKUPU|RET_STR:
#define LOOKUPU(aval, sval, ival, fval, nval) { \
                    TaggedValue &arg = args[numargs-1]; \
                    if(arg.get_type() != VAL_STR && arg.get_type() != VAL_MACRO && arg.get_type() != VAL_CSTR) continue; \
                    Ident *id = cs.idents.at(arg.s); \
                    if(id) switch(id->type) \
                    { \
                        case ID_ALIAS: \
                            if(id->flags&IDF_UNKNOWN) break; \
                            arg.cleanup(); \
                            if(id->index < MaxArguments && !(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                            aval; \
                            continue; \
                        case ID_SVAR: arg.cleanup(); sval; continue; \
                        case ID_VAR: arg.cleanup(); ival; continue; \
                        case ID_FVAR: arg.cleanup(); fval; continue; \
                        case ID_COMMAND: \
                        { \
                            arg.cleanup(); \
                            arg.set_null(); \
                            TaggedValue buf[MaxArguments]; \
                            callcommand(cs, id, buf, arg, 0, true); \
                            force_arg(arg, op&CODE_RET_MASK); \
                            continue; \
                        } \
                        default: arg.cleanup(); nval; continue; \
                    } \
                    cs_debug_code(cs, "unknown alias lookup: %s", arg.s); \
                    arg.cleanup(); \
                    nval; \
                    continue; \
                }
            LOOKUPU(arg.set_str(ostd::move(id->get_str())),
                    arg.set_str(*id->storage.sp),
                    arg.set_str(ostd::move(intstr(*id->storage.ip))),
                    arg.set_str(ostd::move(floatstr(*id->storage.fp))),
                    arg.set_str(""));
        case CODE_LOOKUP|RET_STR:
#define LOOKUP(aval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(id->flags&IDF_UNKNOWN) cs_debug_code(cs, "unknown alias lookup: %s", id->name); \
                    aval; \
                    continue; \
                }
            LOOKUP(args[numargs++].set_str(ostd::move(id->get_str())));
        case CODE_LOOKUPARG|RET_STR:
#define LOOKUPARG(aval, nval) { \
                    Ident *id = cs.identmap[op>>8]; \
                    if(!(cs.stack->usedargs&(1<<id->index))) { nval; continue; } \
                    aval; \
                    continue; \
                }
            LOOKUPARG(args[numargs++].set_str(ostd::move(id->get_str())), args[numargs++].set_str(""));
        case CODE_LOOKUPU|RET_INT:
            LOOKUPU(arg.set_int(id->get_int()),
                    arg.set_int(parseint(*id->storage.sp)),
                    arg.set_int(*id->storage.ip),
                    arg.set_int(int(*id->storage.fp)),
                    arg.set_int(0));
        case CODE_LOOKUP|RET_INT:
            LOOKUP(args[numargs++].set_int(id->get_int()));
        case CODE_LOOKUPARG|RET_INT:
            LOOKUPARG(args[numargs++].set_int(id->get_int()), args[numargs++].set_int(0));
        case CODE_LOOKUPU|RET_FLOAT:
            LOOKUPU(arg.set_float(id->get_float()),
                    arg.set_float(parsefloat(*id->storage.sp)),
                    arg.set_float(float(*id->storage.ip)),
                    arg.set_float(*id->storage.fp),
                    arg.set_float(0.0f));
        case CODE_LOOKUP|RET_FLOAT:
            LOOKUP(args[numargs++].set_float(id->get_float()));
        case CODE_LOOKUPARG|RET_FLOAT:
            LOOKUPARG(args[numargs++].set_float(id->get_float()), args[numargs++].set_float(0.0f));
        case CODE_LOOKUPU|RET_NULL:
            LOOKUPU(id->get_val(arg),
                    arg.set_str(*id->storage.sp),
                    arg.set_int(*id->storage.ip),
                    arg.set_float(*id->storage.fp),
                    arg.set_null());
        case CODE_LOOKUP|RET_NULL:
            LOOKUP(id->get_val(args[numargs++]));
        case CODE_LOOKUPARG|RET_NULL:
            LOOKUPARG(id->get_val(args[numargs++]), args[numargs++].set_null());

        case CODE_LOOKUPMU|RET_STR:
            LOOKUPU(id->get_cstr(arg),
                    arg.set_cstr(*id->storage.sp),
                    arg.set_str(ostd::move(intstr(*id->storage.ip))),
                    arg.set_str(ostd::move(floatstr(*id->storage.fp))),
                    arg.set_cstr(""));
        case CODE_LOOKUPM|RET_STR:
            LOOKUP(id->get_cstr(args[numargs++]));
        case CODE_LOOKUPMARG|RET_STR:
            LOOKUPARG(id->get_cstr(args[numargs++]), args[numargs++].set_cstr(""));
        case CODE_LOOKUPMU|RET_NULL:
            LOOKUPU(id->get_cval(arg),
                    arg.set_cstr(*id->storage.sp),
                    arg.set_int(*id->storage.ip),
                    arg.set_float(*id->storage.fp),
                    arg.set_null());
        case CODE_LOOKUPM|RET_NULL:
            LOOKUP(id->get_cval(args[numargs++]));
        case CODE_LOOKUPMARG|RET_NULL:
            LOOKUPARG(id->get_cval(args[numargs++]), args[numargs++].set_null());

        case CODE_SVAR|RET_STR:
        case CODE_SVAR|RET_NULL:
            args[numargs++].set_str(*cs.identmap[op >> 8]->storage.sp);
            continue;
        case CODE_SVAR|RET_INT:
            args[numargs++].set_int(parseint(*cs.identmap[op >> 8]->storage.sp));
            continue;
        case CODE_SVAR|RET_FLOAT:
            args[numargs++].set_float(parsefloat(*cs.identmap[op >> 8]->storage.sp));
            continue;
        case CODE_SVARM:
            args[numargs++].set_cstr(*cs.identmap[op >> 8]->storage.sp);
            continue;
        case CODE_SVAR1:
            cs.set_var_str_checked(cs.identmap[op >> 8], args[--numargs].s);
            args[numargs].cleanup();
            continue;

        case CODE_IVAR|RET_INT:
        case CODE_IVAR|RET_NULL:
            args[numargs++].set_int(*cs.identmap[op >> 8]->storage.ip);
            continue;
        case CODE_IVAR|RET_STR:
            args[numargs++].set_str(ostd::move(intstr(*cs.identmap[op >> 8]->storage.ip)));
            continue;
        case CODE_IVAR|RET_FLOAT:
            args[numargs++].set_float(float(*cs.identmap[op >> 8]->storage.ip));
            continue;
        case CODE_IVAR1:
            cs.set_var_int_checked(cs.identmap[op >> 8], args[--numargs].i);
            continue;
        case CODE_IVAR2:
            numargs -= 2;
            cs.set_var_int_checked(cs.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8));
            continue;
        case CODE_IVAR3:
            numargs -= 3;
            cs.set_var_int_checked(cs.identmap[op >> 8], (args[numargs].i << 16) | (args[numargs + 1].i << 8) | args[numargs + 2].i);
            continue;

        case CODE_FVAR|RET_FLOAT:
        case CODE_FVAR|RET_NULL:
            args[numargs++].set_float(*cs.identmap[op >> 8]->storage.fp);
            continue;
        case CODE_FVAR|RET_STR:
            args[numargs++].set_str(ostd::move(floatstr(*cs.identmap[op >> 8]->storage.fp)));
            continue;
        case CODE_FVAR|RET_INT:
            args[numargs++].set_int(int(*cs.identmap[op >> 8]->storage.fp));
            continue;
        case CODE_FVAR1:
            cs.set_var_float_checked(cs.identmap[op >> 8], args[--numargs].f);
            continue;

        case CODE_COM|RET_NULL:
        case CODE_COM|RET_STR:
        case CODE_COM|RET_FLOAT:
        case CODE_COM|RET_INT: {
            Ident *id = cs.identmap[op >> 8];
            int offset = numargs - id->numargs;
            result.force_null();
            id->cb_cftv(TvalRange(args + offset, id->numargs), result);
            force_arg(result, op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
            }

        case CODE_COMV|RET_NULL:
        case CODE_COMV|RET_STR:
        case CODE_COMV|RET_FLOAT:
        case CODE_COMV|RET_INT: {
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            id->cb_cftv(ostd::iter(&args[offset], callargs), result);
            force_arg(result, op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }
        case CODE_COMC|RET_NULL:
        case CODE_COMC|RET_STR:
        case CODE_COMC|RET_FLOAT:
        case CODE_COMC|RET_INT: {
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            result.force_null();
            {
                auto buf = ostd::appender<ostd::String>();
                cscript::util::tvals_concat(buf, ostd::iter(&args[offset], callargs), " ");
                TaggedValue tv;
                tv.set_mstr(buf.get().iter());
                id->cb_cftv(TvalRange(&tv, 1), result);
            }
            force_arg(result, op & CODE_RET_MASK);
            free_args(args, numargs, offset);
            continue;
        }

        case CODE_CONC|RET_NULL:
        case CODE_CONC|RET_STR:
        case CODE_CONC|RET_FLOAT:
        case CODE_CONC|RET_INT:
        case CODE_CONCW|RET_NULL:
        case CODE_CONCW|RET_STR:
        case CODE_CONCW|RET_FLOAT:
        case CODE_CONCW|RET_INT: {
            int numconc = op >> 8;
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(&args[numargs - numconc], numconc), ((op & CODE_OP_MASK) == CODE_CONC) ? " " : "");
            free_args(args, numargs, numargs - numconc);
            args[numargs].set_mstr(buf.get().iter());
            buf.get().disown();
            force_arg(args[numargs], op & CODE_RET_MASK);
            numargs++;
            continue;
        }

        case CODE_CONCM|RET_NULL:
        case CODE_CONCM|RET_STR:
        case CODE_CONCM|RET_FLOAT:
        case CODE_CONCM|RET_INT: {
            int numconc = op >> 8;
            auto buf = ostd::appender<ostd::String>();
            cscript::util::tvals_concat(buf, ostd::iter(&args[numargs - numconc], numconc));
            free_args(args, numargs, numargs - numconc);
            result.set_mstr(buf.get().iter());
            buf.get().disown();
            force_arg(result, op & CODE_RET_MASK);
            continue;
        }

        case CODE_ALIAS:
            cs.identmap[op >> 8]->set_alias(cs, args[--numargs]);
            continue;
        case CODE_ALIASARG:
            cs.identmap[op >> 8]->set_arg(cs, args[--numargs]);
            continue;
        case CODE_ALIASU:
            numargs -= 2;
            cs.set_alias(args[numargs].get_str(), args[numargs + 1]);
            args[numargs].cleanup();
            continue;

#define SKIPARGS(offset) offset
        case CODE_CALL|RET_NULL:
        case CODE_CALL|RET_STR:
        case CODE_CALL|RET_FLOAT:
        case CODE_CALL|RET_INT: {
#define FORCERESULT { \
                free_args(args, numargs, SKIPARGS(offset)); \
                force_arg(result, op&CODE_RET_MASK); \
                continue; \
            }
#define CALLALIAS(cs, result) { \
                IdentStack argstack[MaxArguments]; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->push_arg(args[offset + i], argstack[i], false); \
                int oldargs = (cs).numargs; \
                (cs).numargs = callargs; \
                int oldflags = (cs).identflags; \
                (cs).identflags |= id->flags&IDF_OVERRIDDEN; \
                IdentLink aliaslink = { id, (cs).stack, (1<<callargs)-1, argstack }; \
                (cs).stack = &aliaslink; \
                if(!id->code) id->code = reinterpret_cast<Bytecode *>(compilecode(cs, id->get_str())); \
                ostd::Uint32 *codep = reinterpret_cast<ostd::Uint32 *>(id->code); \
                codep[0] += 0x100; \
                runcode((cs), codep+1, (result)); \
                codep[0] -= 0x100; \
                if(int(codep[0]) < 0x100) delete[] codep; \
                (cs).stack = aliaslink.next; \
                (cs).identflags = oldflags; \
                for(int i = 0; i < callargs; i++) \
                    (cs).identmap[i]->pop_arg(); \
                for(int argmask = aliaslink.usedargs&(~0<<callargs), i = callargs; argmask; i++) \
                    if(argmask&(1<<i)) { (cs).identmap[i]->pop_arg(); argmask &= ~(1<<i); } \
                force_arg(result, op&CODE_RET_MASK); \
                (cs).numargs = oldargs; \
                numargs = SKIPARGS(offset); \
            }
            result.force_null();
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (id->flags & IDF_UNKNOWN) {
                cs_debug_code(cs, "unknown command: %s", id->name);
                FORCERESULT;
            }
            CALLALIAS(cs, result);
            continue;
        }
        case CODE_CALLARG|RET_NULL:
        case CODE_CALLARG|RET_STR:
        case CODE_CALLARG|RET_FLOAT:
        case CODE_CALLARG|RET_INT: {
            result.force_null();
            Ident *id = cs.identmap[op >> 13];
            int callargs = (op >> 8) & 0x1F, offset = numargs - callargs;
            if (!(cs.stack->usedargs & (1 << id->index))) FORCERESULT;
            CALLALIAS(cs, result);
            continue;
        }
#undef SKIPARGS

#define SKIPARGS(offset) offset-1
        case CODE_CALLU|RET_NULL:
        case CODE_CALLU|RET_STR:
        case CODE_CALLU|RET_FLOAT:
        case CODE_CALLU|RET_INT: {
            int callargs = op >> 8, offset = numargs - callargs;
            TaggedValue &idarg = args[offset - 1];
            if (idarg.get_type() != VAL_STR && idarg.get_type() != VAL_MACRO && idarg.get_type() != VAL_CSTR) {
litval:
                result.cleanup();
                result = idarg;
                force_arg(result, op & CODE_RET_MASK);
                while (--numargs >= offset) args[numargs].cleanup();
                continue;
            }
            Ident *id = cs.idents.at(idarg.s);
            if (!id) {
noid:
                if (cs_check_num(idarg.s)) goto litval;
                cs_debug_code(cs, "unknown command: %s", idarg.s);
                result.force_null();
                FORCERESULT;
            }
            result.force_null();
            switch (id->type) {
            default:
                if (!id->cb_cftv) FORCERESULT;
            /* fallthrough */
            case ID_COMMAND:
                idarg.cleanup();
                callcommand(cs, id, &args[offset], result, callargs);
                force_arg(result, op & CODE_RET_MASK);
                numargs = offset - 1;
                continue;
            case ID_LOCAL: {
                IdentStack locals[MaxArguments];
                idarg.cleanup();
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) cs.force_ident(args[offset + j])->push_alias(locals[j]);
                code = runcode(cs, code, result);
                for (ostd::Size j = 0; j < ostd::Size(callargs); ++j) args[offset + j].id->pop_alias();
                goto exit;
            }
            case ID_VAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_int_checked(id, ostd::iter(&args[offset], callargs));
                FORCERESULT;
            case ID_FVAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_float_checked(id, args[offset].force_float());
                FORCERESULT;
            case ID_SVAR:
                if (callargs <= 0) cs.print_var(id);
                else cs.set_var_str_checked(id, args[offset].force_str());
                FORCERESULT;
            case ID_ALIAS:
                if (id->index < MaxArguments && !(cs.stack->usedargs & (1 << id->index))) FORCERESULT;
                if (id->get_valtype() == VAL_NULL) goto noid;
                idarg.cleanup();
                CALLALIAS(cs, result);
                continue;
            }
        }
#undef SKIPARGS
        }
    }
exit:
    --rundepth;
    return code;
}

void CsState::run_ret(Bytecode const *code, TaggedValue &ret) {
    runcode(*this, reinterpret_cast<ostd::Uint32 const *>(code), ret);
}

void CsState::run_ret(ostd::ConstCharRange code, TaggedValue &ret) {
    GenState gs(*this);
    gs.code.reserve(64);
    /* FIXME range */
    gs.gen_main(code.data(), VAL_ANY);
    runcode(*this, gs.code.data() + 1, ret);
    if (int(gs.code[0]) >= 0x100)
        gs.code.disown();
}

/* TODO */
void CsState::run_ret(Ident *id, TvalRange args, TaggedValue &ret) {
    int nargs = int(args.size());
    ret.set_null();
    ++rundepth;
    if (rundepth > MaxRunDepth) cs_debug_code(*this, "exceeded recursion limit");
    else if (id) switch (id->type) {
        default:
            if (!id->cb_cftv) break;
        /* fallthrough */
        case ID_COMMAND:
            if (nargs < id->numargs) {
                TaggedValue buf[MaxArguments];
                memcpy(buf, args.data(), args.size() * sizeof(TaggedValue));
                callcommand(*this, id, buf, ret, nargs, false);
            } else callcommand(*this, id, args.data(), ret, nargs, false);
            nargs = 0;
            break;
        case ID_VAR:
            if (args.empty()) print_var(id);
            else set_var_int_checked(id, args);
            break;
        case ID_FVAR:
            if (args.empty()) print_var(id);
            else set_var_float_checked(id, args[0].force_float());
            break;
        case ID_SVAR:
            if (args.empty()) print_var(id);
            else set_var_str_checked(id, args[0].force_str());
            break;
        case ID_ALIAS:
            if (id->index < MaxArguments && !(stack->usedargs & (1 << id->index))) break;
            if (id->get_valtype() == VAL_NULL) break;
#define callargs nargs
#define offset 0
#define op RET_NULL
#define SKIPARGS(offset) offset
            CALLALIAS(*this, ret);
#undef callargs
#undef offset
#undef op
#undef SKIPARGS
            break;
        }
    free_args(args.data(), nargs, 0);
    --rundepth;
}

ostd::String CsState::run_str(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

ostd::String CsState::run_str(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

ostd::String CsState::run_str(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    ostd::String s = ret.get_str();
    ret.cleanup();
    return s;
}

int CsState::run_int(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

int CsState::run_int(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

int CsState::run_int(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    int i = ret.get_int();
    ret.cleanup();
    return i;
}

float CsState::run_float(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

float CsState::run_float(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

float CsState::run_float(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    float f = ret.get_float();
    ret.cleanup();
    return f;
}

bool CsState::run_bool(Bytecode const *code) {
    TaggedValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(ostd::ConstCharRange code) {
    TaggedValue ret;
    run_ret(code, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_bool(Ident *id, TvalRange args) {
    TaggedValue ret;
    run_ret(id, args, ret);
    bool b = ret.get_bool();
    ret.cleanup();
    return b;
}

bool CsState::run_file(ostd::ConstCharRange fname) {
    ostd::ConstCharRange oldsrcfile = src_file, oldsrcstr = src_str;
    char *buf = nullptr;
    ostd::Size len;

    ostd::FileStream f(fname, ostd::StreamMode::read);
    if (!f.is_open())
        return false;

    len = f.size();
    buf = new char[len + 1];
    if (f.get(buf, len) != len) {
        delete[] buf;
        return false;
    }
    buf[len] = '\0';

    src_file = fname;
    src_str = ostd::ConstCharRange(buf, len);
    run_int(buf);
    src_file = oldsrcfile;
    src_str = oldsrcstr;
    delete[] buf;
    return true;
}

} /* namespace cscript */
