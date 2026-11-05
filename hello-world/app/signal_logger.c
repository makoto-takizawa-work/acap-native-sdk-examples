/**
 * signal_logger - receive signals synchronously and log their details.
 *
 * Signals are blocked and taken with sigwaitinfo(), so all logging runs in
 * normal context (calling syslog() from a signal handler is not safe).
 */
#define _GNU_SOURCE
#include "signal_logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* Synchronous fault signals are left alone: blocking them is undefined and
 * the default action (core dump) is what we want. SIGKILL/SIGSTOP can never
 * be caught. */
static int is_excluded(int signo) {
    switch (signo) {
        case SIGKILL:
        case SIGSTOP:
        case SIGSEGV:
        case SIGBUS:
        case SIGFPE:
        case SIGILL:
        case SIGTRAP:
        case SIGABRT:
        case SIGSYS:
            return 1;
        default:
            return 0;
    }
}

int signal_logger_init(sigset_t* set) {
    sigemptyset(set);
    for (int signo = 1; signo < NSIG; signo++) {
        if (is_excluded(signo))
            continue;
        /* glibc reserves a couple of RT signals; sigaddset rejects them. */
        sigaddset(set, signo);
    }
    if (sigprocmask(SIG_BLOCK, set, NULL) != 0) {
        syslog(LOG_ERR, "sigprocmask failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static const char* code_name(const siginfo_t* info) {
    switch (info->si_code) {
        case SI_USER:
            return "SI_USER (kill)";
        case SI_KERNEL:
            return "SI_KERNEL";
        case SI_QUEUE:
            return "SI_QUEUE (sigqueue)";
        case SI_TIMER:
            return "SI_TIMER";
        case SI_MESGQ:
            return "SI_MESGQ";
        case SI_ASYNCIO:
            return "SI_ASYNCIO";
        case SI_SIGIO:
            return "SI_SIGIO";
        case SI_TKILL:
            return "SI_TKILL (tkill/tgkill)";
        default:
            break;
    }
    if (info->si_signo == SIGCHLD) {
        switch (info->si_code) {
            case CLD_EXITED:
                return "CLD_EXITED";
            case CLD_KILLED:
                return "CLD_KILLED";
            case CLD_DUMPED:
                return "CLD_DUMPED";
            case CLD_STOPPED:
                return "CLD_STOPPED";
            case CLD_CONTINUED:
                return "CLD_CONTINUED";
            default:
                break;
        }
    }
    return "other";
}

/* Read /proc/<pid>/comm of the sender; it may already be gone. */
static void sender_comm(pid_t pid, char* buf, size_t len) {
    snprintf(buf, len, "?");
    if (pid <= 0)
        return;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f)
        return;
    if (fgets(buf, (int)len, f))
        buf[strcspn(buf, "\n")] = '\0';
    fclose(f);
}

int signal_logger_wait(const sigset_t* set) {
    siginfo_t info;
    int signo;

    do {
        signo = sigwaitinfo(set, &info);
    } while (signo < 0 && errno == EINTR);

    if (signo < 0) {
        syslog(LOG_ERR, "sigwaitinfo failed: %s", strerror(errno));
        return -1;
    }

    char comm[64];
    sender_comm(info.si_pid, comm, sizeof(comm));

    syslog(LOG_INFO,
           "Received signal %d (SIG%s: %s) code=%d [%s] sender_pid=%d sender_uid=%d "
           "sender_comm=%s",
           signo,
           sigabbrev_np(signo) ? sigabbrev_np(signo) : "RT",
           strsignal(signo),
           info.si_code,
           code_name(&info),
           (int)info.si_pid,
           (int)info.si_uid,
           comm);
    return signo;
}

int signal_logger_is_terminating(int signo) {
    return signo == SIGTERM || signo == SIGINT || signo == SIGQUIT;
}
