#include "cs_thread.hh"

#include <cstdio>

namespace cubescript {

thread_state::thread_state(internal_state *cs):
    vmstack{cs}, idstack{cs}, callstack{cs}, astacks{cs}, errbuf{cs}
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
    auto it = astacks.try_emplace(a->index());
    if (it.second) {
        auto *imp = const_cast<alias_impl *>(
            static_cast<alias_impl const *>(a)
        );
        it.first->second.node = &imp->p_initial;
        it.first->second.flags = imp->p_flags;
    }
    return it.first->second;
}

char *thread_state::request_errbuf(std::size_t bufs, char *&sp) {
    errbuf.clear();
    std::size_t sz = 0;
    if (current_line) {
        /* we can attach line number */
        sz = source.size() + 32;
        for (;;) {
            /* we are using so the buffer tracks the elements and therefore
             * does not wipe them when we attempt to reserve more capacity
             */
            errbuf.resize(sz);
            int nsz;
            if (!source.empty()) {
                nsz = std::snprintf(
                    errbuf.data(), sz, "%.*s:%zu: ",
                    int(source.size()), source.data(),
                    *current_line
                );
            } else {
                nsz = std::snprintf(
                    errbuf.data(), sz, "%zu: ", *current_line
                );
            }
            if (nsz <= 0) {
                abort(); /* should be unreachable */
            } else if (std::size_t(nsz) < sz) {
                sz = std::size_t(nsz);
                break;
            }
            sz = std::size_t(nsz + 1);
        }
    }
    errbuf.resize(sz + bufs + 1);
    sp = errbuf.data();
    return &errbuf[sz];
}

} /* namespace cubescript */
