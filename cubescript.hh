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
    IDF_PERSIST    = 1 << 0,
    IDF_OVERRIDE   = 1 << 1,
    IDF_HEX        = 1 << 2,
    IDF_READONLY   = 1 << 3,
    IDF_OVERRIDDEN = 1 << 4,
    IDF_UNKNOWN    = 1 << 5,
    IDF_ARG        = 1 << 6
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
    null = 0, integer, number, string, cstring, code, macro, ident
};

struct OSTD_EXPORT CsValue {
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

    void set(CsValue &tv);

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

    void cleanup();
    void copy_arg(CsValue &r) const;

private:
    union {
        CsInt p_i;
        CsFloat p_f;
        CsBytecode *p_code;
        CsIdent *p_id;
        char *p_s;
        char const *p_cstr;
    };
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
    ivar = 0, fvar, svar, command, alias, special
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

using CsVarCb = ostd::Function<void(CsIdent &)>;

struct OSTD_EXPORT CsVar: CsIdent {
    friend struct CsState;

protected:
    CsVar(CsIdentType tp, ostd::ConstCharRange name, CsVarCb func, int flags = 0);

private:
    CsVarCb cb_var;

    void changed() {
        if (cb_var) {
            cb_var(*this);
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
    CsAlias(ostd::ConstCharRange n, CsValue const &v, int flags);

    CsBytecode *p_acode;
    CsIdentStack *p_astack;
    CsValue p_val;
};

using CsCommandCb = ostd::Function<void(CsValueRange, CsValue &)>;

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

struct CsIdentLink {
    CsIdent *id;
    CsIdentLink *next;
    int usedargs;
    CsIdentStack *argstack;
};

enum {
    CS_LIB_MATH   = 1 << 0,
    CS_LIB_STRING = 1 << 1,
    CS_LIB_LIST   = 1 << 2,
    CS_LIB_ALL    = 0b111
};

struct OSTD_EXPORT CsState {
    CsMap<ostd::ConstCharRange, CsIdent *> idents;
    CsVector<CsIdent *> identmap;

    CsIdentLink noalias;
    CsIdentLink *p_stack = &noalias;

    int identflags = 0;
    int nodebug = 0;

    CsState();
    ~CsState();

    CsStream const &get_out() const;
    CsStream &get_out();
    void set_out(CsStream &s);

    CsStream const &get_err() const;
    CsStream &get_err();
    void set_err(CsStream &s);

    void init_libs(int libs = CS_LIB_ALL);

    void clear_override(CsIdent &id);
    void clear_overrides();

    CsIdent *new_ident(ostd::ConstCharRange name, int flags = IDF_UNKNOWN);
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

    CsCommand *new_command(
        ostd::ConstCharRange name, ostd::ConstCharRange args, CsCommandCb func
    );

    CsIdent *get_ident(ostd::ConstCharRange name) {
        CsIdent **id = idents.at(name);
        if (!id) {
            return nullptr;
        }
        return *id;
    }

    CsAlias *get_alias(ostd::ConstCharRange name) {
        CsIdent *id = get_ident(name);
        if (!id->is_alias()) {
            return nullptr;
        }
        return static_cast<CsAlias *>(id);
    }

    bool have_ident(ostd::ConstCharRange name) {
        return idents.at(name) != nullptr;
    }

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

    void run_ret(CsBytecode *code, CsValue &ret);
    void run_ret(ostd::ConstCharRange code, CsValue &ret);
    void run_ret(CsIdent *id, CsValueRange args, CsValue &ret);

    void run(CsBytecode *code);
    void run(ostd::ConstCharRange code);
    void run(CsIdent *id, CsValueRange args);

    ostd::Maybe<CsString> run_file_str(ostd::ConstCharRange fname);
    ostd::Maybe<CsInt> run_file_int(ostd::ConstCharRange fname);
    ostd::Maybe<CsFloat> run_file_float(ostd::ConstCharRange fname);
    ostd::Maybe<bool> run_file_bool(ostd::ConstCharRange fname);
    bool run_file_ret(ostd::ConstCharRange fname, CsValue &ret);
    bool run_file(ostd::ConstCharRange fname);

    void set_alias(ostd::ConstCharRange name, CsValue &v);

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

    CsStream *p_out, *p_err;
};

struct OSTD_EXPORT CsStackedValue: CsValue {
    CsStackedValue(CsIdent *id = nullptr);
    ~CsStackedValue();

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
                case CsValueType::integer: {
                    auto r = format_int(
                        ostd::forward<R>(writer), vals[i].get_int()
                    );
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case CsValueType::number: {
                    auto r = format_float(
                        ostd::forward<R>(writer), vals[i].get_float()
                    );
                    if (r > 0) {
                        ret += ostd::Size(r);
                    }
                    break;
                }
                case CsValueType::string:
                case CsValueType::cstring:
                case CsValueType::macro: {
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
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
