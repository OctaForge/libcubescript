#ifndef CS_REPL_HAS_EDIT
/* use nothing (no line editing support) */

#include <optional>
#include <string>

inline void init_lineedit(cs::state &, std::string_view) {
}

inline std::optional<std::string> read_line(cs::state &s, cs::builtin_var &pr) {
    std::string lbuf;
    char buf[512];
    printf("%s", pr.value().get_string(s).data());
    std::fflush(stdout);
    while (fgets(buf, sizeof(buf), stdin)) {
        lbuf += static_cast<char const *>(buf);
        if (strchr(buf, '\n')) {
            break;
        }
    }
    return std::move(lbuf);
}

inline void add_history(cs::state &, std::string_view) {
}

#endif
