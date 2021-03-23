#include <memory>

#include "cs_util.hh"
#include "cs_bcode.hh"
#include "cs_state.hh"
#include "cs_strman.hh"

namespace cscript {

cs_shared_state::cs_shared_state(cs_alloc_cb af, void *data):
    allocf{af}, aptr{data},
    idents{allocator_type{this}},
    identmap{allocator_type{this}},
    varprintf{},
    strman{create<cs_strman>(this)},
    empty{bcode_init_empty(this)}
{}

cs_shared_state::~cs_shared_state() {
     bcode_free_empty(this, empty);
     destroy(strman);
}

void *cs_shared_state::alloc(void *ptr, size_t os, size_t ns) {
    void *p = allocf(aptr, ptr, os, ns);
    if (!p && ns) {
        throw std::bad_alloc{};
    }
    return p;
}

} /* namespace cscript */
