#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"

#include <utility>

namespace cubescript {

struct break_exception {
};

struct continue_exception {
};

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup = false
);

void exec_alias(
    thread_state &ts, alias *a, any_value *args, any_value &result,
    std::size_t callargs, std::size_t &nargs, std::size_t offset,
    std::size_t skip, alias_stack &astack
);

any_value exec_code_with_args(thread_state &ts, bcode_ref const &body);

std::uint32_t *vm_exec(
    thread_state &ts, std::uint32_t *code, any_value &result
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_VM_HH */
