#include "Nmea.h"

#include <stdlib.h>
#include <string.h>

namespace scooter {

namespace {

// The checksum is the XOR of every character between '$' and '*'.
bool checksumOk(const char* s) {
  if (s[0] != '$') return false;
  unsigned char sum = 0;
  const char* p = s + 1;
  while (*p && *p != '*') sum ^= static_cast<unsigned char>(*p++);
  if (*p != '*' || !p[1] || !p[2]) return false;
  char hex[3] = {p[1], p[2], 0};
  return static_cast<unsigned char>(strtoul(hex, nullptr, 16)) == sum;
}

// "ddmm.mmmm" (or "dddmm.mmmm") -> decimal degrees.
double toDegrees(const char* field) {
  const double v = atof(field);
  const int whole = static_cast<int>(v / 100.0);
  return whole + (v - whole * 100.0) / 60.0;
}

}  // namespace

bool parseRmc(const char* sentence, GpsFix& out) {
  if (!checksumOk(sentence)) return false;
  if (strncmp(sentence + 3, "RMC,", 4) != 0) return false;  // $GPRMC, $GNRMC, ...

  // Split a copy at the commas; empty fields stay as empty strings.
  char buf[100];
  strncpy(buf, sentence, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  const char* fields[8] = {"", "", "", "", "", "", "", ""};
  int n = 0;
  fields[n++] = buf;
  for (char* p = buf; *p && n < 8; ++p) {
    if (*p == ',') {
      *p = 0;
      fields[n++] = p + 1;
    }
  }
  // fields: 0 $GPRMC, 1 time, 2 status, 3 lat, 4 N/S, 5 lon, 6 E/W
  if (n < 7 || fields[2][0] != 'A' || !fields[3][0] || !fields[5][0]) return false;

  out.lat = toDegrees(fields[3]) * (fields[4][0] == 'S' ? -1.0 : 1.0);
  out.lng = toDegrees(fields[5]) * (fields[6][0] == 'W' ? -1.0 : 1.0);
  out.valid = true;
  return true;
}

bool NmeaReader::feed(char c) {
  if (c == '\r') return false;
  if (c == '$') len_ = 0;  // a new sentence always starts here
  if (c == '\n') {
    line_[len_] = 0;
    len_ = 0;
    return parseRmc(line_, fix_);
  }
  if (len_ < sizeof(line_) - 1) line_[len_++] = c;
  return false;
}

}  // namespace scooter
