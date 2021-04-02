#include "cs_thread.hh"

#include "cs_gen.hh"

namespace cubescript {

thread_state::thread_state(internal_state *cs):
    vmstack{cs}, idstack{cs}, errbuf{cs}
{
    vmstack.reserve(32);
    idstack.reserve(MAX_ARGUMENTS);
}

hook_func thread_state::set_hook(hook_func f) {
    auto hk = std::move(call_hook);
    call_hook = std::move(f);
    return hk;
}

alias_stack &thread_state::get_astack(alias *a) {
    return static_cast<alias_impl *>(a)->p_astack;
}

} /* namespace cubescript */
