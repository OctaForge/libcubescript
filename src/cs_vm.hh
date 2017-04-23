#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript/cubescript.hh"

#include <cstdlib>
#include <array>
#include <vector>

#include "cs_util.hh"

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

static constexpr int DummyIdx = MaxArguments;
static constexpr int NumargsIdx = MaxArguments + 1;
static constexpr int DbgaliasIdx = MaxArguments + 2;

enum {
    CsIdUnknown = -1, CsIdIvar, CsIdFvar, CsIdSvar, CsIdCommand, CsIdAlias,
    CsIdLocal, CsIdDo, CsIdDoArgs, CsIdIf, CsIdBreak, CsIdContinue, CsIdResult,
    CsIdNot, CsIdAnd, CsIdOr
};

struct cs_identLink {
    cs_ident *id;
    cs_identLink *next;
    int usedargs;
    cs_ident_stack *argstack;
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

static inline int cs_vtype_to_int(cs_value_type v) {
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
    CsCodeJump, CsCodeJumpB, CsCodeJumpResult,
    CsCodeBreak,

    CsCodeOpMask = 0x3F,
    CsCodeRet = 6,
    CsCodeRetMask = 0xC0,

    /* return type flags */
    CsRetNull   = CsValNull << CsCodeRet,
    CsRetString = CsValString << CsCodeRet,
    CsRetInt    = CsValInt << CsCodeRet,
    CsRetFloat  = CsValFloat << CsCodeRet,

    /* CsCodeJumpB, CsCodeJumpResult */
    CsCodeFlagTrue = 1 << CsCodeRet,
    CsCodeFlagFalse = 0 << CsCodeRet
};

struct cs_shared_state {
    CsMap<ostd::string_range, cs_ident *> idents;
    CsVector<cs_ident *> identmap;
    cs_alloc_cb allocf;
    void *aptr;

    void *alloc(void *ptr, size_t os, size_t ns) {
        return allocf(aptr, ptr, os, ns);
    }

    template<typename T, typename ...A>
    T *create(A &&...args) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, sizeof(T)));
        new (ret) T(std::forward<A>(args)...);
        return ret;
    }

    template<typename T>
    T *create_array(size_t len) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, len * sizeof(T)));
        for (size_t i = 0; i < len; ++i) {
            new (&ret[i]) T();
        }
        return ret;
    }

    template<typename T>
    void destroy(T *v) noexcept {
        v->~T();
        alloc(v, sizeof(T), 0);
    }

    template<typename T>
    void destroy_array(T *v, size_t len) noexcept {
        v->~T();
        alloc(v, len * sizeof(T), 0);
    }
};

struct CsBreakException {
};

struct CsContinueException {
};

template<typename T>
constexpr size_t CsTypeStorageSize =
    (sizeof(T) - 1) / sizeof(uint32_t) + 1;

struct cs_gen_state {
    cs_state &cs;
    cs_gen_state *prevps;
    bool parsing = true;
    CsVector<uint32_t> code;
    ostd::string_range source;
    size_t current_line;
    ostd::string_range src_name;

    cs_gen_state() = delete;
    cs_gen_state(cs_state &csr):
        cs(csr), prevps(csr.p_pstate), code(),
        source(nullptr), current_line(1), src_name()
    {
        csr.p_pstate = this;
    }

    ~cs_gen_state() {
        done();
    }

    void done() {
        if (!parsing) {
            return;
        }
        cs.p_pstate = prevps;
        parsing = false;
    }

    ostd::string_range get_str();
    cs_string get_str_dup(bool unescape = true);

    ostd::string_range get_word();

    void gen_str(ostd::string_range word, bool macro = false) {
        if (word.size() <= 3 && !macro) {
            uint32_t op = CsCodeValInt | CsRetString;
            for (size_t i = 0; i < word.size(); ++i) {
                op |= uint32_t(
                    static_cast<unsigned char>(word[i])
                ) << ((i + 1) * 8);
            }
            code.push_back(op);
            return;
        }
        code.push_back(
            (macro ? CsCodeMacro : (CsCodeVal | CsRetString)) | (word.size() << 8)
        );
        auto it = reinterpret_cast<uint32_t const *>(word.data());
        code.insert(
            code.end(), it, it + (word.size() / sizeof(uint32_t))
        );
        size_t esz = word.size() % sizeof(uint32_t);
        union {
            char c[sizeof(uint32_t)];
            uint32_t u;
        } end;
        end.u = 0;
        memcpy(end.c, word.data() + word.size() - esz, esz);
        code.push_back(end.u);
    }

    void gen_str() {
        code.push_back(CsCodeValInt | CsRetString);
    }

    void gen_null() {
        code.push_back(CsCodeValInt | CsRetNull);
    }

    void gen_int(cs_int i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push_back(CsCodeValInt | CsRetInt | (i << 8));
        } else {
            union {
                cs_int i;
                uint32_t u[CsTypeStorageSize<cs_int>];
            } c;
            c.i = i;
            code.push_back(CsCodeVal | CsRetInt);
            code.insert(code.end(), c.u, c.u + CsTypeStorageSize<cs_int>);
        }
    }

    void gen_int(ostd::string_range word);

    void gen_float(cs_float f = 0.0f) {
        if (cs_int(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push_back(CsCodeValInt | CsRetFloat | (cs_int(f) << 8));
        } else {
            union {
                cs_float f;
                uint32_t u[CsTypeStorageSize<cs_float>];
            } c;
            c.f = f;
            code.push_back(CsCodeVal | CsRetFloat);
            code.insert(code.end(), c.u, c.u + CsTypeStorageSize<cs_float>);
        }
    }

    void gen_float(ostd::string_range word);

    void gen_ident(cs_ident *id) {
        code.push_back(
            ((id->get_index() < MaxArguments)
                ? CsCodeIdentArg
                : CsCodeIdent
            ) | (id->get_index() << 8)
        );
    }

    void gen_ident() {
        gen_ident(cs.p_state->identmap[DummyIdx]);
    }

    void gen_ident(ostd::string_range word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(
        int wordtype, ostd::string_range word = ostd::string_range(),
        int line = 0
    );

    void gen_main(ostd::string_range s, int ret_type = CsValAny);

    void next_char() {
        if (source.empty()) {
            return;
        }
        if (*source == '\n') {
            ++current_line;
        }
        source.pop_front();
    }

    char current(size_t ahead = 0) {
        if (source.size() <= ahead) {
            return '\0';
        }
        return source[ahead];
    }

    ostd::string_range read_macro_name();

    char skip_until(ostd::string_range chars);
    char skip_until(char cf);

    void skip_comments();
};

cs_string intstr(cs_int v);
cs_string floatstr(cs_float v);

bool cs_check_num(ostd::string_range s);

static inline void bcode_incr(uint32_t *bc) {
    *bc += 0x100;
}

static inline void bcode_decr(uint32_t *bc) {
    *bc -= 0x100;
    if (std::int32_t(*bc) < 0x100) {
        delete[] bc;
    }
}

static inline bool cs_is_arg_used(cs_state &cs, cs_ident *id) {
    if (!cs.p_callstack) {
        return true;
    }
    return cs.p_callstack->usedargs & (1 << id->get_index());
}

struct cs_aliasInternal {
    static void push_arg(
        cs_alias *a, cs_value &v, cs_ident_stack &st, bool um = true
    ) {
        if (a->p_astack == &st) {
            /* prevent cycles and unnecessary code elsewhere */
            a->p_val = std::move(v);
            clean_code(a);
            return;
        }
        st.val_s = std::move(a->p_val);
        st.next = a->p_astack;
        a->p_astack = &st;
        a->p_val = std::move(v);
        clean_code(a);
        if (um) {
            a->p_flags &= ~CS_IDF_UNKNOWN;
        }
    }

    static void pop_arg(cs_alias *a) {
        if (!a->p_astack) {
            return;
        }
        cs_ident_stack *st = a->p_astack;
        a->p_val = std::move(a->p_astack->val_s);
        clean_code(a);
        a->p_astack = st->next;
    }

    static void undo_arg(cs_alias *a, cs_ident_stack &st) {
        cs_ident_stack *prev = a->p_astack;
        st.val_s = std::move(a->p_val);
        st.next = prev;
        a->p_astack = prev->next;
        a->p_val = std::move(prev->val_s);
        clean_code(a);
    }

    static void redo_arg(cs_alias *a, cs_ident_stack &st) {
        cs_ident_stack *prev = st.next;
        prev->val_s = std::move(a->p_val);
        a->p_astack = prev;
        a->p_val = std::move(st.val_s);
        clean_code(a);
    }

    static void set_arg(cs_alias *a, cs_state &cs, cs_value &v) {
        if (cs_is_arg_used(cs, a)) {
            a->p_val = std::move(v);
            clean_code(a);
        } else {
            push_arg(a, v, cs.p_callstack->argstack[a->get_index()], false);
            cs.p_callstack->usedargs |= 1 << a->get_index();
        }
    }

    static void set_alias(cs_alias *a, cs_state &cs, cs_value &v) {
        a->p_val = std::move(v);
        clean_code(a);
        a->p_flags = (a->p_flags & cs.identflags) | cs.identflags;
    }

    static void clean_code(cs_alias *a) {
        uint32_t *bcode = reinterpret_cast<uint32_t *>(a->p_acode);
        if (bcode) {
            bcode_decr(bcode);
            a->p_acode = nullptr;
        }
    }

    static cs_bcode *compile_code(cs_alias *a, cs_state &cs) {
        if (!a->p_acode) {
            cs_gen_state gs(cs);
            gs.code.reserve(64);
            gs.gen_main(a->get_value().get_str());
            /* i wish i could steal the memory somehow */
            uint32_t *code = new uint32_t[gs.code.size()];
            memcpy(code, gs.code.data(), gs.code.size() * sizeof(uint32_t));
            bcode_incr(code);
            a->p_acode = reinterpret_cast<cs_bcode *>(code);
        }
        return a->p_acode;
    }
};

template<typename F>
static void cs_do_args(cs_state &cs, F body) {
    if (!cs.p_callstack) {
        body();
        return;
    }
    cs_ident_stack argstack[MaxArguments];
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            cs_aliasInternal::undo_arg(
                static_cast<cs_alias *>(cs.p_state->identmap[i]), argstack[i]
            );
        }
    }
    cs_identLink *prevstack = cs.p_callstack->next;
    cs_identLink aliaslink = {
        cs.p_callstack->id, cs.p_callstack,
        prevstack ? prevstack->usedargs : ((1 << MaxArguments) - 1),
        prevstack ? prevstack->argstack : nullptr
    };
    cs.p_callstack = &aliaslink;
    cs_do_and_cleanup(std::move(body), [&]() {
        if (prevstack) {
            prevstack->usedargs = aliaslink.usedargs;
        }
        cs.p_callstack = aliaslink.next;
        int argmask2 = cs.p_callstack->usedargs;
        for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
            if (argmask2 & 1) {
                cs_aliasInternal::redo_arg(
                    static_cast<cs_alias *>(cs.p_state->identmap[i]), argstack[i]
                );
            }
        }
    });
}

cs_bcode *cs_copy_code(cs_bcode *c);

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_VM_HH */
