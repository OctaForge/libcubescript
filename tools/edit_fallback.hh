#ifndef CS_REPL_HAS_EDIT
/* use nothing (no line editing support) */

#include <optional>

#include <ostd/string.hh>

inline void init_lineedit(cs_state &, std::string_view) {
}

inline std::optional<std::string> read_line(cs_state &, cs_svar *pr) {
    ostd::write(pr->get_value());
    return ostd::cin.get_line(ostd::appender<std::string>()).get();
}

inline void add_history(cs_state &, std::string_view) {
}

#endif
