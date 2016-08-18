#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript.hh"

#include <stdlib.h>

#include <ostd/array.hh>
#include <ostd/vector.hh>

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

enum {
    ID_UNKNOWN = -1, ID_IVAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS,
    ID_LOCAL, ID_DO, ID_DOARGS, ID_IF, ID_RESULT, ID_NOT, ID_AND, ID_OR
};

struct Command: Ident {
    char *cargs;
    ostd::Uint32 argmask;
    int numargs;
    CmdFunc cb_cftv;

    Command(
        int type, ostd::ConstCharRange name, ostd::ConstCharRange args,
        ostd::Uint32 argmask, int numargs, CmdFunc func
    );
};

enum {
    CODE_START = 0,
    CODE_OFFSET,
    CODE_NULL, CODE_TRUE, CODE_FALSE, CODE_NOT,
    CODE_POP,
    CODE_ENTER, CODE_ENTER_RESULT,
    CODE_EXIT, CODE_RESULT_ARG,
    CODE_VAL, CODE_VALI,
    CODE_DUP,
    CODE_MACRO,
    CODE_BOOL,
    CODE_BLOCK, CODE_EMPTY,
    CODE_COMPILE, CODE_COND,
    CODE_FORCE,
    CODE_RESULT,
    CODE_IDENT, CODE_IDENTU, CODE_IDENTARG,
    CODE_COM, CODE_COMC, CODE_COMV,
    CODE_CONC, CODE_CONCW, CODE_CONCM, CODE_DOWN,
    CODE_SVAR, CODE_SVARM, CODE_SVAR1,
    CODE_IVAR, CODE_IVAR1, CODE_IVAR2, CODE_IVAR3,
    CODE_FVAR, CODE_FVAR1,
    CODE_LOOKUP, CODE_LOOKUPU, CODE_LOOKUPARG,
    CODE_LOOKUPM, CODE_LOOKUPMU, CODE_LOOKUPMARG,
    CODE_ALIAS, CODE_ALIASU, CODE_ALIASARG,
    CODE_CALL, CODE_CALLU, CODE_CALLARG,
    CODE_PRINT,
    CODE_LOCAL,
    CODE_DO, CODE_DOARGS,
    CODE_JUMP, CODE_JUMP_TRUE, CODE_JUMP_FALSE,
    CODE_JUMP_RESULT_TRUE, CODE_JUMP_RESULT_FALSE,

    CODE_OP_MASK = 0x3F,
    CODE_RET = 6,
    CODE_RET_MASK = 0xC0,

    /* return type flags */
    RET_NULL   = VAL_NULL << CODE_RET,
    RET_STR    = VAL_STR << CODE_RET,
    RET_INT    = VAL_INT << CODE_RET,
    RET_FLOAT  = VAL_FLOAT << CODE_RET,
};

struct NullValue: TaggedValue {
    NullValue() { set_null(); }
} const null_value;

template<typename F>
static void cs_do_args(CsState &cs, F body) {
    IdentStack argstack[MaxArguments];
    int argmask1 = cs.stack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            static_cast<Alias *>(cs.identmap[i])->undo_arg(argstack[i]);
        }
    }
    IdentLink *prevstack = cs.stack->next;
    IdentLink aliaslink = {
        cs.stack->id, cs.stack, prevstack->usedargs, prevstack->argstack
    };
    cs.stack = &aliaslink;
    body();
    prevstack->usedargs = aliaslink.usedargs;
    cs.stack = aliaslink.next;
    int argmask2 = cs.stack->usedargs;
    for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
        if (argmask2 & 1) {
            static_cast<Alias *>(cs.identmap[i])->redo_arg(argstack[i]);
        }
    }
}

ostd::ConstCharRange cs_debug_line(
    CsState &cs, ostd::ConstCharRange p, ostd::ConstCharRange fmt,
    ostd::CharRange buf
);

void cs_debug_alias(CsState &cs);

template<typename ...A>
void cs_debug_code(CsState &cs, ostd::ConstCharRange fmt, A &&...args) {
    if (cs.nodebug) {
        return;
    }
    ostd::err.writefln(fmt, ostd::forward<A>(args)...);
    cs_debug_alias(cs);
}

template<typename ...A>
void cs_debug_code_line(
    CsState &cs, ostd::ConstCharRange p, ostd::ConstCharRange fmt, A &&...args
) {
    if (cs.nodebug) {
        return;
    }
    ostd::Array<char, 256> buf;
    ostd::err.writefln(
        cs_debug_line(cs, p, fmt, ostd::CharRange(buf.data(), buf.size())),
        ostd::forward<A>(args)...
    );
    cs_debug_alias(cs);
}

ostd::Uint32 *compilecode(CsState &cs, ostd::ConstCharRange str);

struct GenState {
    CsState &cs;
    ostd::Vector<ostd::Uint32> code;
    char const *source;

    GenState() = delete;
    GenState(CsState &csr): cs(csr), code(), source(nullptr) {}

    void gen_str(ostd::ConstCharRange word, bool macro = false) {
        if (word.size() <= 3 && !macro) {
            ostd::Uint32 op = CODE_VALI | RET_STR;
            for (ostd::Size i = 0; i < word.size(); ++i) {
                op |= ostd::Uint32(ostd::byte(word[i])) << ((i + 1) * 8);
            }
            code.push(op);
            return;
        }
        code.push(
            (macro ? CODE_MACRO : (CODE_VAL | RET_STR)) | (word.size() << 8)
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
        code.push(CODE_VALI | RET_STR);
    }

    void gen_null() {
        code.push(CODE_VALI | RET_NULL);
    }

    void gen_int(CsInt i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push(CODE_VALI | RET_INT | (i << 8));
        } else {
            code.push(CODE_VAL | RET_INT);
            code.push(i);
        }
    }

    void gen_int(ostd::ConstCharRange word);

    void gen_float(CsFloat f = 0.0f) {
        if (CsInt(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push(CODE_VALI | RET_FLOAT | (CsInt(f) << 8));
        } else {
            union {
                CsFloat f;
                ostd::Uint32 u;
            } c;
            c.f = f;
            code.push(CODE_VAL | RET_FLOAT);
            code.push(c.u);
        }
    }

    void gen_float(ostd::ConstCharRange word);

    void gen_ident(Ident *id) {
        code.push(
            ((id->index < MaxArguments)
                ? CODE_IDENTARG
                : CODE_IDENT
            ) | (id->index << 8)
        );
    }

    void gen_ident() {
        gen_ident(cs.dummy);
    }

    void gen_ident(ostd::ConstCharRange word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(
        int wordtype, ostd::ConstCharRange word = ostd::ConstCharRange()
    );

    void gen_main(ostd::ConstCharRange s, int ret_type = VAL_ANY);

    char next_char() {
        return *source++;
    }

    char current() {
        return *source;
    }
};

ostd::String intstr(int v);
ostd::String floatstr(CsFloat v);

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

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_VM_HH */
