#ifndef DFTRACER_TEST_UNIT_CHECK_H
#define DFTRACER_TEST_UNIT_CHECK_H

#include <cstdlib>
#include <iostream>

// Release builds define NDEBUG, which turns assert() into a no-op -- an
// assert-based test then passes no matter what it asserts. Use DFT_CHECK for
// anything that must actually be verified.
#define DFT_CHECK(cond)                                             \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" \
                << __LINE__ << std::endl;                           \
      std::exit(1);                                                 \
    }                                                               \
  } while (0)

#endif  // DFTRACER_TEST_UNIT_CHECK_H
