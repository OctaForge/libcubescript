#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

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

static const int valtypet[] = {
    VAL_NULL, VAL_INT, VAL_FLOAT, VAL_STRING,
    VAL_CODE, VAL_IDENT
};

static inline int vtype_to_int(value_type v) {
    return valtypet[int(v)];
}

struct CsBreakException {
};

struct CsContinueException {
};

template<typename T>
constexpr size_t CsTypeStorageSize =
    (sizeof(T) - 1) / sizeof(uint32_t) + 1;

struct codegen_state {
    state &cs;
    codegen_state *prevps;
    bool parsing = true;
    valbuf<uint32_t> code;
    char const *source, *send;
    size_t current_line;
    std::string_view src_name;

    codegen_state() = delete;
    codegen_state(state &csr):
        cs{csr}, prevps{csr.p_pstate}, code{cs},
        source{}, send{}, current_line{1}, src_name{}
    {
        csr.p_pstate = this;
    }

    ~codegen_state() {
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
    charbuf get_str_dup();

    std::string_view get_word();

    void gen_str(std::string_view word) {
        if (word.size() <= 3) {
            uint32_t op = BC_INST_VAL_INT | BC_RET_STRING;
            for (size_t i = 0; i < word.size(); ++i) {
                op |= uint32_t(
                    static_cast<unsigned char>(word[i])
                ) << ((i + 1) * 8);
            }
            code.push_back(op);
            return;
        }
        code.push_back(BC_INST_VAL | BC_RET_STRING | (word.size() << 8));
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
        code.push_back(BC_INST_VAL_INT | BC_RET_STRING);
    }

    void gen_null() {
        code.push_back(BC_INST_VAL_INT | BC_RET_NULL);
    }

    void gen_int(integer_type i = 0) {
        if (i >= -0x800000 && i <= 0x7FFFFF) {
            code.push_back(BC_INST_VAL_INT | BC_RET_INT | (i << 8));
        } else {
            union {
                integer_type i;
                uint32_t u[CsTypeStorageSize<integer_type>];
            } c;
            c.i = i;
            code.push_back(BC_INST_VAL | BC_RET_INT);
            code.append(c.u, c.u + CsTypeStorageSize<integer_type>);
        }
    }

    void gen_int(std::string_view word);

    void gen_float(float_type f = 0.0f) {
        if (integer_type(f) == f && f >= -0x800000 && f <= 0x7FFFFF) {
            code.push_back(BC_INST_VAL_INT | BC_RET_FLOAT | (integer_type(f) << 8));
        } else {
            union {
                float_type f;
                uint32_t u[CsTypeStorageSize<float_type>];
            } c;
            c.f = f;
            code.push_back(BC_INST_VAL | BC_RET_FLOAT);
            code.append(c.u, c.u + CsTypeStorageSize<float_type>);
        }
    }

    void gen_float(std::string_view word);

    void gen_ident(ident *id) {
        code.push_back(
            ((id->get_index() < MaxArguments)
                ? BC_INST_IDENT_ARG
                : BC_INST_IDENT
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

template<typename F>
static void call_with_args(state &cs, F body) {
    if (!cs.p_callstack) {
        body();
        return;
    }
    valarray<ident_stack, MaxArguments> argstack{cs};
    int argmask1 = cs.p_callstack->usedargs;
    for (int i = 0; argmask1; argmask1 >>= 1, ++i) {
        if (argmask1 & 1) {
            static_cast<alias_impl *>(cs.p_state->identmap[i])->undo_arg(
                argstack[i]
            );
        }
    }
    ident_link *prevstack = cs.p_callstack->next;
    ident_link aliaslink = {
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
                static_cast<alias_impl *>(cs.p_state->identmap[i])->redo_arg(
                    argstack[i]
                );
            }
        }
    });
}

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_VM_HH */
