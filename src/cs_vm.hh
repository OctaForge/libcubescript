#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript/cubescript.hh"

#include <cstdlib>
#include <array>
#include <vector>
#include <type_traits>

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

struct cs_ident_link {
    cs_ident *id;
    cs_ident_link *next;
    int usedargs;
    cs_ident_stack *argstack;
};

template<typename T, std::size_t N>
struct cs_valarray {
    cs_valarray(cs_state &cs) {
        for (std::size_t i = 0; i < N; ++i) {
            new (&stor[i]) T{cs};
        }
    }

    ~cs_valarray() {
        for (std::size_t i = 0; i < N; ++i) {
            reinterpret_cast<T *>(&stor[i])->~T();
        }
    }

    T &operator[](std::size_t i) {
        return *reinterpret_cast<T *>(&stor[i]);
    }

    std::aligned_storage_t<sizeof(T), alignof(T)> stor[N];
};

enum {
    CS_VAL_NULL = 0, CS_VAL_INT, CS_VAL_FLOAT, CS_VAL_STRING,
    CS_VAL_ANY, CS_VAL_CODE, CS_VAL_IDENT, CS_VAL_WORD,
    CS_VAL_POP, CS_VAL_COND
};

static const int cs_valtypet[] = {
    CS_VAL_NULL, CS_VAL_INT, CS_VAL_FLOAT, CS_VAL_STRING,
    CS_VAL_CODE, CS_VAL_IDENT
};

static inline int cs_vtype_to_int(cs_value_type v) {
    return cs_valtypet[int(v)];
}

/* instruction: uint32 [length 24][retflag 2][opcode 6] */
enum {
    CS_CODE_START = 0,
    CS_CODE_OFFSET,
    CS_CODE_NULL, CS_CODE_TRUE, CS_CODE_FALSE, CS_CODE_NOT,
    CS_CODE_POP,
    CS_CODE_ENTER, CS_CODE_ENTER_RESULT,
    CS_CODE_EXIT, CS_CODE_RESULT_ARG,
    CS_CODE_VAL, CS_CODE_VAL_INT,
    CS_CODE_DUP,
    CS_CODE_BOOL,
    CS_CODE_BLOCK, CS_CODE_EMPTY,
    CS_CODE_COMPILE, CS_CODE_COND,
    CS_CODE_FORCE,
    CS_CODE_RESULT,
    CS_CODE_IDENT, CS_CODE_IDENT_U, CS_CODE_IDENT_ARG,
    CS_CODE_COM, CS_CODE_COM_C, CS_CODE_COM_V,
    CS_CODE_CONC, CS_CODE_CONC_W, CS_CODE_CONC_M,
    CS_CODE_SVAR, CS_CODE_SVAR1,
    CS_CODE_IVAR, CS_CODE_IVAR1, CS_CODE_IVAR2, CS_CODE_IVAR3,
    CS_CODE_FVAR, CS_CODE_FVAR1,
    CS_CODE_LOOKUP, CS_CODE_LOOKUP_U, CS_CODE_LOOKUP_ARG,
    CS_CODE_LOOKUP_M, CS_CODE_LOOKUP_MU, CS_CODE_LOOKUP_MARG,
    CS_CODE_ALIAS, CS_CODE_ALIAS_U, CS_CODE_ALIAS_ARG,
    CS_CODE_CALL, CS_CODE_CALL_U, CS_CODE_CALL_ARG,
    CS_CODE_PRINT,
    CS_CODE_LOCAL,
    CS_CODE_DO, CS_CODE_DO_ARGS,
    CS_CODE_JUMP, CS_CODE_JUMP_B, CS_CODE_JUMP_RESULT,
    CS_CODE_BREAK,

    CS_CODE_OP_MASK = 0x3F,
    CS_CODE_RET = 6,
    CS_CODE_RET_MASK = 0xC0,

    /* return type flags */
    CS_RET_NULL   = CS_VAL_NULL << CS_CODE_RET,
    CS_RET_STRING = CS_VAL_STRING << CS_CODE_RET,
    CS_RET_INT    = CS_VAL_INT << CS_CODE_RET,
    CS_RET_FLOAT  = CS_VAL_FLOAT << CS_CODE_RET,

    /* CS_CODE_JUMP_B, CS_CODE_JUMP_RESULT */
    CS_CODE_FLAG_TRUE = 1 << CS_CODE_RET,
    CS_CODE_FLAG_FALSE = 0 << CS_CODE_RET
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
    cs_valbuf<uint32_t> code;
    char const *source, *send;
    size_t current_line;
    std::string_view src_name;

    cs_gen_state() = delete;
    cs_gen_state(cs_state &csr):
        cs{csr}, prevps{csr.p_pstate}, code{cs},
        source{}, send{}, current_line{1}, src_name{}
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

    std::string_view get_str();
    cs_charbuf get_str_dup();

    std::string_view get_word();

    void gen_str(std::string_view word) {
        if (word.size() <= 3) {
            uint32_t op = CS_CODE_VAL_INT | CS_RET_STRING;
            for (size_t i = 0; i < word.size(); ++i) {
                op |= uint32_t(
                    static_cast<unsigned char>(word[i])
                ) << ((i + 1) * 8);
            }
            code.push_back(op);
            return;
        }
        code.push_back(CS_CODE_VAL | CS_RET_STRING | (word.size() << 8));
        auto it = reinterpret_cast<uint32_t const *>(word.data());
        code.append(it, it + (word.size() / sizeof(uint32_t)));
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
        code.push_back(CS_CODE_VAL_INT | CS_RET_STRING);
    }

    void gen_null() {
        code.push_back(CS_CODE_VAL_INT | CS_RET_NULL);
    }

    void gen_int(cs_int i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push_back(CS_CODE_VAL_INT | CS_RET_INT | (i << 8));
        } else {
            union {
                cs_int i;
                uint32_t u[CsTypeStorageSize<cs_int>];
            } c;
            c.i = i;
            code.push_back(CS_CODE_VAL | CS_RET_INT);
            code.append(c.u, c.u + CsTypeStorageSize<cs_int>);
        }
    }

    void gen_int(std::string_view word);

    void gen_float(cs_float f = 0.0f) {
        if (cs_int(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push_back(CS_CODE_VAL_INT | CS_RET_FLOAT | (cs_int(f) << 8));
        } else {
            union {
                cs_float f;
                uint32_t u[CsTypeStorageSize<cs_float>];
            } c;
            c.f = f;
            code.push_back(CS_CODE_VAL | CS_RET_FLOAT);
            code.append(c.u, c.u + CsTypeStorageSize<cs_float>);
        }
    }

    void gen_float(std::string_view word);

    void gen_ident(cs_ident *id) {
        code.push_back(
            ((id->get_index() < MaxArguments)
                ? CS_CODE_IDENT_ARG
                : CS_CODE_IDENT
            ) | (id->get_index() << 8)
        );
    }

    void gen_ident() {
        gen_ident(cs.p_state->identmap[DummyIdx]);
    }

    void gen_ident(std::string_view word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(
        int wordtype, std::string_view word = std::string_view(),
        int line = 0
    );

    void gen_main(std::string_view s, int ret_type = CS_VAL_ANY);

    void next_char() {
        if (source == send) {
            return;
        }
        if (*source == '\n') {
            ++current_line;
        }
        ++source;
    }

    char current(size_t ahead = 0) {
        if (std::size_t(send - source) <= ahead) {
            return '\0';
        }
        return source[ahead];
    }

    std::string_view read_macro_name();

    char skip_until(std::string_view chars);
    char skip_until(char cf);

    void skip_comments();
};

bool cs_check_num(std::string_view s);

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

struct cs_alias_internal {
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
    cs_valarray<cs_ident_stack, MaxArguments> argstack{cs};
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            cs_alias_internal::undo_arg(
                static_cast<cs_alias *>(cs.p_state->identmap[i]), argstack[i]
            );
        }
    }
    cs_ident_link *prevstack = cs.p_callstack->next;
    cs_ident_link aliaslink = {
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
                cs_alias_internal::redo_arg(
                    static_cast<cs_alias *>(cs.p_state->identmap[i]), argstack[i]
                );
            }
        }
    });
}

cs_bcode *cs_copy_code(cs_bcode *c);

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_VM_HH */
