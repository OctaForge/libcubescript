#include "cs_thread.hh"

#include "cs_gen.hh"

namespace cubescript {

thread_state::thread_state(internal_state *cs):
    vmstack{cs}, idstack{cs}, astacks{cs}, errbuf{cs}
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
    auto it = astacks.try_emplace(a->get_index());
    if (it.second) {
        it.first->second.node = &static_cast<alias_impl *>(a)->p_initial;
        it.first->second.flags = static_cast<alias_impl *>(a)->p_flags;
    }
    return it.first->second;
}

alias_stack const &thread_state::get_astack(alias const *a) {
    return get_astack(const_cast<alias *>(a));
}

} /* namespace cubescript */
