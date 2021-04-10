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

/* instructions consist of:
 *
 * [D 24][M 2][O 6] == I
 *
 * I: instruction
 * O: opcode
 * M: type mask
 * D: data
 *
 * also:
 *
 * R: result slot
 *
 * "force to M" means changing the type of the value as described by the
 * type mask; this is generally string/integer/float, null in general
 * preserves the type, except where mentioned
 */
enum {
    /* noop */
    BC_INST_START = 0,
    BC_INST_OFFSET,
    /* set R to null/true/false according to M */
    BC_INST_NULL, BC_INST_TRUE, BC_INST_FALSE,
    /* pop a value off the stack and set R to negated value according to M */
    BC_INST_NOT,
    /* pop a value off the stack */
    BC_INST_POP,
    /* recursively invoke VM from next instruction, push result on the stack */
    BC_INST_ENTER,
    /* recursively invoke VM from next instruction, result in R */
    BC_INST_ENTER_RESULT,
    /* exit VM, force R according to M */
    BC_INST_EXIT,
    /* pop a value off the stack and set R according to M */
    BC_INST_RESULT,
    /* push R on the stack according to M */
    BC_INST_RESULT_ARG,
    /* force top of the stack according to M */
    BC_INST_FORCE,
    /* duplicate top of the stack according to M */
    BC_INST_DUP,
    /* push value after I on the stack according to M (length D if string) */
    BC_INST_VAL,
    /* push value inside D on the stack according to M
     *
     * strings are at most 3 bytes long, integers and floats must be
     * integral values between -0x800000 and 0x7FFFFF inclusive
     */
    BC_INST_VAL_INT,
    /* pop D aliases off the stack, push their values and recurse the VM
     * pop their values afterwards (i.e. they are local to the execution)
     */
    BC_INST_LOCAL,
    /* pop a value off the stack, execute its bytecode,
     * result in R according to M
     */
    BC_INST_DO,
    /* like above, except argument aliases are restored to the previous
     * callstack level before calling (and restored back afterwards)
     */
    BC_INST_DO_ARGS,
    /* jump forward by D instructions */
    BC_INST_JUMP,
    /* conditional jump: pop a value off the stack, jump only if considered
     * true or false (see BC_INST_FLAG_TRUE/FALSE)
     */
    BC_INST_JUMP_B,
    /* conditional jump: pop a value off the stack, if it's bytecode,
     * eval it (saving the value into R), if it's not, save the value
     * into R, then jump only if the value is considered true or false
     * (see BC_INST_FLAG_TRUE/FALSE)
     */
    BC_INST_JUMP_RESULT,
    /* break or continue a loop; if no loop is currently running, raise
     * an error, otherwise break (if BC_INST_FLAG_FALSE) or continue
     * (if BC_INST_FLAG_TRUE)
     */
    BC_INST_BREAK,
    /* bytecode of length D follows, push on the stack as bytecode */
    BC_INST_BLOCK,
    /* push bytecode of (BC_INST_EXIT | M) on the stack */
    BC_INST_EMPTY,
    /* compile the value on top of the stack as if it was a string (null for
     * non-string/integer/float values) */
    BC_INST_COMPILE,
    /* compile the value on top of the stack if string; if string is empty,
     * force to null, if not string, keep as is
     */
    BC_INST_COND,
    /* push ident with index D on the stack; if arg, push val and mark used */
    BC_INST_IDENT,
    /* make value on top of stack an ident; if value is string, that is
     * the ident name, otherwise dummy is used; ident is created if non
     * existent, and if arg, push val and mark used
     */
    BC_INST_IDENT_U,
    /* lookup the alias with index D and push its value (error if unset) */
    BC_INST_LOOKUP,
    /* lookup an unknown ident with the name being given by the string on
     * top of the stack; if a var or a set alias, update top of the stack
     * to the ident's value (according to M), else raise error
     */
    BC_INST_LOOKUP_U,
    /* concatenate D values on top of the stack together, with topmost value
     * being last; delimit with spaces; push the result according to M
     */
    BC_INST_CONC,
    /* like above but without delimiter */
    BC_INST_CONC_W,
    /* push the value of svar with index D on the stack according to M */
    BC_INST_SVAR,
    /* push the value of ivar with index D on the stack according to M */
    BC_INST_IVAR,
    /* push the value of fvar with index D on the stack according to M */
    BC_INST_FVAR,
    /* pop a value off the stack and set vvar with index D to it */
    BC_INST_FVAR1,
    /* pop a value off the stack and set alias with index D to it */
    BC_INST_ALIAS,
    /* pop 2 values off the stack; top is value to set, below is alias name */
    BC_INST_ALIAS_U,
    /* call alias with index D and arg count following the instruction, pop
     * the arguments off the stack (top being last); if unknown, raise error,
     * store result in R according to M
     */
    BC_INST_CALL,
    /* given argument count D, pop the arguments off the stack (top being last)
     * and then pop one more value (that being the ident name); look up the
     * ident (raise error if non-existent) and then call according to its
     * type (vars behave as in PRINT); store result in R according to M
     */
    BC_INST_CALL_U,
    /* call builtin command with index D; arguments are popped off the stack,
     * last argument being topmost; result of the call goes in R according to M
     */
    BC_INST_COM,
    /* call builtin command with index D and arg count following the
     * instruction, arguments are popped off the stack and passed as is
     */
    BC_INST_COM_V,
    /* call builtin command with index D and arg count following the
     * instruction, arguments are popped off the stack and concatenated
     */
    BC_INST_COM_C,

    /* opcode mask */
    BC_INST_OP_MASK = 0x3F,
    /* type mask shift */
    BC_INST_RET = 6,
    /* type mask, shifted */
    BC_INST_RET_MASK = 0xC0,

    /* type mask flags */
    BC_RET_NULL   = VAL_NULL << BC_INST_RET,
    BC_RET_STRING = VAL_STRING << BC_INST_RET,
    BC_RET_INT    = VAL_INT << BC_INST_RET,
    BC_RET_FLOAT  = VAL_FLOAT << BC_INST_RET,

    /* BC_INST_JUMP_B, BC_INST_JUMP_RESULT */
    BC_INST_FLAG_TRUE = 1 << BC_INST_RET,
    BC_INST_FLAG_FALSE = 0 << BC_INST_RET
};

std::uint32_t *bcode_alloc(internal_state *cs, std::size_t sz);

void bcode_addref(std::uint32_t *code);
void bcode_unref(std::uint32_t *code);

struct empty_block {
    bcode init;
    std::uint32_t code;
};

empty_block *bcode_init_empty(internal_state *cs);
void bcode_free_empty(internal_state *cs, empty_block *empty);
bcode *bcode_get_empty(empty_block *empty, std::size_t val);

struct bcode_p {
    bcode_p(bcode_ref const &r): br{const_cast<bcode_ref *>(&r)} {}

    bcode *get() {
        return br->p_code;
    }

    static bcode_ref make_ref(bcode *v) {
        return bcode_ref{v};
    }

    bcode_ref *br;
};

} /* namespace cubescript */

#endif
