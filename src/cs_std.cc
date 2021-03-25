#include "cs_std.hh"

#include "cs_thread.hh"

namespace cubescript {

charbuf::charbuf(state &cs): charbuf{cs.p_tstate->istate} {}

} /* namespace cubescript */
