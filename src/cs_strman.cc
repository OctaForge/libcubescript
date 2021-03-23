#include <cubescript/cubescript.hh>

#include "cs_strman.hh"

namespace cscript {

struct cs_strref_state {
    std::size_t length;
    std::size_t refcount;
};

inline cs_strref_state *get_ref_state(char const *ptr) {
    return const_cast<cs_strref_state *>(
        reinterpret_cast<cs_strref_state const *>(ptr)
    ) - 1;
}

char const *cs_strman::add(std::string_view str) {
    auto it = counts.find(str);
    /* already present: just increment ref */
    if (it != counts.end()) {
        auto *st = it->second;
        /* having a null pointer is the same as non-existence */
        if (st) {
            ++st->refcount;
            return reinterpret_cast<char const *>(st + 1);
        }
    }
    /* not present: allocate brand new data */
    auto ss = str.size();
    auto strp = alloc_buf(ss);
    /* write string data, it's already pre-terminated */
    memcpy(strp, str.data(), ss);
    /* store it */
    counts.emplace(std::string_view{strp, ss}, get_ref_state(strp));
    return strp;
}

char const *cs_strman::ref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    ++ss->refcount;
    return ptr;
}

cs_strref cs_strman::steal(char *ptr) {
    auto *ss = get_ref_state(ptr);
    auto sr = std::string_view{ptr, ss->length};
    /* much like add(), but we already have memory */
    auto it = counts.find(sr);
    if (it != counts.end()) {
        auto *st = it->second;
        if (st) {
            /* the buffer is superfluous now */
            cstate->alloc(ss, ss->length + sizeof(cs_strref_state) + 1, 0);
            return cs_strref{reinterpret_cast<char const *>(st + 1), cstate};
        }
    }
    ss->refcount = 0; /* cs_strref will increment it */
    counts.emplace(sr, ss);
    return cs_strref{ptr, cstate};
}

void cs_strman::unref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    if (!--ss->refcount) {
        /* refcount zero, so ditch it
         * this path is a little slow...
         */
        auto sr = std::string_view{ptr, ss->length};
        auto it = counts.find(sr);
        if (it == counts.end()) {
            /* internal error: this should *never* happen */
            throw cs_internal_error{"no refcount"};
        }
        /* we're freeing the key */
        counts.erase(it);
        /* dealloc */
        cstate->alloc(ss, ss->length + sizeof(cs_strref_state) + 1, 0);
    }
}

char const *cs_strman::find(std::string_view str) const {
    auto it = counts.find(str);
    if (it == counts.end()) {
        return nullptr;
    }
    return reinterpret_cast<char const *>(it->second + 1);
}

std::string_view cs_strman::get(char const *ptr) const {
    auto *ss = get_ref_state(ptr);
    return std::string_view{ptr, ss->length};
}

char *cs_strman::alloc_buf(std::size_t len) const {
    auto mem = cstate->alloc(nullptr, 0, len + sizeof(cs_strref_state) + 1);
    if (!mem) {
        throw cs_internal_error{"allocation failed"};
    }
    /* write length and initial refcount */
    auto *sst = static_cast<cs_strref_state *>(mem);
    sst->length = len;
    sst->refcount = 1;
    /* pre-terminate */
    auto *strp = reinterpret_cast<char *>(sst + 1);
    strp[len] = '\0';
    /* now the user can fill it */
    return strp;
};

} /* namespace cscript */
