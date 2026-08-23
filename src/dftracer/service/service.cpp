#include <dftracer/service/service.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

// Atomic flag to control server running state
std::atomic<bool> running(true);

// Signal handler for SIGINT/SIGTERM to gracefully shut down the server
static void server_signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    running = false;
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <run|stop> [log_dir]" << std::endl;
    return 1;
  }

  std::string cmd = argv[1];
  std::string log_dir = (argc == 3) ? argv[2] : "/tmp";

  // Ensure log_dir ends without trailing slash
  if (!log_dir.empty() && log_dir.back() == '/') log_dir.pop_back();

  std::string pid_file_path = log_dir + "/dftracer_server.pid";
  std::string out_log_path = log_dir + "/dftracer_server.out";
  std::string err_log_path = log_dir + "/dftracer_server.err";

  if (cmd == "run") {
    // Foreground mode: no daemonize, no fork.
    // Redirect stdout/stderr to log files so output is captured.
    freopen(out_log_path.c_str(), "w", stdout);
    freopen(err_log_path.c_str(), "w", stderr);

    // Write PID file for identification
    std::ofstream pid_file(pid_file_path);
    pid_file << getpid();
    pid_file.close();

    std::signal(SIGINT, server_signal_handler);
    std::signal(SIGTERM, server_signal_handler);

    auto server = dftracer::DFTracerService();
    server.start();

    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    server.stop();
    std::remove(pid_file_path.c_str());
    return 0;
  } else if (cmd == "stop") {
    // Stop the running server by sending SIGTERM to its PID
    std::ifstream pid_file(pid_file_path);
    pid_t pid;
    if (!(pid_file >> pid)) {
      std::cerr << "No running server found." << std::endl;
      return 1;
    }
    pid_file.close();

    if (kill(pid, SIGTERM) == 0) {
      std::cout << "Sent SIGTERM to server (PID " << pid << ")." << std::endl;
      std::remove(pid_file_path.c_str());
    } else {
      std::cerr << "Failed to send SIGTERM to server." << std::endl;
      return 1;
    }
    return 0;
  } else {
    std::cerr << "Unknown command: " << cmd
              << " (expected: run, stop)" << std::endl;
    return 1;
  }
}