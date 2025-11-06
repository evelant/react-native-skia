#pragma once

#include <android/looper.h>
#include <queue>
#include <unistd.h>
#include <mutex>
#include <functional>

// NOTE (ANR/Deadlock fix):
// This dispatcher previously executed queued tasks while holding the queue
// mutex. If a task posted more work (common during Skia redraw scheduling) or
// re-entered into JSI/React Native while the JS runtime lock was held, it could
// cause a lock inversion with the JS runtime lock and the queue mutex, leading
// to ANRs on the main (UI) thread.
//
// Change: Execute tasks outside of the queue mutex by swapping to a local
// queue. This avoids holding the mutex during arbitrary user code and
// eliminates lock inversion with other locks (e.g., JSI runtime). We purposefully
// do NOT add a same-thread fast path (running tasks inline when called from
// the main looper) to preserve FIFO ordering and avoid surprising reentrancy.
// Tasks posted from main are enqueued and picked up by the current drain loop.
//
// Behavior remains: tasks still run on the main thread; only queue mutation is
// protected by the mutex.
class MainThreadDispatcher {
private:
  ALooper *mainLooper = nullptr;
  int messagePipe[2] = {-1, -1};
  std::queue<std::function<void()>> taskQueue;
  std::mutex queueMutex;

  static constexpr int LOOPER_ID_MAIN = 1;

  void processMessages() {
    // Drain the queue in batches without holding the mutex during execution.
    // This prevents lock inversion with other locks (e.g., JSI runtime) that
    // tasks might acquire.
    for (;;) {
      std::queue<std::function<void()>> local;
      {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (taskQueue.empty()) {
          break;
        }
        std::swap(local, taskQueue);
      }

      while (!local.empty()) {
        auto task = std::move(local.front());
        local.pop();
        task();
      }

      // Loop to catch work enqueued by tasks themselves without requiring
      // another looper wakeup. If nothing new was posted, the next iteration
      // breaks immediately.
    }
  }

public:
  static MainThreadDispatcher &getInstance() {
    static MainThreadDispatcher instance;
    return instance;
  }

  bool isOnMainThread() { return ALooper_forThread() == mainLooper; }

  void post(std::function<void()> task) {
    // Always enqueue (even on main) to preserve FIFO ordering and avoid
    // reentrancy. Since processMessages drains in batches, tasks posted during
    // a drain are picked up within the same looper iteration without holding
    // the mutex while executing them.
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      taskQueue.push(std::move(task));
    }
    char wake = 1;
    write(messagePipe[1], &wake, 1);
  }

  ~MainThreadDispatcher() {
    close(messagePipe[0]);
    close(messagePipe[1]);
  }

private:
  MainThreadDispatcher() {
    mainLooper = ALooper_forThread();
    if (!mainLooper) {
      mainLooper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }

    pipe(messagePipe);

    ALooper_addFd(
        mainLooper, messagePipe[0], LOOPER_ID_MAIN, ALOOPER_EVENT_INPUT,
        [](int fd, int events, void *data) -> int {
          char buf[1];
          read(fd, buf, 1);
          auto dispatcher = static_cast<MainThreadDispatcher *>(data);
          dispatcher->processMessages();
          return 1;
        },
        this);
  }
};
