#ifndef LIBCUBESCRIPT_LOCK_HH
#define LIBCUBESCRIPT_LOCK_HH

#include <cubescript/cubescript.hh>

#if LIBCUBESCRIPT_CONF_THREAD_SAFE
#include <mutex>
#include <atomic>
#else
#include <utility>
#endif

namespace cubescript {

#if ! LIBCUBESCRIPT_CONF_THREAD_SAFE

struct mutex_type {
    void lock() {}
    void unlock() {}
};

template<typename T>
struct atomic_type {
    T p_v;

    T load() const {
        return p_v;
    }
    void store(T v) {
        p_v = v;
    }
    T exchange(T v) {
        return std::exchange(p_v, v);
    }
};

#else

using mutex_type = std::mutex;
template<typename T>
using atomic_type = std::atomic<T>;

#endif

struct mtx_guard {
    mtx_guard(mutex_type &m): p_m{m} {
        m.lock();
    }

    ~mtx_guard() {
        p_m.unlock();
    }

    mutex_type &p_m;
};

} /* namespace cubescript */

#endif
