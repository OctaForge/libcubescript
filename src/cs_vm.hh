#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript/cubescript.hh"

#include <cstdlib>
#include <array>
#include <vector>
#include <type_traits>

#include "cs_std.hh"
#include "cs_bcode.hh"
#include "cs_ident.hh"

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

static constexpr int DummyIdx = MaxArguments;
static constexpr int NumargsIdx = MaxArguments + 1;
static constexpr int DbgaliasIdx = MaxArguments + 2;

static const int cs_valtypet[] = {
    CS_VAL_NULL, CS_VAL_INT, CS_VAL_FLOAT, CS_VAL_STRING,
    CS_VAL_CODE, CS_VAL_IDENT
};

static inline int cs_vtype_to_int(cs_value_type v) {
    return cs_valtypet[int(v)];
}

struct CsBreakException {
};

struct CsContinueException {
};

template<typename T>
constexpr size_t CsTypeStorageSize =
    (sizeof(T) - 1) / sizeof(uint32_t) + 1;

struct cs_gen_state {
    cs_state &cs;
    cs_gen_state *prevps;
    bool parsing = true;
    cs_valbuf<uint32_t> code;
    char const *source, *send;
    size_t current_line;
    std::string_view src_name;

    cs_gen_state() = delete;
    cs_gen_state(cs_state &csr):
        cs{csr}, prevps{csr.p_pstate}, code{cs},
        source{}, send{}, current_line{1}, src_name{}
    {
        csr.p_pstate = this;
    }

    ~cs_gen_state() {
        done();
    }

    void done() {
        if (!parsing) {
            return;
        }
        cs.p_pstate = prevps;
        parsing = false;
    }

    std::string_view get_str();
    cs_charbuf get_str_dup();

    std::string_view get_word();

    void gen_str(std::string_view word) {
        if (word.size() <= 3) {
            uint32_t op = CS_CODE_VAL_INT | CS_RET_STRING;
            for (size_t i = 0; i < word.size(); ++i) {
                op |= uint32_t(
                    static_cast<unsigned char>(word[i])
                ) << ((i + 1) * 8);
            }
            code.push_back(op);
            return;
        }
        code.push_back(CS_CODE_VAL | CS_RET_STRING | (word.size() << 8));
        auto it = reinterpret_cast<uint32_t const *>(word.data());
        code.append(it, it + (word.size() / sizeof(uint32_t)));
        size_t esz = word.size() % sizeof(uint32_t);
        union {
            char c[sizeof(uint32_t)];
            uint32_t u;
        } end;
        end.u = 0;
        memcpy(end.c, word.data() + word.size() - esz, esz);
        code.push_back(end.u);
    }

    void gen_str() {
        code.push_back(CS_CODE_VAL_INT | CS_RET_STRING);
    }

    void gen_null() {
        code.push_back(CS_CODE_VAL_INT | CS_RET_NULL);
    }

    void gen_int(cs_int i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push_back(CS_CODE_VAL_INT | CS_RET_INT | (i << 8));
        } else {
            union {
                cs_int i;
                uint32_t u[CsTypeStorageSize<cs_int>];
            } c;
            c.i = i;
            code.push_back(CS_CODE_VAL | CS_RET_INT);
            code.append(c.u, c.u + CsTypeStorageSize<cs_int>);
        }
    }

    void gen_int(std::string_view word);

    void gen_float(cs_float f = 0.0f) {
        if (cs_int(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push_back(CS_CODE_VAL_INT | CS_RET_FLOAT | (cs_int(f) << 8));
        } else {
            union {
                cs_float f;
                uint32_t u[CsTypeStorageSize<cs_float>];
            } c;
            c.f = f;
            code.push_back(CS_CODE_VAL | CS_RET_FLOAT);
            code.append(c.u, c.u + CsTypeStorageSize<cs_float>);
        }
    }

    void gen_float(std::string_view word);

    void gen_ident(cs_ident *id) {
        code.push_back(
            ((id->get_index() < MaxArguments)
                ? CS_CODE_IDENT_ARG
                : CS_CODE_IDENT
            ) | (id->get_index() << 8)
        );
    }

    void gen_ident() {
        gen_ident(cs.p_state->identmap[DummyIdx]);
    }

    void gen_ident(std::string_view word) {
        gen_ident(cs.new_ident(word));
    }

    void gen_value(
        int wordtype, std::string_view word = std::string_view(),
        int line = 0
    );

    void gen_main(std::string_view s, int ret_type = CS_VAL_ANY);

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

void bcode_ref(uint32_t *code);
void bcode_unref(uint32_t *code);

template<typename F>
static void cs_do_args(cs_state &cs, F body) {
    if (!cs.p_callstack) {
        body();
        return;
    }
    cs_valarray<cs_ident_stack, MaxArguments> argstack{cs};
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            static_cast<cs_alias_impl *>(cs.p_state->identmap[i])->undo_arg(
                argstack[i]
            );
        }
    }
    cs_ident_link *prevstack = cs.p_callstack->next;
    cs_ident_link aliaslink = {
        cs.p_callstack->id, cs.p_callstack,
        prevstack ? prevstack->usedargs : ((1 << MaxArguments) - 1),
        prevstack ? prevstack->argstack : nullptr
    };
    cs.p_callstack = &aliaslink;
    call_with_cleanup(std::move(body), [&]() {
        if (prevstack) {
            prevstack->usedargs = aliaslink.usedargs;
        }
        cs.p_callstack = aliaslink.next;
        int argmask2 = cs.p_callstack->usedargs;
        for (int i = 0; argmask2; argmask2 >>= 1, ++i) {
            if (argmask2 & 1) {
                static_cast<cs_alias_impl *>(cs.p_state->identmap[i])->redo_arg(
                    argstack[i]
                );
            }
        }
    });
}

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CS_VM_HH */
