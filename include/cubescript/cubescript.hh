#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <stdio.h>
#include <stdlib.h>

#include "cubescript_conf.hh"

#include <ostd/platform.hh>
#include <ostd/types.hh>
#include <ostd/string.hh>
#include <ostd/vector.hh>
#include <ostd/map.hh>
#include <ostd/range.hh>
#include <ostd/utility.hh>
#include <ostd/maybe.hh>
#include <ostd/io.hh>
#include <ostd/functional.hh>
#include <ostd/format.hh>

namespace cscript {

enum {
    CsIdfPersist    = 1 << 0,
    CsIdfOverride   = 1 << 1,
    CsIdfHex        = 1 << 2,
    CsIdfReadOnly   = 1 << 3,
    CsIdfOverridden = 1 << 4,
    CsIdfUnknown    = 1 << 5,
    CsIdfArg        = 1 << 6
};

struct CsBytecode;

struct OSTD_EXPORT CsBytecodeRef {
    CsBytecodeRef():
        p_code(nullptr)
    {}
    CsBytecodeRef(CsBytecode *v);
    CsBytecodeRef(CsBytecodeRef const &v);
    CsBytecodeRef(CsBytecodeRef &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~CsBytecodeRef();

    CsBytecodeRef &operator=(CsBytecodeRef const &v);
    CsBytecodeRef &operator=(CsBytecodeRef &&v);

    operator bool() const { return p_code != nullptr; }
    operator CsBytecode *() const { return p_code; }

private:
    CsBytecode *p_code;
};

OSTD_EXPORT bool cs_code_is_empty(CsBytecode *code);

struct CsIdent;

enum class CsValueType {
    Null = 0, Int, Float, String, Cstring, Code, Macro, Ident
};

struct OSTD_EXPORT CsValue {
    CsValue();
    ~CsValue();

    CsValue(CsValue const &);
    CsValue(CsValue &&);

    CsValue &operator=(CsValue const &v);
    CsValue &operator=(CsValue &&v);

    CsValueType get_type() const;

    void set_int(CsInt val);
    void set_float(CsFloat val);
    void set_str(CsString val);
    void set_null();
    void set_code(CsBytecode *val);
    void set_cstr(ostd::ConstCharRange val);
    void set_mstr(ostd::CharRange val);
    void set_ident(CsIdent *val);
    void set_macro(ostd::ConstCharRange val);

    CsString get_str() const;
    ostd::ConstCharRange get_strr() const;
    CsInt get_int() const;
    CsFloat get_float() const;
    CsBytecode *get_code() const;
    CsIdent *get_ident() const;
    void get_val(CsValue &r) const;

    bool get_bool() const;

    void force_null();
    CsFloat force_float();
    CsInt force_int();
    ostd::ConstCharRange force_str();

    bool code_is_empty() const;

private:
    ostd::AlignedUnion<1, CsInt, CsFloat, void *> p_stor;
    ostd::Size p_len;
    CsValueType p_type;
};

using CsValueRange = ostd::PointerRange<CsValue>;

struct CsIdentStack {
    CsValue val_s;
    CsIdentStack *next;
};

struct CsState;

enum class CsIdentType {
    Ivar = 0, Fvar, Svar, Command, Alias, Special
};

struct CsVar;
struct CsIvar;
struct CsFvar;
struct CsSvar;
struct CsAlias;
struct CsCommand;

struct OSTD_EXPORT CsIdent {
    friend struct CsState;

    CsIdent() = delete;
    CsIdent(CsIdent const &) = delete;
    CsIdent(CsIdent &&) = delete;

    /* trigger destructors for all inherited members properly */
    virtual ~CsIdent() {};

    CsIdent &operator=(CsIdent const &) = delete;
    CsIdent &operator=(CsIdent &&) = delete;

    CsIdentType get_type() const;
    ostd::ConstCharRange get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    CsAlias *get_alias();
    CsAlias const *get_alias() const;

    bool is_command() const;
    CsCommand *get_command();
    CsCommand const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    CsVar *get_var();
    CsVar const *get_var() const;

    bool is_ivar() const;
    CsIvar *get_ivar();
    CsIvar const *get_ivar() const;

    bool is_fvar() const;
    CsFvar *get_fvar();
    CsFvar const *get_fvar() const;

    bool is_svar() const;
    CsSvar *get_svar();
    CsSvar const *get_svar() const;

    int get_type_raw() const {
        return p_type;
    }

protected:
    CsIdent(CsIdentType tp, ostd::ConstCharRange name, int flags = 0);

    CsString p_name;
    /* represents the CsIdentType above, but internally it has a wider variety
     * of values, so it's an int here (maps to an internal enum)
     */
    int p_type, p_flags;

private:
    int p_index = -1;
};

using CsIdentRange = ostd::PointerRange<CsIdent *>;
using CsConstIdentRange = ostd::PointerRange<CsIdent const *>;

using CsVarCb = ostd::Function<void(CsState &, CsIdent &)>;

struct OSTD_EXPORT CsVar: CsIdent {
    friend struct CsState;

protected:
    CsVar(CsIdentType tp, ostd::ConstCharRange name, CsVarCb func, int flags = 0);

private:
    CsVarCb cb_var;

    void changed(CsState &cs) {
        if (cb_var) {
            cb_var(cs, *this);
        }
    }
};

struct OSTD_EXPORT CsIvar: CsVar {
    friend struct CsState;

    CsInt get_val_min() const;
    CsInt get_val_max() const;

    CsInt get_value() const;
    void set_value(CsInt val);

private:
    CsIvar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt v, CsVarCb f, int flags
    );

    CsInt p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT CsFvar: CsVar {
    friend struct CsState;

    CsFloat get_val_min() const;
    CsFloat get_val_max() const;

    CsFloat get_value() const;
    void set_value(CsFloat val);

private:
    CsFvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat v,
        CsVarCb f, int flags
    );

    CsFloat p_storage, p_minval, p_maxval, p_overrideval;
};

struct OSTD_EXPORT CsSvar: CsVar {
    friend struct CsState;

    ostd::ConstCharRange get_value() const;
    void set_value(CsString val);

private:
    CsSvar(ostd::ConstCharRange n, CsString v, CsVarCb f, int flags);

    CsString p_storage, p_overrideval;
};

struct OSTD_EXPORT CsAlias: CsIdent {
    friend struct CsState;
    friend struct CsAliasInternal;

    CsValue const &get_value() const {
        return p_val;
    }

    CsValue &get_value() {
        return p_val;
    }

    void get_cstr(CsValue &v) const;
    void get_cval(CsValue &v) const;
private:
    CsAlias(ostd::ConstCharRange n, char *a, int flags);
    CsAlias(ostd::ConstCharRange n, CsInt a, int flags);
    CsAlias(ostd::ConstCharRange n, CsFloat a, int flags);
    CsAlias(ostd::ConstCharRange n, int flags);
    CsAlias(ostd::ConstCharRange n, CsValue v, int flags);

    CsBytecode *p_acode;
    CsIdentStack *p_astack;
    CsValue p_val;
};

using CsCommandCb = ostd::Function<void(CsState &, CsValueRange, CsValue &)>;

struct CsCommand: CsIdent {
    friend struct CsState;
    friend struct CsCommandInternal;

    ostd::ConstCharRange get_args() const;
    int get_num_args() const;

private:
    CsCommand(
        ostd::ConstCharRange name, ostd::ConstCharRange args,
        int numargs, CsCommandCb func
    );

    CsString p_cargs;
    CsCommandCb p_cb_cftv;
    int p_numargs;
};

struct CsIdentLink;

enum {
    CsLibMath   = 1 << 0,
    CsLibString = 1 << 1,
    CsLibList   = 1 << 2,
    CsLibAll    = 0b111
};

using CsHookCb = ostd::Function<void(CsState &)>;
using CsAllocCb = void *(*)(void *, void *, ostd::Size, ostd::Size);

template<typename T>
struct CsAllocator {
    template<typename TT>
    friend struct CsAllocator;

    using Value = T;
    static constexpr bool PropagateOnContainerCopyAssignment = true;
    static constexpr bool PropagateOnContainerMoveAssignment = true;
    static constexpr bool PropagateOnContainerSwap = true;

    CsAllocator() = delete;
    CsAllocator(CsAllocator const &a) noexcept: p_state(a.p_state) {}
    CsAllocator(CsState &cs) noexcept: p_state(cs) {}

    template<typename TT>
    CsAllocator(CsAllocator<TT> const &a) noexcept: p_state(a.p_state) {}

    T *allocate(ostd::Size n, void const * = nullptr);
    void deallocate(T *p, ostd::Size n) noexcept;

    bool operator==(CsAllocator const &o) const noexcept {
        return &p_state != &o.p_state;
    }

    bool operator!=(CsAllocator const &o) const noexcept {
        return &p_state != &o.p_state;
    }

private:
    CsState &p_state;
};

struct CsErrorException;
struct CsSharedState;

enum class CsLoopState {
    Normal = 0, Break, Continue
};

struct OSTD_EXPORT CsState {
    friend struct CsErrorException;

    CsSharedState *p_state;
    CsIdentLink *p_callstack = nullptr;

    int identflags = 0;

    CsState(CsAllocCb func = nullptr, void *data = nullptr);
    virtual ~CsState();

    CsStream const &get_out() const;
    CsStream &get_out();
    void set_out(CsStream &s);

    CsStream const &get_err() const;
    CsStream &get_err();
    void set_err(CsStream &s);

    CsHookCb set_call_hook(CsHookCb func);
    CsHookCb const &get_call_hook() const;
    CsHookCb &get_call_hook();

    template<typename F>
    CsHookCb set_call_hook(F &&f) {
        return set_call_hook(CsHookCb(
            ostd::allocator_arg, CsAllocator<char>(*this), ostd::forward<F>(f)
        ));
    }

    void *alloc(void *ptr, ostd::Size olds, ostd::Size news);

    template<typename T, typename ...A>
    T *create(A &&...args) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, sizeof(T)));
        new (ret) T(ostd::forward<A>(args)...);
        return ret;
    }

    template<typename T>
    T *create_array(ostd::Size len) {
        T *ret = static_cast<T *>(alloc(nullptr, 0, len * sizeof(T)));
        for (ostd::Size i = 0; i < len; ++i) {
            new (&ret[i]) T();
        }
        return ret;
    }

    template<typename T>
    void destroy(T *v) noexcept {
        v->~T();
        alloc(v, sizeof(T), 0);
    }

    template<typename T>
    void destroy_array(T *v, ostd::Size len) noexcept {
        v->~T();
        alloc(v, len * sizeof(T), 0);
    }

    void init_libs(int libs = CsLibAll);

    void clear_override(CsIdent &id);
    void clear_overrides();

    CsIdent *new_ident(ostd::ConstCharRange name, int flags = CsIdfUnknown);
    CsIdent *force_ident(CsValue &v);

    CsIvar *new_ivar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt v,
        CsVarCb f = CsVarCb(), int flags = 0
    );
    CsFvar *new_fvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat v,
        CsVarCb f = CsVarCb(), int flags = 0
    );
    CsSvar *new_svar(
        ostd::ConstCharRange n, CsString v,
        CsVarCb f = CsVarCb(), int flags = 0
    );

    template<typename F>
    CsIvar *new_ivar(
        ostd::ConstCharRange n, CsInt m, CsInt x, CsInt v, F &&f, int flags = 0
    ) {
        return new_ivar(n, m, x, v, CsVarCb(
            ostd::allocator_arg, CsAllocator<char>(*this), ostd::forward<F>(f)
        ), flags);
    }
    template<typename F>
    CsFvar *new_fvar(
        ostd::ConstCharRange n, CsFloat m, CsFloat x, CsFloat v, F &&f,
        int flags = 0
    ) {
        return new_fvar(n, m, x, v, CsVarCb(
            ostd::allocator_arg, CsAllocator<char>(*this), ostd::forward<F>(f)
        ), flags);
    }
    template<typename F>
    CsSvar *new_svar(
        ostd::ConstCharRange n, CsString v, F &&f, int flags = 0
    ) {
        return new_svar(n, ostd::move(v), CsVarCb(
            ostd::allocator_arg, CsAllocator<char>(*this), ostd::forward<F>(f)
        ), flags);
    }

    CsCommand *new_command(
        ostd::ConstCharRange name, ostd::ConstCharRange args, CsCommandCb func
    );

    template<typename F>
    CsCommand *new_command(
        ostd::ConstCharRange name, ostd::ConstCharRange args, F &&f
    ) {
        return new_command(name, args, CsCommandCb(
            ostd::allocator_arg, CsAllocator<char>(*this), ostd::forward<F>(f)
        ));
    }

    CsIdent *get_ident(ostd::ConstCharRange name);
    CsAlias *get_alias(ostd::ConstCharRange name);
    bool have_ident(ostd::ConstCharRange name);

    CsIdentRange get_idents();
    CsConstIdentRange get_idents() const;

    bool reset_var(ostd::ConstCharRange name);
    void touch_var(ostd::ConstCharRange name);

    CsString run_str(CsBytecode *code);
    CsString run_str(ostd::ConstCharRange code);
    CsString run_str(CsIdent *id, CsValueRange args);

    CsInt run_int(CsBytecode *code);
    CsInt run_int(ostd::ConstCharRange code);
    CsInt run_int(CsIdent *id, CsValueRange args);

    CsFloat run_float(CsBytecode *code);
    CsFloat run_float(ostd::ConstCharRange code);
    CsFloat run_float(CsIdent *id, CsValueRange args);

    bool run_bool(CsBytecode *code);
    bool run_bool(ostd::ConstCharRange code);
    bool run_bool(CsIdent *id, CsValueRange args);

    void run(CsBytecode *code, CsValue &ret);
    void run(ostd::ConstCharRange code, CsValue &ret);
    void run(CsIdent *id, CsValueRange args, CsValue &ret);

    void run(CsBytecode *code);
    void run(ostd::ConstCharRange code);
    void run(CsIdent *id, CsValueRange args);

    CsLoopState run_loop(CsBytecode *code, CsValue &ret);
    CsLoopState run_loop(CsBytecode *code);

    bool is_in_loop() const {
        return p_inloop;
    }

    ostd::Maybe<CsString> run_file_str(ostd::ConstCharRange fname);
    ostd::Maybe<CsInt> run_file_int(ostd::ConstCharRange fname);
    ostd::Maybe<CsFloat> run_file_float(ostd::ConstCharRange fname);
    ostd::Maybe<bool> run_file_bool(ostd::ConstCharRange fname);
    bool run_file(ostd::ConstCharRange fname, CsValue &ret);
    bool run_file(ostd::ConstCharRange fname);

    void set_alias(ostd::ConstCharRange name, CsValue v);

    void set_var_int(
        ostd::ConstCharRange name, CsInt v,
        bool dofunc = true, bool doclamp = true
    );
    void set_var_float(
        ostd::ConstCharRange name, CsFloat v,
        bool dofunc  = true, bool doclamp = true
    );
    void set_var_str(
        ostd::ConstCharRange name, ostd::ConstCharRange v, bool dofunc = true
    );

    void set_var_int_checked(CsIvar *iv, CsInt v);
    void set_var_int_checked(CsIvar *iv, CsValueRange args);
    void set_var_float_checked(CsFvar *fv, CsFloat v);
    void set_var_str_checked(CsSvar *fv, ostd::ConstCharRange v);

    ostd::Maybe<CsInt> get_var_int(ostd::ConstCharRange name);
    ostd::Maybe<CsFloat> get_var_float(ostd::ConstCharRange name);
    ostd::Maybe<CsString> get_var_str(ostd::ConstCharRange name);

    ostd::Maybe<CsInt> get_var_min_int(ostd::ConstCharRange name);
    ostd::Maybe<CsInt> get_var_max_int(ostd::ConstCharRange name);

    ostd::Maybe<CsFloat> get_var_min_float(ostd::ConstCharRange name);
    ostd::Maybe<CsFloat> get_var_max_float(ostd::ConstCharRange name);

    ostd::Maybe<CsString> get_alias_val(ostd::ConstCharRange name);

    void print_var(CsVar *v);
    virtual void print_var(CsIvar *iv);
    virtual void print_var(CsFvar *fv);
    virtual void print_var(CsSvar *sv);

private:
    CsIdent *add_ident(CsIdent *id);

    int p_inloop = 0;

    CsAllocCb p_allocf;
    void *p_aptr;

    char p_errbuf[512];

    CsHookCb p_callhook;

    CsStream *p_out, *p_err;
};

struct CsStackStateNode {
    CsStackStateNode const *next;
    CsIdent const *id;
    int index;
};

struct CsStackState {
    CsStackState() = delete;
    CsStackState(CsState &cs, CsStackStateNode *nd = nullptr, bool gap = false);
    CsStackState(CsStackState const &) = delete;
    CsStackState(CsStackState &&st);
    ~CsStackState();

    CsStackState &operator=(CsStackState const &) = delete;
    CsStackState &operator=(CsStackState &&);

    CsStackStateNode const *get() const;
    bool gap() const;

private:
    CsState &p_state;
    CsStackStateNode *p_node;
    bool p_gap;
};

struct CsErrorException {
    friend struct CsState;

    CsErrorException() = delete;
    CsErrorException(CsErrorException const &) = delete;
    CsErrorException(CsErrorException &&v):
        p_errmsg(v.p_errmsg), p_stack(ostd::move(v.p_stack))
    {}

    ostd::ConstCharRange what() const {
        return p_errmsg;
    }

    CsStackState &get_stack() {
        return p_stack;
    }

    CsStackState const &get_stack() const {
        return p_stack;
    }

    CsErrorException(CsState &cs, ostd::ConstCharRange msg):
        p_errmsg(), p_stack(cs)
    {
        p_errmsg = save_msg(cs, msg);
        p_stack = save_stack(cs);
    }

    template<typename ...A>
    CsErrorException(CsState &cs, ostd::ConstCharRange msg, A &&...args):
        p_errmsg(), p_stack(cs)
    {
        char fbuf[512];
        auto ret = ostd::format(
            ostd::CharRange(fbuf, sizeof(fbuf)), msg, ostd::forward<A>(args)...
        );
        if ((ret < 0) || (ostd::Size(ret) > sizeof(fbuf))) {
            p_errmsg = save_msg(cs, msg);
        } else {
            p_errmsg = save_msg(cs, ostd::CharRange(fbuf, ret));
        }
        p_stack = save_stack(cs);
    }

private:
    CsStackState save_stack(CsState &cs);
    ostd::ConstCharRange save_msg(CsState &cs, ostd::ConstCharRange v);

    ostd::ConstCharRange p_errmsg;
    CsStackState p_stack;
};

struct OSTD_EXPORT CsStackedValue: CsValue {
    CsStackedValue(CsIdent *id = nullptr);
    ~CsStackedValue();

    CsStackedValue(CsStackedValue const &) = delete;
    CsStackedValue(CsStackedValue &&) = delete;

    CsStackedValue &operator=(CsStackedValue const &) = delete;
    CsStackedValue &operator=(CsStackedValue &&v) = delete;

    CsStackedValue &operator=(CsValue const &v);
    CsStackedValue &operator=(CsValue &&v);

    bool set_alias(CsIdent *id);
    CsAlias *get_alias() const;
    bool has_alias() const;

    bool push();
    bool pop();

private:
    CsAlias *p_a;
    CsIdentStack p_stack;
    bool p_pushed;
};

template<typename T>
T *CsAllocator<T>::allocate(ostd::Size n, void const *) {
    return static_cast<T *>(p_state.alloc(nullptr, 0, n * sizeof(T)));
}

template<typename T>
void CsAllocator<T>::deallocate(T *p, ostd::Size n) noexcept {
    p_state.alloc(p, n * sizeof(T), 0);
}

namespace util {
    template<typename R>
    inline ostd::Size escape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 2;
        writer.put('"');
        for (; !str.empty(); str.pop_front()) {
            switch (str.front()) {
                case '\n':
                    ret += writer.put_n("^n", 2);
                    break;
                case '\t':
                    ret += writer.put_n("^t", 2);
                    break;
                case '\f':
                    ret += writer.put_n("^f", 2);
                    break;
                case '"':
                    ret += writer.put_n("^\"", 2);
                    break;
                case '^':
                    ret += writer.put_n("^^", 2);
                    break;
                default:
                    ret += writer.put(str.front());
                    break;
            }
        }
        writer.put('"');
        return ret;
    }

    template<typename R>
    inline ostd::Size unescape_string(R &&writer, ostd::ConstCharRange str) {
        ostd::Size ret = 0;
        for (; !str.empty(); str.pop_front()) {
            if (str.front() == '^') {
                str.pop_front();
                if (str.empty()) {
                    break;
                }
                switch (str.front()) {
                    case 'n':
                        ret += writer.put('\n');
                        break;
                    case 't':
                        ret += writer.put('\r');
                        break;
                    case 'f':
                        ret += writer.put('\f');
                        break;
                    case '"':
                        ret += writer.put('"');
                        break;
                    case '^':
                        ret += writer.put('^');
                        break;
                    default:
                        ret += writer.put(str.front());
                        break;
                }
            } else {
                ret += writer.put(str.front());
            }
        }
        return ret;
    }

    struct ListParser {
        ostd::ConstCharRange input;
        ostd::ConstCharRange quote = ostd::ConstCharRange();
        ostd::ConstCharRange item = ostd::ConstCharRange();

        ListParser() = delete;
        ListParser(ostd::ConstCharRange src): input(src) {}

        void skip();
        bool parse();

        CsString element();
    };

    ostd::Size list_length(ostd::ConstCharRange s);
    ostd::Maybe<CsString> list_index(
        ostd::ConstCharRange s, ostd::Size idx
    );
    CsVector<CsString> list_explode(
        ostd::ConstCharRange s, ostd::Size limit = -1
    );

    template<typename R>
    inline ostd::Ptrdiff format_int(R &&writer, CsInt val) {
        return ostd::format(ostd::forward<R>(writer), IntFormat, val);
    }

    template<typename R>
    inline ostd::Ptrdiff format_float(R &&writer, CsFloat val) {
        return ostd::format(
            ostd::forward<R>(writer),
            (val == CsInt(val)) ? RoundFloatFormat : FloatFormat, val
        );
    }

    template<typename R>
    inline ostd::Size tvals_concat(
        R &&writer, CsValueRange vals,
        ostd::ConstCharRange sep = ostd::ConstCharRange()
    ) {
        ostd::Size ret = 0;
        for (ostd::Size i = 0; i < vals.size(); ++i) {
            auto s = ostd::appender<CsString>();
            switch (vals[i].get_type()) {
                case CsValueType::Int: {
                    auto r = format_int(
                        ostd::forward<R>(writer), vals[i].get_int()
                    );
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case CsValueType::Float: {
                    auto r = format_float(
                        ostd::forward<R>(writer), vals[i].get_float()
                    );
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case CsValueType::String:
                case CsValueType::Cstring:
                case CsValueType::Macro: {
                    auto sv = vals[i].get_strr();
                    ret += writer.put_n(sv.data(), sv.size());
                    break;
                }
                default:
                    break;
            }
            if (i == (vals.size() - 1)) {
                break;
            }
            ret += writer.put_n(sep.data(), sep.size());
        }
        return ret;
    }

    template<typename R>
    inline ostd::Size print_stack(R &&writer, CsStackState const &st) {
        ostd::Size ret = 0;
        auto nd = st.get();
        while (nd) {
            auto rt = ostd::format(
                writer,
                ((nd->index == 1) && st.gap())
                    ? "  ..%d) %s" : "  %d) %s",
                nd->index, nd->id->get_name()
            );
            if (rt > 0) {
                ret += ostd::Size(rt);
            } else {
                return ret;
            }
            nd = nd->next;
            if (nd) {
                ret += writer.put('\n');
            }
        }
        return ret;
    }
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
