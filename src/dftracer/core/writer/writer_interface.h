#ifndef DFTRACER_WRITER_INTERFACE_H
#define DFTRACER_WRITER_INTERFACE_H

#include <cstddef>
#include <dftracer/core/common/typedef.h>

namespace dftracer {
struct EventContext {
  ConstEventNameType event_name;
  ConstEventNameType category;
  ConstEventNameType phase;
  ProcessID process_id;
  ThreadID thread_id;
};

class WriterInterface {
 public:
  virtual void initialize(const char* filename) = 0;
  virtual bool control_hooks_enabled() const { return false; }
  virtual void before_write(const EventContext& event) {}
  virtual size_t write(const char* data, size_t len, bool force = false) = 0;
  virtual void finalize(int index) = 0;
  virtual ~WriterInterface() = default;
};
}  // namespace dftracer

#endif  // DFTRACER_WRITER_INTERFACE_H
