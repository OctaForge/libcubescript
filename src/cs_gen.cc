#include <cstdlib>
#include <cstring>
#include <cmath>

#include "cs_gen.hh"

#include "cs_ident.hh"
#include "cs_parser.hh"

namespace cubescript {

bcode_ref gen_state::steal_ref() {
    auto *cp = bcode_alloc(ts.istate, code.size());
    std::memcpy(cp, code.data(), code.size() * sizeof(std::uint32_t));
    return bcode_ref{reinterpret_cast<bcode *>(cp + 1)};
}

void gen_state::gen_val_null() {
    code.push_back(BC_INST_VAL_INT | BC_RET_NULL);
}

void gen_state::gen_val_integer(integer_type v) {
    if (v >= -0x800000 && v <= 0x7FFFFF) {
        code.push_back(BC_INST_VAL_INT | BC_RET_INT | (v << 8));
    } else {
        std::uint32_t u[bc_store_size<integer_type>] = {0};
        std::memcpy(u, &v, sizeof(v));
        code.push_back(BC_INST_VAL | BC_RET_INT);
        code.append(u, u + bc_store_size<integer_type>);
    }
}

void gen_state::gen_val_integer(std::string_view v) {
    gen_val_integer(parse_int(v));
}

void gen_state::gen_val_float(float_type v) {
    if (std::floor(v) == v && v >= -0x800000 && v <= 0x7FFFFF) {
        code.push_back(
            BC_INST_VAL_INT | BC_RET_FLOAT | (integer_type(std::floor(v)) << 8)
        );
    } else {
        std::uint32_t u[bc_store_size<float_type>] = {0};
        std::memcpy(u, &v, sizeof(v));
        code.push_back(BC_INST_VAL | BC_RET_FLOAT);
        code.append(u, u + bc_store_size<float_type>);
    }
}

void gen_state::gen_val_float(std::string_view v) {
    gen_val_float(parse_float(v));
}

void gen_state::gen_val_string(std::string_view v) {
    auto vsz = v.size();
    if (vsz <= 3) {
        std::uint32_t op = BC_INST_VAL_INT | BC_RET_STRING;
        for (size_t i = 0; i < vsz; ++i) {
            auto c = static_cast<unsigned char>(v[i]);
            op |= std::uint32_t(c) << ((i + 1) * 8);
        }
        code.push_back(op);
        return;
    }
    code.push_back(BC_INST_VAL | BC_RET_STRING | std::uint32_t(vsz << 8));
    auto it = reinterpret_cast<std::uint32_t const *>(v.data());
    code.append(it, it + (v.size() / sizeof(std::uint32_t)));
    std::size_t esz = v.size() % sizeof(std::uint32_t);
    char c[sizeof(std::uint32_t)] = {0};
    std::memcpy(c, v.data() + v.size() - esz, esz);
    std::uint32_t u;
    std::memcpy(&u, c, sizeof(u));
    code.push_back(u);
}

void gen_state::gen_val_ident() {
    gen_val_ident(*ts.istate->id_dummy);
}

void gen_state::gen_val_ident(ident &i) {
    code.push_back(BC_INST_IDENT | (i.get_index() << 8));
}

void gen_state::gen_val_ident(std::string_view v) {
    gen_val_ident(ts.istate->new_ident(*ts.pstate, v, IDENT_FLAG_UNKNOWN));
}

void gen_state::gen_val(
    int val_type, std::string_view v, std::size_t line
) {
    switch (val_type) {
        case VAL_ANY:
            if (!v.empty()) {
                gen_val_string(v);
            } else {
                gen_val_null();
            }
            break;
        case VAL_STRING:
            gen_val_string(v);
            break;
        case VAL_FLOAT:
            gen_val_float(v);
            break;
        case VAL_INT:
            gen_val_integer(v);
            break;
        case VAL_COND:
            if (!v.empty()) {
                gen_block(v, line);
            } else {
                gen_val_null();
            }
            break;
        case VAL_CODE:
            gen_block(v, line);
            break;
        case VAL_IDENT:
            gen_val_ident(v);
            break;
        default:
            break;
    }
}

void gen_state::gen_main_null() {
    code.reserve(code.size() + 4);
    code.push_back(BC_INST_START);
    gen_val_null();
    code.push_back(BC_INST_RESULT);
    code.push_back(BC_INST_EXIT);
}

void gen_state::gen_main_integer(integer_type v) {
    code.reserve(code.size() + bc_store_size<integer_type> + 3);
    code.push_back(BC_INST_START);
    gen_val_integer(v);
    code.push_back(BC_INST_RESULT);
    code.push_back(BC_INST_EXIT);
}

void gen_state::gen_main_float(float_type v) {
    code.reserve(code.size() + bc_store_size<float_type> + 3);
    code.push_back(BC_INST_START);
    gen_val_float(v);
    code.push_back(BC_INST_RESULT);
    code.push_back(BC_INST_EXIT);
}

void gen_state::gen_block() {
    code.push_back(BC_INST_EMPTY);
}

std::pair<std::size_t, std::string_view> gen_state::gen_block(
    std::string_view v, std::size_t line, int ret_type, int term
) {
    auto csz = code.size();
    code.push_back(BC_INST_BLOCK);
    /* encodes the offset from the start of the bytecode block
     * this is used for refcounting (subtract the offset, get to
     * the start of the original allocation, i.e. BC_INST_START)
     */
    code.push_back(BC_INST_OFFSET | std::uint32_t((csz + 2) << 8));
    auto ret_line = line;
    if (!v.empty()) {
        parser_state ps{ts, *this};
        ps.source = v.data();
        ps.send = v.data() + v.size();
        ps.current_line = line;
        ps.parse_block(VAL_ANY, term);
        v = std::string_view{ps.source, ps.send};
        ret_line = ps.current_line;
    }
    if (code.size() > (csz + 2)) {
        code.push_back(BC_INST_EXIT | ret_type);
        /* encode the block size in BC_INST_BLOCK */
        code[csz] |= (std::uint32_t(code.size() - csz - 1) << 8);
    } else {
        /* empty code */
        code.resize(csz);
        code.push_back(BC_INST_EMPTY | ret_type);
    }
    return std::make_pair(ret_line, v);
}

} /* namespace cubescript */
