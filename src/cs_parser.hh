#ifndef LIBCUBESCRIPT_PARSER_HH
#define LIBCUBESCRIPT_PARSER_HH

#include <cstdlib>
#include <string_view>
#include <type_traits>

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_bcode.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"
#include "cs_gen.hh"

namespace cubescript {

integer_type parse_int(std::string_view input, std::string_view *end = nullptr);
float_type parse_float(std::string_view input, std::string_view *end = nullptr);

bool is_valid_name(std::string_view input);

struct parser_state {
    thread_state &ts;
    gen_state &gs;
    char const *source, *send;
    std::size_t current_line;

    parser_state() = delete;
    parser_state(thread_state &tsr, gen_state &gsr):
        ts{tsr}, gs{gsr}, source{}, send{}, current_line{1}
    {
        ts.current_line = &current_line;
    }

    ~parser_state() {
        ts.current_line = nullptr;
    }

    std::string_view get_str();
    charbuf get_str_dup();

    std::string_view get_word();

    void parse_block(int ltype, int term = '\0');

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

    void parse_lookup(int ltype);
    bool parse_subblock();
    void parse_blockarg(int ltype);
    bool parse_arg(int ltype, charbuf *word = nullptr);

    bool parse_call_command(
        command_impl *id, ident &self, int rettype, std::uint32_t limit = 0
    );
    bool parse_call_alias(alias &id);
    bool parse_call_id(ident &id, int ltype);

    bool parse_assign(charbuf &idname, int ltype, int term, bool &noass);

    bool parse_id_local();
    bool parse_id_do(bool args, int ltype);
    bool parse_id_if(ident &id, int ltype);
    bool parse_id_and_or(ident &id, int ltype);
};

} /* namespace cubescript */

#endif
