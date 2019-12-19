#pragma once

// We keep these in a separate header to avoid leaking to clients
// Missing/non-matching defines for Apple
#ifdef __APPLE__
#ifdef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif
#define MSG_NOSIGNAL 0x2000
#endif
