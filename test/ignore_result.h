#ifndef DFTRACER_TEST_IGNORE_RESULT_H
#define DFTRACER_TEST_IGNORE_RESULT_H

/* These tests call I/O purely to generate trace events and have no use for the
 * result. warn_unused_result is not silenced by a (void) cast; assigning the
 * result is. __typeof__ keeps this usable from both the C and C++ tests. */
#define DFT_IGNORE(expr)              \
  do {                                \
    __typeof__(expr) _dft_r = (expr); \
    (void)_dft_r;                     \
  } while (0)

#endif /* DFTRACER_TEST_IGNORE_RESULT_H */
