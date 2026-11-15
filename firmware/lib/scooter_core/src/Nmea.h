// Nmea.h - position from a NEO-6M style GPS receiver. [new in this rewrite]
//
// The receiver sends text lines ("NMEA sentences") over a serial port. Only
// the RMC sentence is used: it carries validity, latitude and longitude.
//   $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
// The prototype had no GPS; this parser is tested on the host only.
#pragma once
#include <stddef.h>

namespace scooter {

struct GpsFix {
  bool valid = false;
  double lat = 0.0;  // degrees, north positive
  double lng = 0.0;  // degrees, east positive
};

// Parse one complete sentence. Returns false (and leaves `out` alone) if the
// checksum is wrong, the sentence is not RMC, or the receiver says "no fix".
bool parseRmc(const char* sentence, GpsFix& out);

// Collects characters from the serial port into lines and parses each one.
class NmeaReader {
 public:
  // Feed one received character. Returns true when a new valid fix was stored.
  bool feed(char c);
  const GpsFix& fix() const { return fix_; }

 private:
  char line_[100];
  size_t len_ = 0;
  GpsFix fix_;
};

}  // namespace scooter
