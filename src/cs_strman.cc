#include <cubescript/cubescript.hh>

#include "cs_strman.hh"

namespace cubescript {

struct string_ref_state {
    std::size_t length;
    std::size_t refcount;
};

inline string_ref_state *get_ref_state(char const *ptr) {
    return const_cast<string_ref_state *>(
        reinterpret_cast<string_ref_state const *>(ptr)
    ) - 1;
}

char const *string_pool::add(std::string_view str) {
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

char const *string_pool::ref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    ++ss->refcount;
    return ptr;
}

string_ref string_pool::steal(char *ptr) {
    auto *ss = get_ref_state(ptr);
    auto sr = std::string_view{ptr, ss->length};
    /* much like add(), but we already have memory */
    auto it = counts.find(sr);
    if (it != counts.end()) {
        auto *st = it->second;
        if (st) {
            /* the buffer is superfluous now */
            cstate->alloc(ss, ss->length + sizeof(string_ref_state) + 1, 0);
            return string_ref{reinterpret_cast<char const *>(st + 1), cstate};
        }
    }
    ss->refcount = 0; /* string_ref will increment it */
    counts.emplace(sr, ss);
    return string_ref{ptr, cstate};
}

void string_pool::unref(char const *ptr) {
    auto *ss = get_ref_state(ptr);
    if (!--ss->refcount) {
        /* refcount zero, so ditch it
         * this path is a little slow...
         */
        auto sr = std::string_view{ptr, ss->length};
        auto it = counts.find(sr);
        if (it == counts.end()) {
            /* internal error: this should *never* happen */
            throw internal_error{"no refcount"};
        }
        /* we're freeing the key */
        counts.erase(it);
        /* dealloc */
        cstate->alloc(ss, ss->length + sizeof(string_ref_state) + 1, 0);
    }
}

char const *string_pool::find(std::string_view str) const {
    auto it = counts.find(str);
    if (it == counts.end()) {
        return nullptr;
    }
    return reinterpret_cast<char const *>(it->second + 1);
}

std::string_view string_pool::get(char const *ptr) const {
    auto *ss = get_ref_state(ptr);
    return std::string_view{ptr, ss->length};
}

char *string_pool::alloc_buf(std::size_t len) const {
    auto mem = cstate->alloc(nullptr, 0, len + sizeof(string_ref_state) + 1);
    if (!mem) {
        throw internal_error{"allocation failed"};
    }
    /* write length and initial refcount */
    auto *sst = static_cast<string_ref_state *>(mem);
    sst->length = len;
    sst->refcount = 1;
    /* pre-terminate */
    auto *strp = reinterpret_cast<char *>(sst + 1);
    strp[len] = '\0';
    /* now the user can fill it */
    return strp;
};

/* strref implementation */

/* strref */

LIBCUBESCRIPT_EXPORT string_ref::string_ref(
    internal_state *cs, std::string_view str
): p_state{cs}
{
    p_str = cs->strman->add(str);
}

LIBCUBESCRIPT_EXPORT string_ref::string_ref(state &cs, std::string_view str):
    p_state{cs.p_state}
{
    p_str = p_state->strman->add(str);
}

LIBCUBESCRIPT_EXPORT string_ref::string_ref(string_ref const &ref):
    p_state{ref.p_state}, p_str{ref.p_str}
{
    p_state->strman->ref(p_str);
}

/* this can be used by friends to do quick string_ref creation */
LIBCUBESCRIPT_EXPORT string_ref::string_ref(char const *p, internal_state *cs):
    p_state{cs}
{
    p_str = p_state->strman->ref(p);
}

LIBCUBESCRIPT_EXPORT string_ref::~string_ref() {
    p_state->strman->unref(p_str);
}

LIBCUBESCRIPT_EXPORT string_ref &string_ref::operator=(string_ref const &ref) {
    p_str = ref.p_str;
    p_state = ref.p_state;
    p_state->strman->ref(p_str);
    return *this;
}

LIBCUBESCRIPT_EXPORT string_ref::operator std::string_view() const {
    return p_state->strman->get(p_str);
}

LIBCUBESCRIPT_EXPORT bool string_ref::operator==(string_ref const &s) const {
    return p_str == s.p_str;
}

} /* namespace cubescript */