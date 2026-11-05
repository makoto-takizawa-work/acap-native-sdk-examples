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
 * This application writes "Hello World!" to the syslog.
 *
 */

#include <stdio.h>
#include <syslog.h>

/***** Main function *********************************************************/

/**
 * brief Main function.
 *
 * This main function writes "Hello World" to the syslog and stdout.
 */
int main(void) {
    openlog("signal_logger", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Hello World");
    printf("Hello World\n");
    closelog();
    return 0;
}
