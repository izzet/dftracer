#include <dftracer/core/writer/zmq_writer.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace dftracer {

template <>
std::shared_ptr<ZMQWriter> Singleton<ZMQWriter>::instance = nullptr;
template <>
bool Singleton<ZMQWriter>::stop_creating_instances = false;

// Static pointer for fork handling
static ZMQWriter* writer_instance = nullptr;

// Fork handler called in child process after fork
static void on_fork_child() {
  if (writer_instance) {
    DFTRACER_LOG_DEBUG("Fork child: re-initializing ZMQ socket.", "");
    writer_instance->reconnect();
  }
}

ZMQWriter::ZMQWriter()
    : context_(nullptr), socket_(nullptr), initialized_(false), init_pid_(0) {
  DFTRACER_LOG_DEBUG("ZMQWriter.ZMQWriter", "");
}

ZMQWriter::~ZMQWriter() { finalize(0); }

void ZMQWriter::initialize(const char* filename) {
  if (initialized_) {
    DFTRACER_LOG_INFO("ZMQWriter already initialized", "");
    return;
  }

  // Use environment variable if filename is not provided
  const char* env_endpoint = std::getenv("DFTRACER_ZMQ_ENDPOINT");
  if (env_endpoint && strlen(env_endpoint) > 0) {
    endpoint_ = env_endpoint;
  } else {
    endpoint_ = filename ? filename : "tcp://localhost:5555";
  }

  try {
    context_ = std::make_unique<zmq::context_t>(1);
    socket_ =
        std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::push);

    if (!socket_) {
      DFTRACER_LOG_ERROR("Unable to create ZeroMQ socket for %s",
                         endpoint_.c_str());
      throw std::runtime_error("Failed to create ZeroMQ socket");
    }

    socket_->connect(endpoint_);
    initialized_ = true;
    init_pid_ = getpid();

    // Register fork handler for child process reconnection
    writer_instance = this;
    pthread_atfork(nullptr, nullptr, on_fork_child);

    DFTRACER_LOG_INFO("ZMQWriter connected to %s with fork handling (PID %d)",
                      endpoint_.c_str(), init_pid_);
  } catch (const zmq::error_t& e) {
    DFTRACER_LOG_ERROR("ZeroMQ error during initialization: %s", e.what());
    throw std::runtime_error(std::string("ZeroMQ initialization failed: ") +
                             e.what());
  }
}

size_t ZMQWriter::write(const char* data, size_t len, bool force) {
  if (!initialized_ || !socket_) {
    DFTRACER_LOG_ERROR("ZMQWriter not initialized or socket is null", "");
    return 0;
  }

  if (len == 0 || !data) {
    return 0;
  }

  try {
    zmq::message_t message(data, len);
    auto result = socket_->send(message, zmq::send_flags::dontwait);

    if (result) {
      DFTRACER_LOG_DEBUG("ZMQWriter sent %zu bytes", len);
      return len;
    } else {
      DFTRACER_LOG_ERROR("ZMQWriter failed to send message (would block)", "");
      return 0;
    }
  } catch (const zmq::error_t& e) {
    DFTRACER_LOG_ERROR("ZMQWriter send error: %s", e.what());
    return 0;
  }
}

void ZMQWriter::finalize(int index) {
  if (!initialized_) {
    DFTRACER_LOG_DEBUG("ZMQWriter already finalized or not initialized", "");
    return;
  }

  // Check if we're in a child process - skip finalization to avoid hang
  if (getpid() != init_pid_) {
    DFTRACER_LOG_INFO(
        "ZMQWriter::finalize called in child process (Init PID: %d, Current "
        "PID: %d). Skipping finalization.",
        init_pid_, getpid());
    // Release ownership to avoid destructor calls
    if (socket_) socket_.release();
    if (context_) context_.release();
    initialized_ = false;
    return;
  }

  DFTRACER_LOG_INFO("ZMQWriter finalizing", "");

  try {
    if (socket_) {
      socket_->close();
      socket_.reset();
    }
    if (context_) {
      context_->close();
      context_.reset();
    }
  } catch (const zmq::error_t& e) {
    DFTRACER_LOG_ERROR("ZeroMQ error during finalization: %s", e.what());
  }

  if (writer_instance == this) {
    writer_instance = nullptr;
  }

  initialized_ = false;
  DFTRACER_LOG_INFO("ZMQWriter finalized", "");
}

void ZMQWriter::reconnect() {
  DFTRACER_LOG_DEBUG("Child process starting ZMQ reconnect", "");

  // Release ownership of the inherited pointers.
  // This prevents the destructor (and zmq_close) from being called.
  // The old state is invalid and must be abandoned.
  if (socket_) socket_.release();
  if (context_) context_.release();

  DFTRACER_LOG_DEBUG("Child process abandoned inherited ZMQ state", "");

  // Create a brand new context and socket for this child process.
  try {
    context_ = std::make_unique<zmq::context_t>(1);
    socket_ =
        std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::push);

    if (!socket_) {
      DFTRACER_LOG_ERROR("Unable to create new ZeroMQ socket in child for %s",
                         endpoint_.c_str());
      initialized_ = false;
      return;
    }

    // Connect the new socket to the original destination.
    socket_->connect(endpoint_);
    init_pid_ = getpid();
    DFTRACER_LOG_INFO(
        "Child process successfully reconnected ZMQ socket to %s (PID %d)",
        endpoint_.c_str(), init_pid_);
  } catch (const zmq::error_t& e) {
    DFTRACER_LOG_ERROR("ZeroMQ error during reconnect: %s", e.what());
    initialized_ = false;
  }
}

}  // namespace dftracer
