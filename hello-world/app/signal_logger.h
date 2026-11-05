#pragma once

#include <signal.h>

/* Block every catchable, non-fault signal so they can be received with
 * signal_logger_wait(). Must be called before any thread is created. */
int signal_logger_init(sigset_t* set);

/* Wait for one signal from set and log its details to syslog.
 * Returns the signal number, or -1 on error. */
int signal_logger_wait(const sigset_t* set);

/* True if the signal should terminate the application. */
int signal_logger_is_terminating(int signo);
