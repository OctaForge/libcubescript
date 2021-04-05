#include "cs_std.hh"

#include "cs_thread.hh"

namespace cubescript {

charbuf::charbuf(state &cs): charbuf{state_p{cs}.ts().istate} {}
charbuf::charbuf(thread_state &ts): charbuf{ts.istate} {}

} /* namespace cubescript */
