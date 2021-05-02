#include "cs_thread.hh"

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

alias_stack &thread_state::get_astack(alias const *a) {
    auto it = astacks.try_emplace(a->get_index());
    if (it.second) {
        auto *imp = const_cast<alias_impl *>(
            static_cast<alias_impl const *>(a)
        );
        it.first->second.node = &imp->p_initial;
        it.first->second.flags = imp->p_flags;
    }
    return it.first->second;
}

} /* namespace cubescript */
