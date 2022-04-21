/** @file callable.hh
 *
 * @brief Internal callable data structure.
 *
 * There is no public API in this file.
 *
 * @copyright See COPYING.md in the project tree for further information.
 */

#ifndef LIBCUBESCRIPT_CUBESCRIPT_CALLABLE_HH
#define LIBCUBESCRIPT_CUBESCRIPT_CALLABLE_HH

#include <cstring>
#include <cstddef>
#include <utility>
#include <type_traits>
#include <functional>
#include <memory>

namespace cubescript {
namespace internal {

/** @private */
template<typename R, typename ...A>
struct callable {
private:
    struct base {
        base(base const &);
        base &operator=(base const &);

    public:
        base() {}
        virtual ~base() {}
        virtual void move_to(base *) = 0;
        virtual R operator()(A &&...args) const = 0;
    };

    template<typename F>
    struct store: base {
        explicit store(F &&f): p_stor{std::move(f)} {}

        virtual void move_to(base *p) {
            ::new (p) store{std::move(p_stor)};
        }

        virtual R operator()(A &&...args) const {
            return std::invoke(*std::launder(
                reinterpret_cast<F const *>(&p_stor)
            ), std::forward<A>(args)...);
        }

    private:
        F p_stor;
    };

    using alloc_f = void *(*)(void *, void *, std::size_t, std::size_t);

    struct f_alloc {
        alloc_f af;
        void *ud;
        size_t asize;
    };

    alignas(std::max_align_t) unsigned char p_stor[sizeof(void *) * 4];
    base *p_func;

    static inline base *as_base(void *p) {
        return static_cast<base *>(p);
    }

    template<typename T>
    static inline bool f_not_null(T const &) { return true; }

    template<typename T>
    static inline bool f_not_null(T *p) { return !!p; }

    template<typename CR, typename C>
    static inline bool f_not_null(CR C::*p) { return !!p; }

    template<typename T>
    static inline bool f_not_null(callable<T> const &f) { return !!f; }

    bool small_storage() {
        return (static_cast<void *>(p_func) == &p_stor);
    }

    void cleanup() {
        if (!p_func) {
            return;
        }
        p_func->~base();
        if (!small_storage()) {
            auto &ad = *std::launder(reinterpret_cast<f_alloc *>(&p_stor));
            ad.af(ad.ud, p_func, ad.asize, 0);
        }
    }

public:
    callable() noexcept: p_func{nullptr} {}
    callable(std::nullptr_t) noexcept: p_func{nullptr} {}
    callable(std::nullptr_t, alloc_f, void *) noexcept: p_func{nullptr} {}

    callable(callable &&f) noexcept {
        if (!f.p_func) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
    }

    template<typename F>
    callable(F func, alloc_f af, void *ud) {
        if (!f_not_null(func)) {
            return;
        }
        if constexpr (sizeof(store<F>) <= sizeof(p_stor)) {
            auto *p = static_cast<void *>(&p_stor);
            p_func = ::new (p) store<F>{std::move(func)};
        } else {
            auto &ad = *std::launder(reinterpret_cast<f_alloc *>(&p_stor));
            ad.af = af;
            ad.ud = ud;
            ad.asize = sizeof(store<F>);
            p_func = static_cast<store<F> *>(
                af(ud, nullptr, 0, sizeof(store<F>))
            );
            try {
                new (p_func) store<F>{std::move(func)};
            } catch (...) {
                af(ud, p_func, sizeof(store<F>), 0);
                throw;
            }
        }
    }

    callable &operator=(callable const &) = delete;

    callable &operator=(callable &&f) noexcept {
        cleanup();
        if (f.p_func == nullptr) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
        return *this;
    }

    callable &operator=(std::nullptr_t) noexcept {
        cleanup();
        p_func = nullptr;
        return *this;
    }

    template<typename F>
    callable &operator=(F &&func) {
        callable{std::forward<F>(func)}.swap(*this);
        return *this;
    }

    ~callable() {
        cleanup();
    }

    void swap(callable &f) noexcept {
        alignas(std::max_align_t) unsigned char tmp_stor[sizeof(p_stor)];
        if (small_storage() && f.small_storage()) {
            auto *t = as_base(&tmp_stor);
            p_func->move_to(t);
            p_func->~base();
            p_func = nullptr;
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = nullptr;
            p_func = as_base(&p_stor);
            t->move_to(as_base(&f.p_stor));
            t->~base();
            f.p_func = as_base(&f.p_stor);
        } else if (small_storage()) {
            /* copy allocator address/size */
            std::memcpy(&tmp_stor, &f.p_stor, sizeof(tmp_stor));
            p_func->move_to(as_base(&f.p_stor));
            p_func->~base();
            p_func = f.p_func;
            f.p_func = as_base(&f.p_stor);
            std::memcpy(&p_stor, &tmp_stor, sizeof(tmp_stor));
        } else if (f.small_storage()) {
            /* copy allocator address/size */
            std::memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = p_func;
            p_func = as_base(&p_stor);
            std::memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
        } else {
            /* copy allocator address/size */
            std::memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            std::memcpy(&p_stor, &f.p_stor, sizeof(tmp_stor));
            std::memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
            std::swap(p_func, f.p_func);
        }
    }

    explicit operator bool() const noexcept {
        return !!p_func;
    }

    R operator()(A ...args) const {
        return (*p_func)(std::forward<A>(args)...);
    }
};

} /* namespace internal */
} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_CALLABLE_HH */
