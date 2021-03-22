#include "cs_ident.hh"

#include "cs_bcode.hh"
#include "cs_vm.hh"

namespace cscript {

void cs_var_impl::changed(cs_state &cs) {
    if (cb_var) {
        switch (p_type) {
            case ID_IVAR:
                cb_var(cs, *static_cast<cs_ivar_impl *>(this));
                break;
            case ID_FVAR:
                cb_var(cs, *static_cast<cs_fvar_impl *>(this));
                break;
            case ID_SVAR:
                cb_var(cs, *static_cast<cs_svar_impl *>(this));
                break;
            default:
                break;
        }
    }
}

void cs_alias_impl::push_arg(cs_value &v, cs_ident_stack &st, bool um) {
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

void cs_alias_impl::pop_arg() {
    if (!p_astack) {
        return;
    }
    cs_ident_stack *st = p_astack;
    p_val = std::move(p_astack->val_s);
    clean_code();
    p_astack = st->next;
}

void cs_alias_impl::undo_arg(cs_ident_stack &st) {
    cs_ident_stack *prev = p_astack;
    st.val_s = std::move(p_val);
    st.next = prev;
    p_astack = prev->next;
    p_val = std::move(prev->val_s);
    clean_code();
}

void cs_alias_impl::redo_arg(cs_ident_stack &st) {
    cs_ident_stack *prev = st.next;
    prev->val_s = std::move(p_val);
    p_astack = prev;
    p_val = std::move(st.val_s);
    clean_code();
}

void cs_alias_impl::set_arg(cs_state &cs, cs_value &v) {
    if (ident_is_used_arg(this, cs)) {
        p_val = std::move(v);
        clean_code();
    } else {
        push_arg(v, cs.p_callstack->argstack[get_index()], false);
        cs.p_callstack->usedargs |= 1 << get_index();
    }
}

void cs_alias_impl::set_alias(cs_state &cs, cs_value &v) {
    p_val = std::move(v);
    clean_code();
    p_flags = (p_flags & cs.identflags) | cs.identflags;
}

void cs_alias_impl::clean_code() {
    if (p_acode) {
        bcode_decr(p_acode->get_raw());
        p_acode = nullptr;
    }
}

cs_bcode *cs_alias_impl::compile_code(cs_state &cs) {
    if (!p_acode) {
        cs_gen_state gs(cs);
        gs.code.reserve(64);
        gs.gen_main(get_value().get_str());
        /* i wish i could steal the memory somehow */
        uint32_t *code = bcode_alloc(cs, gs.code.size());
        memcpy(code, gs.code.data(), gs.code.size() * sizeof(uint32_t));
        bcode_incr(code);
        p_acode = reinterpret_cast<cs_bcode *>(code);
    }
    return p_acode;
}

bool ident_is_used_arg(cs_ident *id, cs_state &cs) {
    if (!cs.p_callstack) {
        return true;
    }
    return cs.p_callstack->usedargs & (1 << id->get_index());
}

} /* namespace cscript */
