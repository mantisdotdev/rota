#pragma once

#include <atomic>

// Lock-free single-producer, single-consumer ring buffers for the seams between the
// control side (main loop, timer) and the audio callback (D-084). Fixed capacity,
// no allocation; a full queue refuses a push and the producer tries again later.
namespace app {

template <typename T, int Capacity>
class SpscQueue {
 public:
  SpscQueue() : head_(0), tail_(0), items_{} {}

  // Producer side.
  bool push(const T& item) {
    const int tail = tail_.load(std::memory_order_relaxed);
    const int next = (tail + 1) % Capacity;
    if (next == head_.load(std::memory_order_acquire)) return false;
    items_[tail] = item;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side.
  bool peek(T& out) const {
    const int head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return false;
    out = items_[head];
    return true;
  }

  void drop() {
    const int head = head_.load(std::memory_order_relaxed);
    head_.store((head + 1) % Capacity, std::memory_order_release);
  }

  bool pop(T& out) {
    if (!peek(out)) return false;
    drop();
    return true;
  }

  // Either side; a snapshot that may be stale by the time it is read.
  int size() const {
    const int head = head_.load(std::memory_order_acquire);
    const int tail = tail_.load(std::memory_order_acquire);
    return tail >= head ? tail - head : tail + Capacity - head;
  }

 private:
  std::atomic<int> head_;
  std::atomic<int> tail_;
  T items_[Capacity];
};

// The newest value of something, handed from one producer to one consumer without
// tearing: a triple buffer. take() answers false until the next publish.
template <typename T>
class Mailbox {
 public:
  Mailbox() : slots_{}, middle_(1), back_(0), front_(2) {}

  void publish(const T& value) {
    slots_[back_] = value;
    const int previous = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
    back_ = previous & kSlotMask;
  }

  bool take(T& out) {
    if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) return false;
    const int previous = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = previous & kSlotMask;
    out = slots_[front_];
    return true;
  }

 private:
  static constexpr int kFresh = 4;
  static constexpr int kSlotMask = 3;

  T slots_[3];
  std::atomic<int> middle_;
  int back_;   // the producer's private slot
  int front_;  // the consumer's private slot
};

}  // namespace app
