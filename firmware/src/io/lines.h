#pragma once

// Splitting a text file off the card into its lines. Both the song files and the kit
// files are line-oriented (D-104, D-109), so this lives here rather than twice.
namespace io {

// NUL-terminates each line of `text` where its newline was, and fills `lines` with
// where each begins. Returns how many, or -1 when there are more than `capacity`.
// The empty piece after a trailing newline is not a line.
int split_lines(char* text, char** lines, int capacity);

}  // namespace io
