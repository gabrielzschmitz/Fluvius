// engine/systems/thread_pool.h
//
// Persistent worker ganglia used by the physics pipeline. Traditional per-
// parallel-region pthread_create/pthread_join costs the equivalent of several
// microseconds of thread-stack setup per call; with several parallel regions
// per frame this dominates the work for particle counts in the low thousands.
//
// The pool spawns its workers once (at InitThreads). Idle workers block on a
// per-worker condition variable (no CPU burn between regions). The leader
// thread publishes a per-worker job under the worker's mutex, flags wake and
// notifies the CV; each worker runs its chunk and flips an atomic done flag
// that the leader spin-waits on. Region-to-region latency is a few
// microseconds, and there is no per-region thread creation.
//
// Only a single leader thread is supported (SimulateFluid is called from one
// thread); concurrent Run() calls are not safe.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace motrix::engine::systems {

class ThreadPool {
 public:
  static ThreadPool& Instance() {
    static ThreadPool pool;
    return pool;
  }

  struct Worker {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    std::function<void()> job;
    bool wake = false;
    bool shutdown = false;
    std::atomic<bool> done{true};
  };

  // (Re)creates the gang with `count` workers. Joins any previous gang first,
  // so calling this per-frame only if the count changed is harmless.
  void Start(size_t count) {
    Stop();

    size_t n = count > 0 ? count : 1;

    workers_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      workers_.push_back(std::make_unique<Worker>());
      workers_.back()->thread = std::thread([this, i] { WorkerLoop(i); });
    }
  }

  void Stop() {
    for (auto& w : workers_) {
      {
        std::lock_guard<std::mutex> lock(w->mutex);
        w->shutdown = true;
      }
      w->cv.notify_all();
    }
    for (auto& w : workers_) {
      if (w->thread.joinable()) w->thread.join();
    }
    workers_.clear();
  }

  size_t Size() const { return workers_.size(); }

  // Splits the [0, particle_count) range into `effective` static chunks and
  // runs `fn(&task)` for each chunk on a worker (inline on the leader when the
  // caller requests a single chunk). The caller must copy-construct seed.
  template <typename Task, typename Fn>
  void Run(int effective, size_t particle_count, Task& seed, Fn&& fn) {
    const size_t n = static_cast<size_t>(particle_count);
    const size_t worker_count = workers_.size();

    if (effective <= 0) effective = 1;
    if (static_cast<size_t>(effective) > worker_count)
      effective = static_cast<int>(worker_count);

    if (effective <= 1) {
      seed.start = 0;
      seed.end = static_cast<int>(n);
      fn(&seed);
      return;
    }

    int chunk_size = static_cast<int>(n) / effective;
    if (chunk_size < 64) chunk_size = 64;

    // Chunk ranges live on the leader's stack for the duration of the region;
    // workers only touch them while running the job, which completes before
    // Run() returns (the join below) so their lifetime is safe.
    std::vector<Task> tasks(effective);
    for (int i = 0; i < effective; ++i) {
      tasks[i] = seed;
      tasks[i].start = i * chunk_size;
      tasks[i].end = std::min(tasks[i].start + chunk_size, static_cast<int>(n));
    }

    for (int i = 0; i < effective; ++i) {
      Worker& w = *workers_[i];
      {
        std::lock_guard<std::mutex> lock(w.mutex);
        w.job = [i, &tasks, fn] { fn(&tasks[i]); };
        w.wake = true;
        w.done.store(false, std::memory_order_relaxed);
      }
      w.cv.notify_one();
    }

    for (int i = 0; i < effective; ++i) {
      Worker& w = *workers_[i];
      while (!w.done.load(std::memory_order_acquire)) {
      }
    }
  }

 private:
  void WorkerLoop(size_t index) {
    Worker& self = *workers_[index];

    std::unique_lock<std::mutex> lock(self.mutex);
    while (true) {
      self.cv.wait(lock, [&] { return self.shutdown || self.wake; });

      if (self.shutdown) return;

      self.wake = false;
      std::function<void()> job = std::move(self.job);
      lock.unlock();

      job();

      self.done.store(true, std::memory_order_release);
      lock.lock();
    }
  }

  std::vector<std::unique_ptr<Worker>> workers_;
};

}  // namespace motrix::engine::systems