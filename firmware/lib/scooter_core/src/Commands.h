// Commands.h - text commands from the phone (BLE) and the cloud (MQTT).
//
// The words are the ones the prototype sketch accepted: LOCK, UNLOCK, RESET on
// the command channel and CLEAR, SLOW, RED on the zone channel.
//
// BLE and MQTT callbacks run on other tasks than the control loop, so they do
// not touch the Context. They post into an IntentBox (atomic flags) and the
// control loop drains it at the start of each tick. The sketch wrote the
// Context directly from the BLE callback; this is the thread-safe version.
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "Types.h"

namespace scooter {

enum class Command : uint8_t { None, Lock, Unlock, Reset, ZoneClear, ZoneSlow, ZoneRed };

// Which words a channel accepts.
enum class Channel : uint8_t { Ride, Zone, Any };

// Parse a command word. Leading/trailing spaces and line ends are ignored;
// anything else, or a word that does not belong on this channel, gives None.
Command parseCommand(const char* text, size_t len, Channel channel);

class IntentBox {
 public:
  void post(Command c);            // safe to call from any task
  void drainInto(Context& ctx);    // control loop only

 private:
  std::atomic<uint8_t> flags_{0};
  std::atomic<uint8_t> zone_{0};
};

}  // namespace scooter
