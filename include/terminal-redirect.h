
#ifndef TERMINAL_REDIRECT_H
#define TERMINAL_REDIRECT_H

#include <fcntl.h>

void relay_pty_bidirectional(int master_fd, pid_t calling_pid);

#endif //TERMINAL_REDIRECT_H
