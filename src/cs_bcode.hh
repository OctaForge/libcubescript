#ifndef LIBCUBESCRIPT_BCODE_HH
#define LIBCUBESCRIPT_BCODE_HH

#include <cubescript/cubescript.hh>

#include <cstdint>
#include <cstddef>

namespace cscript {

struct cs_bcode;

enum {
    CS_VAL_NULL = 0, CS_VAL_INT, CS_VAL_FLOAT, CS_VAL_STRING,
    CS_VAL_ANY, CS_VAL_CODE, CS_VAL_IDENT, CS_VAL_WORD,
    CS_VAL_POP, CS_VAL_COND
};

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

std::uint32_t *bcode_alloc(cs_state &cs, std::size_t sz);

void bcode_incr(std::uint32_t *code);
void bcode_decr(std::uint32_t *code);
void bcode_ref(std::uint32_t *code);
void bcode_unref(std::uint32_t *code);

} /* namespace cscript */

#endif
