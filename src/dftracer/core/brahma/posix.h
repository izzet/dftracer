//
// Created by hariharan on 8/8/22.
//

#ifndef DFTRACER_POSIX_H
#define DFTRACER_POSIX_H

#include <brahma/brahma.h>
#include <dftracer/core/common/constants.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/md5.h>
#include <dftracer/core/utils/utils.h>
#include <fcntl.h>
#include <sys/param.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace brahma {
class POSIXDFTracer : public POSIX {
 private:
  static bool stop_trace;
  static std::shared_ptr<POSIXDFTracer> instance;
  static const int MAX_FD = 1024;
  HashType tracked_fd[MAX_FD];
  std::shared_ptr<DFTLogger> logger;
  bool trace_all_files;

  inline HashType is_traced(int fd, const char *func) {
    if (fd < 0) return NO_HASH_DEFAULT;
    HashType trace = tracked_fd[fd % MAX_FD];
    if (trace == NO_HASH_DEFAULT) {
      DFTRACER_LOG_DEBUG(
          "Calling POSIXDFTracer.is_traced for %s and"
          " fd %d trace %d",
          func, fd, trace != NO_HASH_DEFAULT);
    }
    return trace;
  }

  inline HashType is_traced(const char *filename, const char *func) {
    if (stop_trace) return NO_HASH_DEFAULT;
    if (trace_all_files) {
      return logger->hash_and_store(filename, METADATA_NAME_FILE_HASH);
    } else {
      const char *tracefile = is_traced_common(filename, func);
      if (tracefile != nullptr) {
        DFTRACER_LOG_DEBUG(
            "Calling POSIXDFTracer.is_traced with "
            "filename %s for %s trace %d",
            filename, func, tracefile != nullptr);
      }
      return logger->hash_and_store(tracefile, METADATA_NAME_FILE_HASH);
    }
  }

  inline void trace(int fd, HashType hash) {
    DFTRACER_LOG_DEBUG("Calling POSIXDFTracer.trace for %d and %d", fd, hash);
    if (fd == -1) return;
    tracked_fd[fd % MAX_FD] = hash;
  }

  inline void remove_trace(int fd) {
    DFTRACER_LOG_DEBUG("Calling POSIXDFTracer.remove_trace for %d", fd);
    if (fd == -1) return;
    tracked_fd[fd % MAX_FD] = NO_HASH_DEFAULT;
  }

 public:
  POSIXDFTracer(bool trace_all) : POSIX(), trace_all_files(trace_all) {
    DFTRACER_LOG_DEBUG("POSIX class intercepted", "");
    for (int i = 0; i < MAX_FD; ++i) tracked_fd[i] = NO_HASH_DEFAULT;
    logger = DFT_LOGGER_INIT();
  }
  /**
   * Resolve file descriptors inherited from a parent process (e.g. via
   * shell stdout redirection before execve).  At LD_PRELOAD init time,
   * fds 0-2 and any others opened by the parent shell are already open
   * but have no entry in tracked_fd[] because DFTracer never saw the
   * open() call.  This method reads /proc/self/fd to fill those gaps.
   *
   * Must be called after the Trie include/exclude sets are populated
   * and after the logger singleton is available.
   */
  void resolve_inherited_fds() {
    char link_path[64];
    char resolved[PATH_MAX];
    for (int fd = 0; fd < MAX_FD; ++fd) {
      if (tracked_fd[fd] != NO_HASH_DEFAULT) continue;
      snprintf(link_path, sizeof(link_path), "/proc/self/fd/%d", fd);
      ssize_t len = df_readlink(link_path, resolved, PATH_MAX - 1);
      if (len <= 0) continue;  // fd not open or error
      resolved[len] = '\0';
      HashType hash = is_traced(resolved, "inherited_fd");
      if (hash != NO_HASH_DEFAULT) {
        tracked_fd[fd] = hash;
        DFTRACER_LOG_INFO(
            "Resolved inherited fd %d -> %s", fd, resolved);
      }
    }
  }
  void finalize() {
    DFTRACER_LOG_DEBUG("Finalizing POSIXDFTracer", "");
    stop_trace = true;
  }
  ~POSIXDFTracer() {
    if (instance != nullptr) {
      instance.reset();
    }
  }
  static std::shared_ptr<POSIXDFTracer> get_instance(bool trace_all = false) {
    DFTRACER_LOG_DEBUG("POSIX class get_instance", "");
    if (!stop_trace && instance == nullptr) {
      instance = std::make_shared<POSIXDFTracer>(trace_all);
      POSIX::set_instance(instance);
    }
    return instance;
  }

  int open(const char *pathname, int flags, ...) override;

  int creat64(const char *path, mode_t mode) override;

  int open64(const char *path, int flags, ...) override;

  int close(int fd) override;

  ssize_t write(int fd, const void *buf, size_t count) override;

  ssize_t read(int fd, void *buf, size_t count) override;

  off_t lseek(int fd, off_t offset, int whence) override;

  off64_t lseek64(int fd, off64_t offset, int whence) override;

  ssize_t pread(int fd, void *buf, size_t count, off_t offset) override;

  ssize_t pread64(int fd, void *buf, size_t count, off64_t offset) override;

  ssize_t pwrite(int fd, const void *buf, size_t count,
                 off64_t offset) override;

  ssize_t pwrite64(int fd, const void *buf, size_t count,
                   off64_t offset) override;

  int fsync(int fd) override;

  int fdatasync(int fd) override;

  int openat(int dirfd, const char *pathname, int flags, ...) override;

  void *mmap(void *addr, size_t length, int prot, int flags, int fd,
             off_t offset) override;

  void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
               off64_t offset) override;

  int __xstat(int vers, const char *path, struct stat *buf) override;

  int __xstat64(int vers, const char *path, struct stat64 *buf) override;

  int __lxstat(int vers, const char *path, struct stat *buf) override;

  int __lxstat64(int vers, const char *path, struct stat64 *buf) override;

  int __fxstat(int vers, int fd, struct stat *buf) override;

  int __fxstat64(int vers, int fd, struct stat64 *buf) override;

  int mkdir(const char *pathname, mode_t mode) override;

  int rmdir(const char *pathname) override;

  int chdir(const char *path) override;

  int link(const char *oldpath, const char *newpath) override;

  int linkat(int fd1, const char *path1, int fd2, const char *path2,
             int flag) override;

  int unlink(const char *pathname) override;

  int symlink(const char *path1, const char *path2) override;

  int symlinkat(const char *path1, int fd, const char *path2) override;

  ssize_t readlink(const char *path, char *buf, size_t bufsize) override;

  ssize_t readlinkat(int fd, const char *path, char *buf,
                     size_t bufsize) override;

  int rename(const char *oldpath, const char *newpath) override;

  int chmod(const char *path, mode_t mode) override;

  int chown(const char *path, uid_t owner, gid_t group) override;

  int lchown(const char *path, uid_t owner, gid_t group) override;

  int utime(const char *filename, const utimbuf *buf) override;

  DIR *opendir(const char *name) override;

  int fcntl(int fd, int cmd, ...) override;

  int dup(int oldfd) override;

  int dup2(int oldfd, int newfd) override;

  int mkfifo(const char *pathname, mode_t mode) override;

  mode_t umask(mode_t mask) override;

  int access(const char *path, int amode) override;

  int faccessat(int fd, const char *path, int amode, int flag) override;

  int remove(const char *pathname) override;

  int truncate(const char *pathname, off_t length) override;

  int ftruncate(int fd, off_t length) override;

  int execl(const char *pathname, const char *arg, ...) override;

  int execlp(const char *file, const char *arg, ...) override;

  int execv(const char *pathname, char *const argv[]) override;

  int execvp(const char *file, char *const argv[]) override;

  int execvpe(const char *file, char *const argv[],
              char *const envp[]) override;

  int fork() override;

  void exit(int status) override;

  void _exit(int status) override;
};

}  // namespace brahma
#endif  // DFTRACER_POSIX_H
