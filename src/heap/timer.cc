bool Timer::started() {
  return started_;
}

void Timer::start(const std::function<void()>& f, time_t::duration interval) {
  std::lock_guard<std::recursive_mutex> timer_guard(mutex);
  assert(!started());
  started_ = true;
  t = std::thread([=](){
    while(this->started_) {
      time_t time = std::chrono::system_clock::now();
      f();
      std::this_thread::sleep_until(time + interval);
    }
  });
}

void Timer::try_start(const std::function<void()>& f, time_t::duration interval) {
  if (!started()) {
    start(f, interval);
  }
}

void Timer::stop() {
  std::lock_guard<std::recursive_mutex> timer_guard(mutex);
  CHECK(started());
  started_ = false;
  CHECK(t.joinable());
  t.join();
}

void Timer::try_stop() {
  std::lock_guard<std::recursive_mutex> timer_guard(mutex);
  if (started()) {
    stop();
  }
}
