/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * - signal_logger -
 *
 * Based on hello_world. Keeps running and writes every signal it receives
 * (e.g. on stop, restart or upgrade) to the syslog, so the signals the
 * platform sends can be inspected with the system log.
 */

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "signal_logger.h"

int main(void) {
    openlog("signal_logger", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Hello World");
    printf("Hello World\n");
    fflush(stdout);

    sigset_t set;
    if (signal_logger_init(&set) != 0) {
        closelog();
        return 1;
    }
    syslog(LOG_INFO, "Started: pid=%d ppid=%d, waiting for signals", (int)getpid(), (int)getppid());

    for (;;) {
        int signo = signal_logger_wait(&set);
        if (signo < 0) {
            closelog();
            return 1;
        }
        if (signal_logger_is_terminating(signo)) {
            syslog(LOG_INFO, "Exiting on signal %d", signo);
            break;
        }
    }

    closelog();
    return 0;
}
