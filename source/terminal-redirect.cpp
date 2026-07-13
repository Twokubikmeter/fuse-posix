#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <string.h>
#include "fastlog.h"

using namespace fastlog;

void relay_pty_bidirectional(int master_fd, pid_t calling_pid) {
  // Open calling process's stdin/stdout
  std::string stdin_path = "/proc/" + std::to_string(calling_pid) + "/fd/0";
  std::string stdout_path = "/proc/" + std::to_string(calling_pid) + "/fd/1";
  std::string stderr_path = "/proc/" + std::to_string(calling_pid) + "/fd/2";
  
  int calling_stdin = open(stdin_path.c_str(), O_RDONLY | O_NONBLOCK);
  int calling_stdout = open(stdout_path.c_str(), O_WRONLY);
  int calling_stderr = open(stderr_path.c_str(), O_WRONLY);
  
  if (calling_stdout == -1 || calling_stderr == -1) {
    fastlog(ERROR, "Failed to open calling process FDs: %s", strerror(errno));
    if (calling_stdin >= 0) close(calling_stdin);
    if (calling_stdout >= 0) close(calling_stdout);
    if (calling_stderr >= 0) close(calling_stderr);
    return;
  }
  
  // Set master_fd to non-blocking
  int flags = fcntl(master_fd, F_GETFL, 0);
  fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
  
  fd_set readfds;
  char buf[4096];
  ssize_t n;
  
  fastlog(ERROR, "Relaying PTY output to calling process %d", calling_pid);
  
  while (1) {
    FD_ZERO(&readfds);
    FD_SET(master_fd, &readfds);
    if (calling_stdin >= 0) {
      FD_SET(calling_stdin, &readfds);
    }
    
    struct timeval tv;
    tv.tv_sec = 1;   // 1 second timeout
    tv.tv_usec = 0;
    
    int ret = select(std::max(master_fd, calling_stdin) + 1, &readfds, nullptr, nullptr, &tv);
    
    if (ret < 0) {
      if (errno == EINTR) continue;
      break;
    }
    
    // Read from PTY and write to calling process's stdout
    if (FD_ISSET(master_fd, &readfds)) {
      n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        write(calling_stdout, buf, n);
        if (calling_stderr >= 0) {
          // Also mirror to stderr for visibility
        }
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        break;
      }
    }
    
    // Read from calling process's stdin and write to PTY
    if (calling_stdin >= 0 && FD_ISSET(calling_stdin, &readfds)) {
      n = read(calling_stdin, buf, sizeof(buf));
      if (n > 0) {
        write(master_fd, buf, n);
      } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        break;
      }
    }
  }
  
  fastlog(ERROR, "PTY relay completed");
  
  if (calling_stdin >= 0) close(calling_stdin);
  if (calling_stdout >= 0) close(calling_stdout);
  if (calling_stderr >= 0) close(calling_stderr);
}