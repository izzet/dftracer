#ifndef DFTRACER_ZMQ_WRITER_H
#define DFTRACER_ZMQ_WRITER_H

#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/core/writer/writer_interface.h>

#include <memory>
#include <string>
#include <vector>
#include <zmq.hpp>

namespace dftracer {

class ZMQWriter : public WriterInterface {
 private:
  std::unique_ptr<zmq::context_t> context_;
  std::unique_ptr<zmq::socket_t> socket_;
  std::string endpoint_;
  bool initialized_;
  pid_t init_pid_;
  bool control_hooks_enabled_ = false;
  std::vector<std::string> control_trigger_event_names_;

 public:
  ZMQWriter();
  ~ZMQWriter() override;

  void initialize(const char* filename) override;
  bool control_hooks_enabled() const override { return control_hooks_enabled_; }
  bool is_control_trigger(ConstEventNameType event_name) const override;
  size_t write(const char* data, size_t len, bool force = false) override;
  void finalize(int index) override;

  /**
   * @brief Re-initializes the ZMQ socket. This is intended to be called
   * in a child process after a fork().
   */
  void reconnect();
};

}  // namespace dftracer

#endif  // DFTRACER_ZMQ_WRITER_H
