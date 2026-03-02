#ifndef DFTRACER_MOFKA_WRITER_H
#define DFTRACER_MOFKA_WRITER_H

#include <dftracer/core/writer/writer_interface.h>

#include <diaspora/Driver.hpp>
#include <diaspora/TopicHandle.hpp>
#include <memory>
#include <string>
#include <vector>

namespace dftracer {

class MofkaWriter : public WriterInterface {
 private:
  std::string group_file_;
  std::string topic_name_;
  std::string control_topic_name_;
  std::unique_ptr<diaspora::Driver> driver_;
  std::unique_ptr<diaspora::Producer> producer_;
  std::unique_ptr<diaspora::Producer> control_producer_;
  std::unique_ptr<diaspora::TopicHandle> topic_;
  std::unique_ptr<diaspora::TopicHandle> control_topic_;
  pid_t init_pid_ = 0;
  size_t flush_every_n_writes_ = 0;
  size_t writes_since_flush_ = 0;
  size_t trace_events_written_ = 0;
  bool control_hooks_enabled_ = false;
  std::vector<std::string> control_trigger_event_names_;

 public:
  MofkaWriter();
  ~MofkaWriter() override;

  void initialize(const char* filename) override;
  bool control_hooks_enabled() const override { return control_hooks_enabled_; }
  void before_write(const EventContext& event) override;
  size_t write(const char* data, size_t len, bool force = false) override;
  void finalize(int index) override;
};

}  // namespace dftracer

#endif  // DFTRACER_MOFKA_WRITER_H
