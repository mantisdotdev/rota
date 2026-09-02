// Counts every operator new in the test binary, so a test can prove a stretch of
// code allocated nothing (§12 rule 4, T-79).
#pragma once

#include <cstdint>

namespace allocation_counter {

uint64_t count();

}  // namespace allocation_counter
