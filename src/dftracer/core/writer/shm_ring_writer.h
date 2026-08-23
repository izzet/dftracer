// Shared-memory ring writer -- the Option C trace data plane.
//
// Drop-in sibling of MofkaWriter. Where MofkaWriter owns a Mofka producer per process (and per
// forked DataLoader worker), this owns an mmap. That single difference removes the entire sec 81
// failure class: a forked child inherits a mapping, not a connection, so there is nothing to
// re-initialise and nothing that can block.
//
// Compare the state MofkaWriter has to carry for fork (mofka_writer.h): init_pid_,
// forked_child_, and a full connection-rebuild path in initialize(). None of that appears here.
// The only fork-aware step is claiming a fresh lane, which liveflow::ShmRing does lazily with one
// atomic fetch_add.
//
// Two topics, mirroring MofkaWriter's producer_ / control_producer_ split:
//   trace ring    the batched JSON buffers BufferManager hands to write()
//   control ring  one boundary event per control trigger, emitted from after_write()
//
// See liveflow_shm/README.md and LIVEFLOW-ARCHITECTURE.md sec 6.

#ifndef DFTRACER_SHM_RING_WRITER_H
#define DFTRACER_SHM_RING_WRITER_H

#include <dftracer/core/writer/writer_interface.h>
#include <liveflow/shm_ring.h>

#include <string>
#include <vector>

namespace dftracer {

class ShmRingWriter : public WriterInterface {
 private:
  liveflow::ShmRing trace_ring_;
  liveflow::ShmRing control_ring_;
  std::string trace_seg_;
  std::string control_seg_;
  bool control_hooks_enabled_ = false;
  bool finalized_ = false;
  size_t trace_events_written_ = 0;
  std::vector<std::string> control_trigger_event_names_;

  // Reusable scratch for the [u32 meta_len][meta_json][payload] framing, so the write path never
  // allocates. Allocation on the trace path is exactly the kind of thing that turns a tracing
  // plane into a source of jitter.
  std::string scratch_;

  bool push(liveflow::ShmRing& ring, const std::string& metadata_json, const char* data,
            size_t len);

 public:
  ShmRingWriter();
  ~ShmRingWriter() override;

  void initialize(const char* filename) override;
  bool control_hooks_enabled() const override { return control_hooks_enabled_; }
  bool is_control_trigger(ConstEventNameType event_name) const override;
  void after_write(const EventContext& event) override;
  size_t write(const char* data, size_t len, bool force = false) override;
  void finalize(int index) override;
};

}  // namespace dftracer

#endif  // DFTRACER_SHM_RING_WRITER_H
