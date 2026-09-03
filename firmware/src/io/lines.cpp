#include "io/lines.h"

namespace io {

int split_lines(char* text, char** lines, int capacity) {
  int count = 0;
  char* start = text;
  for (char* c = text;; ++c) {
    if (*c != '\n' && *c != '\0') continue;
    const bool end = *c == '\0';
    if (end && c == start) break;  // the text ended with its last newline
    if (count == capacity) return -1;
    *c = '\0';
    lines[count++] = start;
    start = c + 1;
    if (end) break;
  }
  return count;
}

}  // namespace io
