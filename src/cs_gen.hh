#ifndef LIBCUBESCRIPT_GEN_HH
#define LIBCUBESCRIPT_GEN_HH

#include <cstdint>
#include <string_view>
#include <utility>

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_bcode.hh"
#include "cs_thread.hh"

namespace cubescript {

struct gen_state {
    thread_state &ts;

    gen_state() = delete;
    gen_state(thread_state &tsr):
        ts{tsr}, code{tsr.istate}
    {}

    std::size_t count() const;
    std::uint32_t peek(std::size_t idx) const;

    bcode_ref steal_ref();

    void gen_pop();
    void gen_dup(int ltype = 0);
    void gen_result(int ltype = 0);
    void gen_push_result(int ltype = 0);
    void gen_force(int ltype);

    void gen_not(int ltype = 0);
    bool gen_if(std::size_t tpos, std::size_t fpos, int ltype = 0);
    void gen_and_or(bool is_or, std::size_t start, int ltype = 0);

    void gen_val_null();
    void gen_result_null(int ltype = 0);
    void gen_result_true(int ltype = 0);
    void gen_result_false(int ltype = 0);

    void gen_val_integer(integer_type v = 0);
    void gen_val_integer(std::string_view v);

    void gen_val_float(float_type v = 0);
    void gen_val_float(std::string_view v);

    void gen_val_string(std::string_view v = std::string_view{});
    void gen_val_string_unescape(std::string_view str);
    void gen_val_block(std::string_view str);

    void gen_val_ident();
    void gen_val_ident(ident &i);
    void gen_val_ident(std::string_view v);

    void gen_val(
        int val_type, std::string_view v = std::string_view{},
        std::size_t line = 0
    );

    void gen_lookup_var(ident &id, int ltype = 0);

    void gen_lookup_alias(ident &id, int ltype = 0, int dtype = 0);
    void gen_lookup_ident(int ltype = 0);

    void gen_assign_alias(ident &id);
    void gen_assign();

    void gen_compile(bool cond = false);
    void gen_ident_lookup();

    void gen_concat(std::size_t concs, bool space, int ltype = 0);

    void gen_command_call(
        ident &id, int comt, int ltype = 0, std::uint32_t nargs = 0
    );
    void gen_alias_call(ident &id, std::uint32_t nargs = 0);
    void gen_call(std::uint32_t nargs = 0);

    void gen_local(std::uint32_t nargs);
    void gen_do(bool args, int ltype = 0);

    void gen_break();
    void gen_continue();

    void gen_main(
        std::string_view s, std::string_view src = std::string_view{}
    );
    void gen_main_null();
    void gen_main_integer(integer_type v);
    void gen_main_float(float_type v);

    bool is_block(std::size_t idx, std::size_t epos = 0) const;

    void gen_block();
    std::pair<std::size_t, std::string_view> gen_block(
        std::string_view v, std::size_t line,
        int ltype = VAL_NULL, int term = '\0'
    );

private:
    valbuf<std::uint32_t> code;
};

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_GEN_HH */
