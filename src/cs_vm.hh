#ifndef LIBCUBESCRIPT_CS_VM_HH
#define LIBCUBESCRIPT_CS_VM_HH

#include "cubescript/cubescript.hh"

#include <cstdlib>
#include <array>
#include <vector>
#include <type_traits>

#include "cs_util.hh"
#include "cs_bcode.hh"

namespace cscript {

static constexpr int MaxArguments = 25;
static constexpr int MaxResults = 7;

static constexpr int DummyIdx = MaxArguments;
static constexpr int NumargsIdx = MaxArguments + 1;
static constexpr int DbgaliasIdx = MaxArguments + 2;

enum {
    CsIdUnknown = -1, CsIdIvar, CsIdFvar, CsIdSvar, CsIdCommand, CsIdAlias,
    CsIdLocal, CsIdDo, CsIdDoArgs, CsIdIf, CsIdBreak, CsIdContinue, CsIdResult,
    CsIdNot, CsIdAnd, CsIdOr
};

struct cs_ident_impl {
    cs_ident_impl() = delete;
    cs_ident_impl(cs_ident_impl const &) = delete;
    cs_ident_impl(cs_ident_impl &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~cs_ident_impl() {};

    cs_ident_impl &operator=(cs_ident_impl const &) = delete;
    cs_ident_impl &operator=(cs_ident_impl &&) = delete;

    cs_ident_impl(cs_ident_type tp, cs_strref name, int flags = 0);

    cs_strref p_name;
    /* represents the cs_ident_type above, but internally it has a wider variety
     * of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

    int p_index = -1;
};

struct cs_var_impl: cs_ident_impl {
    cs_var_impl(cs_ident_type tp, cs_strref name, cs_var_cb func, int flags = 0);

    cs_var_cb cb_var;

    void changed(cs_state &cs);
};

struct cs_ivar_impl: cs_var_impl, cs_ivar {
    cs_ivar_impl(
        cs_strref n, cs_int m, cs_int x, cs_int v, cs_var_cb f, int flags
    );

    cs_int p_storage, p_minval, p_maxval, p_overrideval;
};

struct cs_fvar_impl: cs_var_impl, cs_fvar {
    cs_fvar_impl(
        cs_strref n, cs_float m, cs_float x, cs_float v,
        cs_var_cb f, int flags
    );

    cs_float p_storage, p_minval, p_maxval, p_overrideval;
};

struct cs_svar_impl: cs_var_impl, cs_svar {
    cs_svar_impl(cs_strref n, cs_strref v, cs_strref ov, cs_var_cb f, int flags);

    cs_strref p_storage, p_overrideval;
};

struct cs_ident_link {
    cs_ident *id;
    cs_ident_link *next;
    int usedargs;
    cs_ident_stack *argstack;
};

static inline bool cs_is_arg_used(cs_state &cs, cs_ident *id) {
    if (!cs.p_callstack) {
        return true;
    }
    return cs.p_callstack->usedargs & (1 << id->get_index());
}

struct cs_alias_impl: cs_ident_impl, cs_alias {
    cs_alias_impl(cs_state &cs, cs_strref n, cs_strref a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, std::string_view a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_int a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_float a, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, int flags);
    cs_alias_impl(cs_state &cs, cs_strref n, cs_value v, int flags);

    void push_arg(cs_value &v, cs_ident_stack &st, bool um = true) {
        if (p_astack == &st) {
            /* prevent cycles and unnecessary code elsewhere */
            p_val = std::move(v);
            clean_code();
            return;
        }
        st.val_s = std::move(p_val);
        st.next = p_astack;
        p_astack = &st;
        p_val = std::move(v);
        clean_code();
        if (um) {
            p_flags &= ~CS_IDF_UNKNOWN;
        }
    }

    void pop_arg() {
        if (!p_astack) {
            return;
        }
        cs_ident_stack *st = p_astack;
        p_val = std::move(p_astack->val_s);
        clean_code();
        p_astack = st->next;
    }

    void undo_arg(cs_ident_stack &st) {
        cs_ident_stack *prev = p_astack;
        st.val_s = std::move(p_val);
        st.next = prev;
        p_astack = prev->next;
        p_val = std::move(prev->val_s);
        clean_code();
    }

    void redo_arg(cs_ident_stack &st) {
        cs_ident_stack *prev = st.next;
        prev->val_s = std::move(p_val);
        p_astack = prev;
        p_val = std::move(st.val_s);
        clean_code();
    }

    void set_arg(cs_state &cs, cs_value &v) {
        if (cs_is_arg_used(cs, this)) {
            p_val = std::move(v);
            clean_code();
        } else {
            push_arg(v, cs.p_callstack->argstack[get_index()], false);
            cs.p_callstack->usedargs |= 1 << get_index();
        }
    }

    void set_alias(cs_state &cs, cs_value &v) {
        p_val = std::move(v);
        clean_code();
        p_flags = (p_flags & cs.identflags) | cs.identflags;
    }

    void clean_code();
    cs_bcode *compile_code(cs_state &cs);

    cs_bcode *p_acode;
    cs_ident_stack *p_astack;
    cs_value p_val;
};

struct cs_command_impl: cs_ident_impl, cs_command {
    cs_command_impl(cs_strref name, cs_strref args, int numargs, cs_command_cb func);

    void call(cs_state &cs, std::span<cs_value> args, cs_value &ret) {
        p_cb_cftv(cs, args, ret);
    }

    cs_strref p_cargs;
    cs_command_cb p_cb_cftv;
    int p_numargs;
};

template<typename T, std::size_t N>
struct cs_valarray {
    cs_valarray(cs_state &cs) {
        for (std::size_t i = 0; i < N; ++i) {
            new (&stor[i]) T{cs};
        }
    }

    ~cs_valarray() {
        for (std::size_t i = 0; i < N; ++i) {
            reinterpret_cast<T *>(&stor[i])->~T();
        }
    }

    T &operator[](std::size_t i) {
        return *reinterpret_cast<T *>(&stor[i]);
    }

    std::aligned_storage_t<sizeof(T), alignof(T)> stor[N];
};

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

bool cs_check_num(std::string_view s);

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
    cs_do_and_cleanup(std::move(body), [&]() {
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
