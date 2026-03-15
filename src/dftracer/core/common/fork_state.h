#ifndef DFTRACER_FORK_STATE_H
#define DFTRACER_FORK_STATE_H

#include <cstdlib>
#include <cstdio>
#include <unistd.h>

namespace dftracer {
extern bool g_forked_child_process;

inline void emit_fork_breadcrumb(const char* where) {
  const char* enabled = std::getenv("DFTRACER_FORK_DEBUG");
  if (!enabled || enabled[0] == '\0' || enabled[0] == '0') return;
  std::fprintf(stderr,
               "DFT_BREADCRUMB where=%s pid=%d ppid=%d forked_child=%d\n",
               where, getpid(), getppid(), g_forked_child_process ? 1 : 0);
  std::fflush(stderr);
}
}

#endif  // DFTRACER_FORK_STATE_H
