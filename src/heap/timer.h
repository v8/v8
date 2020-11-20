#ifndef V8_HEAP_TIMER_H_
#define V8_HEAP_TIMER_H_

#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

// A timer that call a function periodically.
// One thing I can do is to have timer as a member of the object of interest,
// And start/stop the timer automatically with RAII tricks, in the timer class.
// This sounds too dangerous - the function might run when it is invalid.
// Also, when the object timer call is being destructed, timer might call on invalid state again.
// This API is also more flexible.
struct Timer {
  using time_t = std::chrono::time_point<std::chrono::system_clock>;
  std::atomic<bool> started_;
  std::thread t;
  Timer() : started_(false) { }
  ~Timer() {
    try_stop();
  }
  std::recursive_mutex mutex;
  // a timer to make sure after stop() return, f() will not be running.
  // we force the timer mutex to be of a high priority - other mutex-locked code *cannot* lock timer by calling timer's method.
  // this design decision allow timer's f to call arbitary code.
  // todo: right now, mutex is locked automatically on api call.
  // maybe expose a transactional-based api that require passing the lock_guard?
  bool started();
  void start(const std::function<void()>& f, time_t::duration interval);
  void try_start(const std::function<void()>& f, time_t::duration interval);
  void stop();
  void try_stop();
};

#endif  // V8_HEAP_TIMER_H_
