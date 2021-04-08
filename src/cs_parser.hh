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

namespace cubescript {

integer_type parse_int(std::string_view input, std::string_view *end = nullptr);
float_type parse_float(std::string_view input, std::string_view *end = nullptr);

bool is_valid_name(std::string_view input);

struct parser_state {
    thread_state &ts;
    parser_state *prevps;
    bool parsing = true;
    valbuf<uint32_t> code;
    char const *source, *send;
    std::size_t current_line;
    std::string_view src_name;

    parser_state() = delete;
    parser_state(thread_state &tsr):
        ts{tsr}, prevps{tsr.cstate}, code{tsr.istate},
        source{}, send{}, current_line{1}, src_name{}
    {
        tsr.cstate = this;
    }

    ~parser_state() {
        done();
    }

    void done() {
        if (!parsing) {
            return;
        }
        ts.cstate = prevps;
        parsing = false;
    }

    std::string_view get_str();
    charbuf get_str_dup();

    std::string_view get_word();

    void gen_str(std::string_view word = std::string_view{}) {
        if (word.size() <= 3) {
            std::uint32_t op = BC_INST_VAL_INT | BC_RET_STRING;
            for (size_t i = 0; i < word.size(); ++i) {
                op |= std::uint32_t(
                    static_cast<unsigned char>(word[i])
                ) << ((i + 1) * 8);
            }
            code.push_back(op);
            return;
        }
        code.push_back(
            BC_INST_VAL | BC_RET_STRING | std::uint32_t(word.size() << 8)
        );
        auto it = reinterpret_cast<std::uint32_t const *>(word.data());
        code.append(it, it + (word.size() / sizeof(std::uint32_t)));
        std::size_t esz = word.size() % sizeof(std::uint32_t);
        char c[sizeof(std::uint32_t)] = {0};
        std::memcpy(c, word.data() + word.size() - esz, esz);
        std::uint32_t u;
        std::memcpy(&u, c, sizeof(u));
        code.push_back(u);
    }

    void gen_null() {
        code.push_back(BC_INST_VAL_INT | BC_RET_NULL);
    }

    void gen_int(integer_type i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push_back(BC_INST_VAL_INT | BC_RET_INT | (i << 8));
        } else {
            std::uint32_t u[bc_store_size<integer_type>] = {0};
            std::memcpy(u, &i, sizeof(i));
            code.push_back(BC_INST_VAL | BC_RET_INT);
            code.append(u, u + bc_store_size<integer_type>);
        }
    }

    void gen_int(std::string_view word);

    void gen_float(float_type f = 0.0f) {
        if (integer_type(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push_back(BC_INST_VAL_INT | BC_RET_FLOAT | (integer_type(f) << 8));
        } else {
            std::uint32_t u[bc_store_size<float_type>] = {0};
            std::memcpy(u, &f, sizeof(f));
            code.push_back(BC_INST_VAL | BC_RET_FLOAT);
            code.append(u, u + bc_store_size<float_type>);
        }
    }

    void gen_float(std::string_view word);

    void gen_ident(ident &id) {
        code.push_back(BC_INST_IDENT | (id.get_index() << 8));
    }

    void gen_ident() {
        gen_ident(*ts.istate->id_dummy);
    }

    void gen_ident(std::string_view word) {
        gen_ident(ts.istate->new_ident(*ts.pstate, word, IDENT_FLAG_UNKNOWN));
    }

    void gen_value(
        int wordtype, std::string_view word = std::string_view(),
        int line = 0
    );

    void gen_main(std::string_view s, int ret_type = VAL_ANY);

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

} /* namespace cubescript */

#endif
