#ifndef LIBCUBESCRIPT_BCODE_HH
#define LIBCUBESCRIPT_BCODE_HH

#include <cubescript/cubescript.hh>

#include <cstdint>
#include <cstddef>

namespace cubescript {

struct bcode {
    std::uint32_t init;

    std::uint32_t *get_raw() {
        return &init;
    }

    std::uint32_t const *get_raw() const {
        return &init;
    }
};

enum {
    VAL_NULL = 0, VAL_INT, VAL_FLOAT, VAL_STRING,
    VAL_ANY, VAL_CODE, VAL_IDENT, VAL_WORD,
    VAL_POP, VAL_COND
};

template<typename T>
constexpr std::size_t bc_store_size = (
    sizeof(T) - 1
) / sizeof(std::uint32_t) + 1;

/* instruction: uint32 [length 24][retflag 2][opcode 6] */
enum {
    BC_INST_START = 0,
    BC_INST_OFFSET,
    BC_INST_NULL, BC_INST_TRUE, BC_INST_FALSE, BC_INST_NOT,
    BC_INST_POP,
    BC_INST_ENTER, BC_INST_ENTER_RESULT,
    BC_INST_EXIT, BC_INST_RESULT_ARG,
    BC_INST_VAL, BC_INST_VAL_INT,
    BC_INST_DUP,
    BC_INST_BOOL,
    BC_INST_BLOCK, BC_INST_EMPTY,
    BC_INST_COMPILE, BC_INST_COND,
    BC_INST_FORCE,
    BC_INST_RESULT,
    BC_INST_IDENT, BC_INST_IDENT_U, BC_INST_IDENT_ARG,
    BC_INST_COM, BC_INST_COM_C, BC_INST_COM_V,
    BC_INST_CONC, BC_INST_CONC_W, BC_INST_CONC_M,
    BC_INST_SVAR, BC_INST_SVAR1,
    BC_INST_IVAR, BC_INST_IVAR1, BC_INST_IVAR2, BC_INST_IVAR3,
    BC_INST_FVAR, BC_INST_FVAR1,
    BC_INST_LOOKUP, BC_INST_LOOKUP_U, BC_INST_LOOKUP_ARG,
    BC_INST_LOOKUP_M, BC_INST_LOOKUP_MU, BC_INST_LOOKUP_MARG,
    BC_INST_ALIAS, BC_INST_ALIAS_U, BC_INST_ALIAS_ARG,
    BC_INST_CALL, BC_INST_CALL_U, BC_INST_CALL_ARG,
    BC_INST_PRINT,
    BC_INST_LOCAL,
    BC_INST_DO, BC_INST_DO_ARGS,
    BC_INST_JUMP, BC_INST_JUMP_B, BC_INST_JUMP_RESULT,
    BC_INST_BREAK,

    BC_INST_OP_MASK = 0x3F,
    BC_INST_RET = 6,
    BC_INST_RET_MASK = 0xC0,

    /* return type flags */
    BC_RET_NULL   = VAL_NULL << BC_INST_RET,
    BC_RET_STRING = VAL_STRING << BC_INST_RET,
    BC_RET_INT    = VAL_INT << BC_INST_RET,
    BC_RET_FLOAT  = VAL_FLOAT << BC_INST_RET,

    /* BC_INST_JUMP_B, BC_INST_JUMP_RESULT */
    BC_INST_FLAG_TRUE = 1 << BC_INST_RET,
    BC_INST_FLAG_FALSE = 0 << BC_INST_RET
};

std::uint32_t *bcode_alloc(state &cs, std::size_t sz);

void bcode_incr(std::uint32_t *code);
void bcode_decr(std::uint32_t *code);
void bcode_addref(std::uint32_t *code);
void bcode_unref(std::uint32_t *code);

struct empty_block {
    bcode init;
    std::uint32_t code;
};

empty_block *bcode_init_empty(internal_state *cs);
void bcode_free_empty(internal_state *cs, empty_block *empty);
bcode *bcode_get_empty(empty_block *empty, std::size_t val);

} /* namespace cubescript */

#endif
