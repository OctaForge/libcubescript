#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript/cubescript.hh"

#include <stdlib.h>

#include <ostd/array.hh>
#include <ostd/vector.hh>

#include "cs_util.hh"

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

static constexpr int DummyIdx = MaxArguments;
static constexpr int NumargsIdx = MaxArguments + 1;
static constexpr int DbgaliasIdx = MaxArguments + 2;

enum {
    CsIdUnknown = -1, CsIdIvar, CsIdFvar, CsIdSvar, CsIdCommand, CsIdAlias,
    CsIdLocal, CsIdDo, CsIdDoArgs, CsIdIf, CsIdResult, CsIdNot, CsIdAnd, CsIdOr
};

struct CsIdentLink {
    CsIdent *id;
    CsIdentLink *next;
    int usedargs;
    CsIdentStack *argstack;
};

enum {
    CsValNull = 0, CsValInt, CsValFloat, CsValString,
    CsValAny, CsValCode, CsValMacro, CsValIdent, CsValCstring,
    CsValCany, CsValWord, CsValPop, CsValCond
};

static const int cs_valtypet[] = {
    CsValNull, CsValInt, CsValFloat, CsValString,
    CsValCstring, CsValCode, CsValMacro, CsValIdent
};

static inline int cs_vtype_to_int(CsValueType v) {
    return cs_valtypet[int(v)];
}

/* instruction: uint32 [length 24][retflag 2][opcode 6] */
enum {
    CsCodeStart = 0,
    CsCodeOffset,
    CsCodeNull, CsCodeTrue, CsCodeFalse, CsCodeNot,
    CsCodePop,
    CsCodeEnter, CsCodeEnterResult,
    CsCodeExit, CsCodeResultArg,
    CsCodeVal, CsCodeValInt,
    CsCodeDup,
    CsCodeMacro,
    CsCodeBool,
    CsCodeBlock, CsCodeEmpty,
    CsCodeCompile, CsCodeCond,
    CsCodeForce,
    CsCodeResult,
    CsCodeIdent, CsCodeIdentU, CsCodeIdentArg,
    CsCodeCom, CsCodeComC, CsCodeComV,
    CsCodeConc, CsCodeConcW, CsCodeConcM,
    CsCodeSvar, CsCodeSvarM, CsCodeSvar1,
    CsCodeIvar, CsCodeIvar1, CsCodeIvar2, CsCodeIvar3,
    CsCodeFvar, CsCodeFvar1,
    CsCodeLookup, CsCodeLookupU, CsCodeLookupArg,
    CsCodeLookupM, CsCodeLookupMu, CsCodeLookupMarg,
    CsCodeAlias, CsCodeAliasU, CsCodeAliasArg,
    CsCodeCall, CsCodeCallU, CsCodeCallArg,
    CsCodePrint,
    CsCodeLocal,
    CsCodeDo, CsCodeDoArgs,
    CsCodeJump, CsCodeJumpTrue, CsCodeJumpFalse,
    CsCodeJumpResultTrue, CsCodeJumpResultFalse,

    CsCodeOpMask = 0x3F,
    CsCodeRet = 6,
    CsCodeRetMask = 0xC0,

    /* return type flags */
    CsRetNull   = CsValNull << CsCodeRet,
    CsRetString = CsValString << CsCodeRet,
    CsRetInt    = CsValInt << CsCodeRet,
    CsRetFloat  = CsValFloat << CsCodeRet,
};

struct CsSharedState {
    CsMap<ostd::ConstCharRange, CsIdent *> idents;
    CsVector<CsIdent *> identmap;
};

template<typename T>
constexpr ostd::Size CsTypeStorageSize =
    (sizeof(T) - 1) / sizeof(ostd::Uint32) + 1;

struct CsErrorException {
    ostd::ConstCharRange errmsg;
    CsStackState stack;
    CsErrorException() = delete;
    CsErrorException(CsErrorException const &) = delete;
    CsErrorException(CsErrorException &&v):
        errmsg(v.errmsg), stack(ostd::move(v.stack))
    {}
    CsErrorException(ostd::ConstCharRange v, CsStackState &&st):
        errmsg(v), stack(ostd::move(st))
    {}
};

CsStackState cs_save_stack(CsState &cs);

template<typename ...A>
void cs_debug_code(CsState &cs, ostd::ConstCharRange fmt, A &&...args) {
    cs.get_err().writefln(fmt, ostd::forward<A>(args)...);
    auto st = cs_save_stack(cs);
    cscript::util::print_stack(cs.get_err().iter(), st);
}

struct GenState {
    CsState &cs;
    CsVector<ostd::Uint32> code;
    char const *source;
    ostd::ConstCharRange src_file, src_str;

    GenState() = delete;
    GenState(CsState &csr):
        cs(csr), code(), source(nullptr), src_file(), src_str()
    {}

    void gen_str(ostd::ConstCharRange word, bool macro = false) {
        if (word.size() <= 3 && !macro) {
            ostd::Uint32 op = CsCodeValInt | CsRetString;
            for (ostd::Size i = 0; i < word.size(); ++i) {
                op |= ostd::Uint32(ostd::byte(word[i])) << ((i + 1) * 8);
            }
            code.push(op);
            return;
        }
        code.push(
            (macro ? CsCodeMacro : (CsCodeVal | CsRetString)) | (word.size() << 8)
        );
        code.push_n(
            reinterpret_cast<ostd::Uint32 const *>(word.data()),
            word.size() / sizeof(ostd::Uint32)
        );
        ostd::Size esz = word.size() % sizeof(ostd::Uint32);
        union {
            char c[sizeof(ostd::Uint32)];
            ostd::Uint32 u;
        } end;
        end.u = 0;
        memcpy(end.c, word.data() + word.size() - esz, esz);
        code.push(end.u);
    }

    void gen_str() {
        code.push(CsCodeValInt | CsRetString);
    }

    void gen_null() {
        code.push(CsCodeValInt | CsRetNull);
    }

    void gen_int(CsInt i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push(CsCodeValInt | CsRetInt | (i << 8));
        } else {
            union {
                CsInt i;
                ostd::Uint32 u[CsTypeStorageSize<CsInt>];
            } c;
            c.i = i;
            code.push(CsCodeVal | CsRetInt);
            code.push_n(c.u, CsTypeStorageSize<CsInt>);
        }
    }

    void gen_int(ostd::ConstCharRange word);

    void gen_float(CsFloat f = 0.0f) {
        if (CsInt(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push(CsCodeValInt | CsRetFloat | (CsInt(f) << 8));
        } else {
            union {
                CsFloat f;
                ostd::Uint32 u[CsTypeStorageSize<CsFloat>];
            } c;
            c.f = f;
            code.push(CsCodeVal | CsRetFloat);
            code.push_n(c.u, CsTypeStorageSize<CsFloat>);
        }
    }

    void gen_float(ostd::ConstCharRange word);

    void gen_ident(CsIdent *id) {
        code.push(
            ((id->get_index() < MaxArguments)
                ? CsCodeIdentArg
                : CsCodeIdent
            ) | (id->get_index() << 8)
        );
    }

    void gen_ident() {
        gen_ident(cs.p_state->identmap[DummyIdx]);
    }

    void gen_ident(ostd::ConstCharRange word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(
        int wordtype, ostd::ConstCharRange word = ostd::ConstCharRange()
    );

    void gen_main(ostd::ConstCharRange s, int ret_type = CsValAny);

    char next_char() {
        return *source++;
    }

    char current() {
        return *source;
    }
};

CsString intstr(CsInt v);
CsString floatstr(CsFloat v);

bool cs_check_num(ostd::ConstCharRange s);

static inline void bcode_incr(ostd::Uint32 *bc) {
    *bc += 0x100;
}

static inline void bcode_decr(ostd::Uint32 *bc) {
    *bc -= 0x100;
    if (ostd::Int32(*bc) < 0x100) {
        delete[] bc;
    }
}

static inline bool cs_is_arg_used(CsState &cs, CsIdent *id) {
    if (!cs.p_callstack) {
        return true;
    }
    return cs.p_callstack->usedargs & (1 << id->get_index());
}

struct CsAliasInternal {
    static void push_arg(
        CsAlias *a, CsValue &v, CsIdentStack &st, bool um = true
    ) {
        if (a->p_astack == &st) {
            /* prevent cycles and unnecessary code elsewhere */
            a->p_val = ostd::move(v);
            clean_code(a);
            return;
        }
        st.val_s = ostd::move(a->p_val);
        st.next = a->p_astack;
        a->p_astack = &st;
        a->p_val = ostd::move(v);
        clean_code(a);
        if (um) {
            a->p_flags &= ~CsIdfUnknown;
        }
    }

    static void pop_arg(CsAlias *a) {
        if (!a->p_astack) {
            return;
        }
        CsIdentStack *st = a->p_astack;
        a->p_val = ostd::move(a->p_astack->val_s);
        clean_code(a);
        a->p_astack = st->next;
    }

    static void undo_arg(CsAlias *a, CsIdentStack &st) {
        CsIdentStack *prev = a->p_astack;
        st.val_s = ostd::move(a->p_val);
        st.next = prev;
        a->p_astack = prev->next;
        a->p_val = ostd::move(prev->val_s);
        clean_code(a);
    }

    static void redo_arg(CsAlias *a, CsIdentStack &st) {
        CsIdentStack *prev = st.next;
        prev->val_s = ostd::move(a->p_val);
        a->p_astack = prev;
        a->p_val = ostd::move(st.val_s);
        clean_code(a);
    }

    static void set_arg(CsAlias *a, CsState &cs, CsValue &v) {
        if (cs_is_arg_used(cs, a)) {
            a->p_val = ostd::move(v);
            clean_code(a);
        } else {
            push_arg(a, v, cs.p_callstack->argstack[a->get_index()], false);
            cs.p_callstack->usedargs |= 1 << a->get_index();
        }
    }

    static void set_alias(CsAlias *a, CsState &cs, CsValue &v) {
        a->p_val = ostd::move(v);
        clean_code(a);
        a->p_flags = (a->p_flags & cs.identflags) | cs.identflags;
    }

    static void clean_code(CsAlias *a) {
        ostd::Uint32 *bcode = reinterpret_cast<ostd::Uint32 *>(a->p_acode);
        if (bcode) {
            bcode_decr(bcode);
            a->p_acode = nullptr;
        }
    }

    static CsBytecode *compile_code(CsAlias *a, CsState &cs) {
        if (!a->p_acode) {
            GenState gs(cs);
            gs.code.reserve(64);
            gs.gen_main(a->get_value().get_str());
            ostd::Uint32 *code = new ostd::Uint32[gs.code.size()];
            memcpy(code, gs.code.data(), gs.code.size() * sizeof(ostd::Uint32));
            bcode_incr(code);
            a->p_acode = reinterpret_cast<CsBytecode *>(code);
        }
        return a->p_acode;
    }
};

template<typename F>
static void cs_do_args(CsState &cs, F body) {
    if (!cs.p_callstack) {
        body();
        return;
    }
    CsIdentStack argstack[MaxArguments];
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            CsAliasInternal::undo_arg(
                static_cast<CsAlias *>(cs.p_state->identmap[i]), argstack[i]
            );
        }
    }
    CsIdentLink *prevstack = cs.p_callstack->next;
    CsIdentLink aliaslink = {
        cs.p_callstack->id, cs.p_callstack, prevstack->usedargs, prevstack->argstack
    };
    cs.p_callstack = &aliaslink;
    cs_do_and_cleanup(ostd::move(body), [&]() {
        prevstack->usedargs = aliaslink.usedargs;
        cs.p_callstack = aliaslink.next;
        int argmask2 = cs.p_callstack->usedargs;
        for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
            if (argmask2 & 1) {
                CsAliasInternal::redo_arg(
                    static_cast<CsAlias *>(cs.p_state->identmap[i]), argstack[i]
                );
            }
        }
    });
}

CsBytecode *cs_copy_code(CsBytecode *c);

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_VM_HH */
