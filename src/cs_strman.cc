#include <cassert>
#include <cstring>
#include <cubescript/cubescript.hh>

#include "cs_strman.hh"
#include "cs_thread.hh"

namespace cubescript {

struct string_ref_state {
    internal_state *state;
    std::size_t length;
    std::size_t refcount;
};

inline string_ref_state *get_ref_state(char const *ptr) {
    string_ref_state *r;
    std::memcpy(&r, &ptr, sizeof(r));
    return r - 1;
}

char const *string_pool::add(std::string_view str) {
    {
        std::lock_guard<std::mutex> l{p_mtx};
        auto it = counts.find(str);
        /* already present: just increment ref */
        if (it != counts.end()) {
            auto *st = it->second;
            /* having a null pointer is the same as non-existence */
            if (st) {
                ++st->refcount;
                st += 1;
                char const *r;
                std::memcpy(&r, &st, sizeof(r));
                return r;
            }
        }
    }
    /* not present: allocate brand new data */
    auto ss = str.size();
    auto strp = alloc_buf(ss);
    /* write string data, it's already pre-terminated */
    memcpy(strp, str.data(), ss);
    /* store it */
    {
        std::lock_guard<std::mutex> l{p_mtx};
        counts.emplace(std::string_view{strp, ss}, get_ref_state(strp));
    }
    return strp;
}

char const *string_pool::internal_ref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    {
        std::lock_guard<std::mutex> l{p_mtx};
        ++ss->refcount;
    }
    return ptr;
}

string_ref string_pool::steal(char *ptr) {
    auto *ss = get_ref_state(ptr);
    auto sr = std::string_view{ptr, ss->length};
    string_ref_state *st = nullptr;
    {
        std::lock_guard<std::mutex> l{p_mtx};
        /* much like add(), but we already have memory */
        auto it = counts.find(sr);
        if (it != counts.end()) {
            st = it->second;
        }
    }
    if (st) {
        /* the buffer is superfluous now */
        cstate->alloc(ss, ss->length + sizeof(string_ref_state) + 1, 0);
        st += 1;
        char const *rp;
        std::memcpy(&rp, &st, sizeof(rp));
        return string_ref{rp};
    } else {
        std::lock_guard<std::mutex> l{p_mtx};
        ss->refcount = 0; /* string_ref will increment it */
        counts.emplace(sr, ss);
    }
    return string_ref{ptr};
}

void string_pool::internal_unref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    if (std::lock_guard<std::mutex> l{p_mtx}; !--ss->refcount) {
        /* refcount zero, so ditch it
         * this path is a little slow...
         */
        auto sr = std::string_view{ptr, ss->length};
        auto it = counts.find(sr);
        /* this should *never* happen unless we have a bug */
#ifndef NDEBUG
        assert(it != counts.end());
#else
        if (it == counts.end()) {
            abort();
        }
#endif
        /* we're freeing the key */
        counts.erase(it);
    } else {
        return;
    }
    /* dealloc */
    cstate->alloc(ss, ss->length + sizeof(string_ref_state) + 1, 0);
}

char const *string_pool::find(std::string_view str) const {
    string_ref_state *sp;
    {
        std::lock_guard<std::mutex> l{p_mtx};
        auto it = counts.find(str);
        if (it == counts.end()) {
            return nullptr;
        }
        sp = it->second + 1;
    }
    char const *rp;
    std::memcpy(&rp, &sp, sizeof(rp));
    return rp;
}

std::string_view string_pool::get(char const *ptr) const {
    auto *ss = get_ref_state(ptr);
    return std::string_view{ptr, ss->length};
}

char *string_pool::alloc_buf(std::size_t len) const {
    auto mem = cstate->alloc(nullptr, 0, len + sizeof(string_ref_state) + 1);
    /* write length and initial refcount */
    auto *sst = static_cast<string_ref_state *>(mem);
    sst->state = cstate;
    sst->length = len;
    sst->refcount = 1;
    /* pre-terminate */
    char *strp;
    sst += 1;
    std::memcpy(&strp, &sst, sizeof(strp));
    strp[len] = '\0';
    /* now the user can fill it */
    return strp;
}

char const *str_managed_ref(char const *str) {
    return get_ref_state(str)->state->strman->internal_ref(str);
}

void str_managed_unref(char const *str) {
    get_ref_state(str)->state->strman->internal_unref(str);
}

std::string_view str_managed_view(char const *str) {
    return get_ref_state(str)->state->strman->get(str);
}

/* strref implementation */

LIBCUBESCRIPT_EXPORT string_ref::string_ref(state &cs, std::string_view str) {
    p_str = state_p{cs}.ts().istate->strman->add(str);
}

LIBCUBESCRIPT_EXPORT string_ref::string_ref(string_ref const &ref):
    p_str{ref.p_str}
{
    get_ref_state(p_str)->state->strman->internal_ref(p_str);
}

/* this can be used by friends to do quick string_ref creation */
LIBCUBESCRIPT_EXPORT string_ref::string_ref(char const *p) {
    p_str = str_managed_ref(p);
}

LIBCUBESCRIPT_EXPORT string_ref::~string_ref() {
    str_managed_unref(p_str);
}

LIBCUBESCRIPT_EXPORT string_ref &string_ref::operator=(string_ref const &ref) {
    p_str = str_managed_ref(ref.p_str);
    return *this;
}

LIBCUBESCRIPT_EXPORT char const *string_ref::data() const {
    return p_str;
}

LIBCUBESCRIPT_EXPORT string_ref::operator std::string_view() const {
    return str_managed_view(p_str);
}

LIBCUBESCRIPT_EXPORT bool string_ref::operator==(string_ref const &s) const {
    return p_str == s.p_str;
}

LIBCUBESCRIPT_EXPORT bool string_ref::operator!=(string_ref const &s) const {
    return p_str != s.p_str;
}

} /* namespace cubescript */
