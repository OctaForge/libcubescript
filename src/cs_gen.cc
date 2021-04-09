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

/* FIXME: figure out how to do without the intermediate buffer */
template<typename F>
static void gen_str_filter(
    valbuf<std::uint32_t> &code, thread_state &ts, std::string_view v, F &&func
) {
    code.push_back(BC_INST_VAL | BC_RET_STRING);
    auto ncode = code.size();
    /* we're reserving a proper number of words */
    auto nwords = (v.size() / sizeof(std::uint32_t)) + 1;
    code.reserve(ncode + nwords);
    /* allocate a character buffer that's at least that many words */
    auto al = std_allocator<char>{ts.istate};
    auto *buf = al.allocate(nwords * sizeof(std::uint32_t));
    /* the body */
    auto len = func(&buf[0]);
    /* fill the leftover bytes with zeroes */
    memset(&buf[len], 0, sizeof(std::uint32_t) - len % sizeof(std::uint32_t));
    /* set the actual length */
    code.back() |= (len << 8);
    auto *ubuf = reinterpret_cast<std::uint32_t *>(buf);
    code.append(ubuf, ubuf + ((len / sizeof(std::uint32_t)) + 1));
    al.deallocate(buf, nwords * sizeof(std::uint32_t));
}

void gen_state::gen_val_string_unescape(std::string_view v) {
    gen_str_filter(code, ts, v, [&v](auto *buf) {
        auto *wbuf = unescape_string(buf, v);
        return std::size_t(wbuf - buf);
    });
}

void gen_state::gen_val_block(std::string_view v) {
    gen_str_filter(code, ts, v, [&v, this](auto *buf) {
        auto *str = v.data();
        auto *send = v.data() + v.size();
        std::size_t len = 0;
        for (std::string_view chrs{"\r/\"@]"}; str < send;) {
            auto *orig = str;
            /* find a boundary character */
            str = std::find_first_of(str, send, chrs.begin(), chrs.end());
            /* copy everything up until boundary character */
            std::memcpy(&buf[len], orig, str - orig);
            len += (str - orig);
            /* found nothing: bail out */
            if (str == send) {
                return len;
            }
            switch (*str) {
                case '\r': /* filter out */
                    ++str;
                    break;
                case '\"': { /* quoted string */
                    char const *start = str;
                    str = parse_string(
                        *ts.pstate, std::string_view{str, send}
                    );
                    std::memcpy(&buf[len], start, std::size_t(str - start));
                    len += (str - start);
                    break;
                }
                case '/':
                    if (((str + 1) != send) && (str[1] == '/')) {
                        /* comment */
                        char const *start = str;
                        str = std::find(str, send, '\n');
                        if (((start + 2) != send) && std::ispunct(start[2])) {
                            /* these comments will be preserved */
                            std::memcpy(
                                &buf[len], start, std::size_t(str - start)
                            );
                            len += (str - start);
                        }
                    } else {
                        /* write and skip */
                        buf[len++] = *str++;
                    }
                    break;
                case '@':
                case ']':
                    if (str <send) {
                        buf[len++] = *str++;
                    } else {
                        return len;
                    }
                    break;
            }
        }
        return len;
    });
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

static inline int ret_code(int type, int def = 0) {
    if (type >= VAL_ANY) {
        return def;
    }
    return type << BC_INST_RET;
}

void gen_state::gen_lookup_ivar(ident &id, int ltype) {
    code.push_back(
        BC_INST_IVAR | ret_code(ltype, BC_RET_INT) | (id.get_index() << 8)
    );
}

void gen_state::gen_lookup_fvar(ident &id, int ltype) {
    code.push_back(
        BC_INST_FVAR | ret_code(ltype, BC_RET_FLOAT) | (id.get_index() << 8)
    );
}

void gen_state::gen_lookup_svar(ident &id, int ltype) {
    code.push_back(
        BC_INST_SVAR | ret_code(ltype, BC_RET_STRING) | (id.get_index() << 8)
    );
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
