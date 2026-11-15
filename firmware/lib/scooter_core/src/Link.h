// Link.h - two small helpers for an unreliable network link. [new in this rewrite]
//
// Backoff: how long to wait before the next reconnect attempt. The wait
// doubles after every failure up to a ceiling, so a scooter in a dead zone
// does not hammer the radio (book ch. 8.4, "exponential backoff").
//
// Outbox: telemetry produced while offline is kept in a fixed ring and sent
// oldest-first after reconnecting; when the ring is full the oldest record is
// dropped (book ch. 9.2.2, "store and forward"). The book stores it in flash;
// this version keeps it in RAM, so a reboot loses it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace scooter {

class Backoff {
 public:
  Backoff(uint32_t baseMs, uint32_t maxMs) : base_(baseMs), max_(maxMs), next_(baseMs) {}

  // The delay to wait now; the following one will be twice as long.
  uint32_t nextDelayMs() {
    const uint32_t d = next_;
    next_ = (next_ > max_ / 2) ? max_ : next_ * 2;
    return d;
  }
  void reset() { next_ = base_; }

 private:
  uint32_t base_, max_, next_;
};

template <int SLOTS, int LENGTH>
class Outbox {
 public:
  // Returns false if the text does not fit in one slot (it is not stored).
  bool push(const char* text) {
    const size_t n = strlen(text);
    if (n >= static_cast<size_t>(LENGTH)) return false;
    if (count_ == SLOTS) {  // full: overwrite the oldest
      head_ = (head_ + 1) % SLOTS;
      --count_;
      ++dropped_;
    }
    memcpy(slots_[(head_ + count_) % SLOTS], text, n + 1);
    ++count_;
    return true;
  }
  bool empty() const { return count_ == 0; }
  int size() const { return count_; }
  uint32_t dropped() const { return dropped_; }
  const char* front() const { return slots_[head_]; }  // oldest record
  void pop() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % SLOTS;
    --count_;
  }

 private:
  char slots_[SLOTS][LENGTH];
  int head_ = 0;
  int count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace scooter
