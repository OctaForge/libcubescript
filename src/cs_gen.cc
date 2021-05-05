#include <cstdlib>
#include <cstring>
#include <cmath>

#include "cs_gen.hh"

#include "cs_ident.hh"
#include "cs_parser.hh"

namespace cubescript {

static inline int ret_code(int type, int def = 0) {
    if (type >= VAL_ANY) {
        return def;
    }
    return type << BC_INST_RET;
}

std::size_t gen_state::count() const {
    return code.size();
}

std::uint32_t gen_state::peek(std::size_t idx) const {
    return code[idx];
}

bcode_ref gen_state::steal_ref() {
    auto *cp = bcode_alloc(ts.istate, code.size());
    std::memcpy(cp, code.data(), code.size() * sizeof(std::uint32_t));
    return bcode_p::make_ref(reinterpret_cast<bcode *>(cp + 1));
}

void gen_state::gen_pop() {
    code.push_back(BC_INST_POP);
}

void gen_state::gen_dup(int ltype) {
    code.push_back(BC_INST_DUP | ret_code(ltype));
}

void gen_state::gen_result(int ltype) {
    code.push_back(BC_INST_RESULT | ret_code(ltype));
}

void gen_state::gen_push_result(int ltype) {
    code.push_back(BC_INST_RESULT_ARG | ret_code(ltype));
}

void gen_state::gen_force(int ltype) {
    code.push_back(BC_INST_FORCE | ret_code(ltype, BC_RET_STRING));
}

void gen_state::gen_not(int ltype) {
    code.push_back(BC_INST_NOT | ret_code(ltype));
}

bool gen_state::gen_if(std::size_t tpos, std::size_t fpos, int ltype) {
    auto inst1 = code[tpos];
    auto op1 = inst1 & ~BC_INST_RET_MASK;
    auto tlen = std::uint32_t(fpos - tpos - 1);
    if (!fpos) {
        if (is_block(tpos, fpos)) {
            code[tpos] = (tlen << 8) | BC_INST_JUMP_B | BC_INST_FLAG_FALSE;
            code[tpos + 1] = BC_INST_ENTER_RESULT;
            code[tpos + tlen] = (
                code[tpos + tlen] & ~BC_INST_RET_MASK
            ) | ret_code(ltype);
            return true;
        }
        gen_block();
    } else {
        auto inst2 = code[fpos];
        auto flen = std::uint32_t(count() - fpos - 1);
        if (is_block(fpos)) {
            if (is_block(tpos, fpos)) {
                code[tpos] = (std::uint32_t(fpos - tpos) << 8)
                    | BC_INST_JUMP_B | BC_INST_FLAG_FALSE;
                code[tpos + 1] = BC_INST_ENTER_RESULT;
                code[tpos + tlen] = (
                    code[tpos + tlen] & ~BC_INST_RET_MASK
                ) | ret_code(ltype);
                code[fpos] = (flen << 8) | BC_INST_JUMP;
                code[fpos + 1] = BC_INST_ENTER_RESULT;
                code[fpos + flen] = (
                    code[fpos + flen] & ~BC_INST_RET_MASK
                ) | ret_code(ltype);
                return true;
            } else if (op1 == BC_INST_EMPTY) {
                code[tpos] = BC_INST_NULL | (inst2 & BC_INST_RET_MASK);
                code[fpos] = (flen << 8) | BC_INST_JUMP_B | BC_INST_FLAG_TRUE;
                code[fpos + 1] = BC_INST_ENTER_RESULT;
                code[fpos + flen] = (
                    code[fpos + flen] & ~BC_INST_RET_MASK
                ) | ret_code(ltype);
                return true;
            }
        }
    }
    return false;
}

void gen_state::gen_and_or(bool is_or, std::size_t start, int ltype) {
    std::uint32_t op;
    if (is_or) {
        op = (BC_INST_JUMP_RESULT | BC_INST_FLAG_TRUE);
    } else {
        op = (BC_INST_JUMP_RESULT | BC_INST_FLAG_FALSE);
    }
    code.push_back(op);
    std::size_t end = count();
    while ((start + 1) < end) {
        uint32_t len = code[start] >> 8;
        code[start] = std::uint32_t((end - start - 1) << 8) | op;
        code[start + 1] = BC_INST_ENTER;
        code[start + len] = (
            code[start + len] & ~BC_INST_RET_MASK
        ) | ret_code(ltype);
        start += len + 1;
    }
}

void gen_state::gen_val_null() {
    code.push_back(BC_INST_VAL_INT | BC_RET_NULL);
}

void gen_state::gen_result_null(int ltype) {
    code.push_back(BC_INST_NULL | ret_code(ltype));
}

void gen_state::gen_result_true(int ltype) {
    code.push_back(BC_INST_TRUE | ret_code(ltype));
}

void gen_state::gen_result_false(int ltype) {
    code.push_back(BC_INST_FALSE | ret_code(ltype));
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
                    str = parse_string(*ts.pstate, make_str_view(str, send));
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
    code.push_back(BC_INST_IDENT | (i.index() << 8));
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

void gen_state::gen_lookup_ivar(ident &id, int ltype) {
    code.push_back(
        BC_INST_IVAR | ret_code(ltype, BC_RET_INT) | (id.index() << 8)
    );
}

void gen_state::gen_lookup_fvar(ident &id, int ltype) {
    code.push_back(
        BC_INST_FVAR | ret_code(ltype, BC_RET_FLOAT) | (id.index() << 8)
    );
}

void gen_state::gen_lookup_svar(ident &id, int ltype) {
    code.push_back(
        BC_INST_SVAR | ret_code(ltype, BC_RET_STRING) | (id.index() << 8)
    );
}

void gen_state::gen_lookup_alias(ident &id, int ltype, int dtype) {
    code.push_back(
        BC_INST_LOOKUP | ret_code(ltype, ret_code(dtype)) | (id.index() << 8)
    );
}

void gen_state::gen_lookup_ident(int ltype) {
    code.push_back(BC_INST_LOOKUP_U | ret_code(ltype));
}

void gen_state::gen_assign_alias(ident &id) {
    code.push_back(BC_INST_ALIAS | (id.index() << 8));
}

void gen_state::gen_assign() {
    code.push_back(BC_INST_ALIAS_U);
}

void gen_state::gen_compile(bool cond) {
    if (cond) {
        code.push_back(BC_INST_COND);
    } else {
        code.push_back(BC_INST_COMPILE);
    }
}

void gen_state::gen_ident_lookup() {
    code.push_back(BC_INST_IDENT_U);
}

void gen_state::gen_concat(std::size_t concs, bool space, int ltype) {
    if (!concs) {
        return;
    }
    if (space) {
        code.push_back(
            BC_INST_CONC | ret_code(ltype) | std::uint32_t(concs << 8)
        );
    } else {
        code.push_back(
            BC_INST_CONC_W | ret_code(ltype) | std::uint32_t(concs << 8)
        );
    }
}

void gen_state::gen_command_call(
    ident &id, int comt, int ltype, std::uint32_t nargs
) {
    code.push_back(comt | ret_code(ltype) | (id.index() << 8));
    if (comt != BC_INST_COM) {
        code.push_back(nargs);
    }
}

void gen_state::gen_alias_call(ident &id, std::uint32_t nargs) {
    code.push_back(BC_INST_CALL | (id.index() << 8));
    code.push_back(nargs);
}

void gen_state::gen_call(std::uint32_t nargs) {
    code.push_back(BC_INST_CALL_U | (nargs << 8));
}

void gen_state::gen_local(std::uint32_t nargs) {
    code.push_back(BC_INST_LOCAL | (nargs << 8));
}

void gen_state::gen_do(bool args, int ltype) {
    if (args) {
        code.push_back(BC_INST_DO_ARGS | ret_code(ltype));
    } else {
        code.push_back(BC_INST_DO | ret_code(ltype));
    }
}

void gen_state::gen_break() {
    code.push_back(BC_INST_BREAK | BC_INST_FLAG_FALSE);
}

void gen_state::gen_continue() {
    code.push_back(BC_INST_BREAK | BC_INST_FLAG_TRUE);
}

void gen_state::gen_main(std::string_view v, std::string_view src) {
    parser_state ps{ts, *this};
    ps.source = v.data();
    ps.send = v.data() + v.size();
    auto psrc = ts.source;
    ts.source = src;
    try {
        code.push_back(BC_INST_START);
        ps.parse_block(VAL_ANY);
        code.push_back(BC_INST_EXIT);
    } catch (...) {
        ts.source = psrc;
        throw;
    }
    ts.source = psrc;
}

void gen_state::gen_main_null() {
    code.reserve(code.size() + 4);
    code.push_back(BC_INST_START);
    gen_val_null();
    gen_result();
    code.push_back(BC_INST_EXIT);
}

void gen_state::gen_main_integer(integer_type v) {
    code.reserve(code.size() + bc_store_size<integer_type> + 3);
    code.push_back(BC_INST_START);
    gen_val_integer(v);
    gen_result();
    code.push_back(BC_INST_EXIT);
}

void gen_state::gen_main_float(float_type v) {
    code.reserve(code.size() + bc_store_size<float_type> + 3);
    code.push_back(BC_INST_START);
    gen_val_float(v);
    gen_result();
    code.push_back(BC_INST_EXIT);
}

bool gen_state::is_block(std::size_t idx, std::size_t epos) const {
    if (!epos) {
        epos = count();
    }
    return ((code[idx] & ~BC_INST_RET_MASK) == (
        BC_INST_BLOCK | (std::uint32_t(epos - idx - 1) << 8)
    ));
}

void gen_state::gen_block() {
    code.push_back(BC_INST_EMPTY);
}

std::pair<std::size_t, std::string_view> gen_state::gen_block(
    std::string_view v, std::size_t line, int ltype, int term
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
        v = make_str_view(ps.source, ps.send);
        ret_line = ps.current_line;
    }
    if (code.size() > (csz + 2)) {
        code.push_back(BC_INST_EXIT | ret_code(ltype));
        /* encode the block size in BC_INST_BLOCK */
        code[csz] |= (std::uint32_t(code.size() - csz - 1) << 8);
    } else {
        /* empty code */
        code.resize(csz);
        code.push_back(BC_INST_EMPTY | ret_code(ltype));
    }
    return std::make_pair(ret_line, v);
}

} /* namespace cubescript */
