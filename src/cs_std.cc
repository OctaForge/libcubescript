#include "cs_std.hh"

#include "cs_thread.hh"

namespace cubescript {

charbuf::charbuf(state &cs): charbuf{cs.thread_pointer()->istate} {}
charbuf::charbuf(thread_state &ts): charbuf{ts.istate} {}

} /* namespace cubescript */
