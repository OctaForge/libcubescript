#ifndef CS_PRIVATE_HH
#define CS_PRIVATE_HH

#include "cubescript.hh"

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;
static constexpr int MaxComargs = 12;

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
    CODE_COM, CODE_COMD, CODE_COMC, CODE_COMV,
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

template<typename F>
static void cs_do_args(CsState &cs, F body) {
    IdentStack argstack[MaxArguments];
    int argmask1 = cs.stack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) if(argmask1 & 1)
        cs.identmap[i]->undo_arg(argstack[i]);
    IdentLink *prevstack = cs.stack->next;
    IdentLink aliaslink = {
        cs.stack->id, cs.stack, prevstack->usedargs, prevstack->argstack
    };
    cs.stack = &aliaslink;
    body();
    prevstack->usedargs = aliaslink.usedargs;
    cs.stack = aliaslink.next;
    int argmask2 = cs.stack->usedargs;
    for(int i = 0; argmask2; argmask2 >>= 1, ++i) if(argmask2 & 1)
        cs.identmap[i]->redo_arg(argstack[i]);
}

} /*namespace cscript */

#endif
