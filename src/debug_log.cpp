#include "debug_log.h"

#include <stdarg.h>

namespace {
String  s_lines[kDebugLogLines];
int     s_head  = 0;    // index of next slot to write
int     s_count = 0;    // number of valid entries (≤ kDebugLogLines)
}  // namespace

void debugLog(const String& line) {
  // Prefix with a seconds.millis timestamp for ordering.
  const uint32_t ms = millis();
  char ts[16];
  snprintf(ts, sizeof(ts), "[%6lu.%03lu] ",
           static_cast<unsigned long>(ms / 1000),
           static_cast<unsigned long>(ms % 1000));
  String entry = String(ts) + line;

  s_lines[s_head] = entry;
  s_head = (s_head + 1) % kDebugLogLines;
  if (s_count < kDebugLogLines) s_count++;

  Serial.println(entry);
}

void debugLogf(const char* fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  debugLog(String(buf));
}

String debugLogDump() {
  String out;
  out.reserve(s_count * 48);
  // Oldest entry is at (head - count) mod N.
  int idx = (s_head - s_count + kDebugLogLines) % kDebugLogLines;
  for (int i = 0; i < s_count; ++i) {
    out += s_lines[idx];
    out += '\n';
    idx = (idx + 1) % kDebugLogLines;
  }
  return out;
}
