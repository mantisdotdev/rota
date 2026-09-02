// render: share code → WAV (PRD §12). Placeholder: the engine and sound layers do not
// exist yet, so this checks its arguments, says so, and fails on purpose.
//   ./build/render "<share code>" out/<name>.wav
#include <cstdio>

namespace {

constexpr int kExpectedArgCount = 3;  // program, share code, output path
constexpr int kExitUsage = 2;
constexpr int kExitNotImplemented = 1;

}  // namespace

int main(int argc, char** argv) {
  if (argc != kExpectedArgCount) {
    std::fprintf(stderr, "usage: render \"<share code>\" out/<name>.wav\n");
    return kExitUsage;
  }
  std::fprintf(stderr, "render: placeholder, engine not implemented yet; nothing written to %s\n",
               argv[2]);
  return kExitNotImplemented;
}
