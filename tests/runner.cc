/* a rudimentary test runner for cubescript files */

#ifdef _MSC_VER
/* avoid silly complaints about fopen */
#  define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <cstdio>
#include <string>
#include <string_view>

#include <cubescript/cubescript.hh>

namespace cs = cubescript;

struct skip_test {};

static bool do_run_file(cs::state &cs, char const *fname) {
    FILE *f = std::fopen(fname, "rb");
    if (!f) {
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    auto len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    auto buf = std::make_unique<char[]>(len + 1);
    if (!buf) {
        std::fclose(f);
        return false;
    }

    if (std::fread(buf.get(), 1, len, f) != std::size_t(len)) {
        std::fclose(f);
        return false;
    }

    buf[len] = '\0';

    cs.run(std::string_view{buf.get(), std::size_t(len)}, fname);
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "error: incorrect number of arguments\n");
    }

    cs::state gcs;
    cs::std_init_all(gcs);

    gcs.new_command("echo", "C", [](auto &s, auto args, auto &) {
        std::printf("%s\n", args[0].get_string(s).view().data());
    });

    gcs.new_command("skip_test", "", [](auto &, auto, auto &) {
        throw skip_test{};
    });

    /* takes a string so we can print it */
    gcs.new_command("assert", "ssN", [](auto &s, auto args, auto &ret) {
        auto val = args[0];
        val.force_code(s);
        if (!s.run(val.get_code()).get_bool()) {
            if (args[2].get_integer() > 1) {
                throw cs::error{
                    s, "assertion failed: [%s] (%s)",
                    args[0].get_string(s).data(), args[1].get_string(s).data()
                };
            } else {
                throw cs::error{
                    s, "assertion failed: [%s]",
                    args[0].get_string(s).data()
                };
            }
        }
        ret = std::move(args[0]);
    });

    try {
        do_run_file(gcs, argv[1]);
    } catch (skip_test) {
        return 77;
    } catch (cs::error const &e) {
        std::fprintf(stderr, "error: %s", e.what().data());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "error: unknown error");
        return 1;
    }

    return 0;
}
