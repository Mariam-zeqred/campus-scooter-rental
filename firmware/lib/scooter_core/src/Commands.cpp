#include "Commands.h"

#include <string.h>

namespace scooter {

namespace {

constexpr uint8_t kLock = 1, kUnlock = 2, kReset = 4, kZone = 8;

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

bool equals(const char* text, size_t len, const char* word) {
  return strlen(word) == len && memcmp(text, word, len) == 0;
}

}  // namespace

Command parseCommand(const char* text, size_t len, Channel channel) {
  while (len > 0 && isSpace(text[0])) {
    ++text;
    --len;
  }
  while (len > 0 && isSpace(text[len - 1])) --len;

  if (channel != Channel::Zone) {
    if (equals(text, len, "LOCK")) return Command::Lock;
    if (equals(text, len, "UNLOCK")) return Command::Unlock;
    if (equals(text, len, "RESET")) return Command::Reset;
  }
  if (channel != Channel::Ride) {
    if (equals(text, len, "CLEAR")) return Command::ZoneClear;
    if (equals(text, len, "SLOW")) return Command::ZoneSlow;
    if (equals(text, len, "RED")) return Command::ZoneRed;
  }
  return Command::None;
}

void IntentBox::post(Command c) {
  switch (c) {
    case Command::Lock: flags_.fetch_or(kLock); break;
    case Command::Unlock: flags_.fetch_or(kUnlock); break;
    case Command::Reset: flags_.fetch_or(kReset); break;
    case Command::ZoneClear: zone_.store(static_cast<uint8_t>(Zone::Clear)); flags_.fetch_or(kZone); break;
    case Command::ZoneSlow: zone_.store(static_cast<uint8_t>(Zone::Slow)); flags_.fetch_or(kZone); break;
    case Command::ZoneRed: zone_.store(static_cast<uint8_t>(Zone::Red)); flags_.fetch_or(kZone); break;
    case Command::None: break;
  }
}

void IntentBox::drainInto(Context& ctx) {
  const uint8_t f = flags_.exchange(0);
  if (f & kLock) ctx.lockRequested = true;
  if (f & kUnlock) ctx.unlockRequested = true;
  if (f & kReset) ctx.resetRequested = true;
  if (f & kZone) {
    ctx.requestedZone = static_cast<Zone>(zone_.load());
    ctx.zoneUpdateRequested = true;
  }
}

}  // namespace scooter
