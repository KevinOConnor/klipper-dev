// TTY based IO
//
// Copyright (C) 2017-2025  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <errno.h> // errno
#include <fcntl.h> // fcntl
#include <poll.h> // poll
#include <pty.h> // openpty
#include <stdio.h> // fprintf
#include <string.h> // memmove
#include <sys/stat.h> // chmod
#include <time.h> // struct timespec
#include <unistd.h> // ttyname
#include <pthread.h> // pthread_create
#include "board/irq.h" // irq_wait
#include "board/misc.h" // console_sendf
#include "command.h" // command_find_block
#include "internal.h" // console_setup
#include "sched.h" // sched_wake_task

// Report 'errno' in a message written to stderr
void
report_errno(char *where, int rc)
{
    int e = errno;
    fprintf(stderr, "Got error %d in %s: (%d)%s\n", rc, where, e, strerror(e));
}


/****************************************************************
 * Console reading background thread
 ****************************************************************/

// Global storage for input command reading
static struct {
    struct task_wake console_wake;
    uint8_t receive_buf[4096];

    // Main input file
    int fd;

    // All variables below must be protected by lock
    pthread_mutex_t lock;

    int receive_pos, force_shutdown;
} ConsoleInfo;

// Sleep until a signal received (waking early for console input if needed)
static void *
console_thread(void *data)
{
    int MP_TTY_IDX = 0;
    struct pollfd main_pfd[1];
    main_pfd[MP_TTY_IDX].fd = ConsoleInfo.fd;

    uint8_t *receive_buf = ConsoleInfo.receive_buf;
    for (;;) {
        main_pfd[MP_TTY_IDX].events = POLLIN;
        int ret = poll(main_pfd, ARRAY_SIZE(main_pfd), -1);
        if (ret <= 0) {
            if (errno != EINTR)
                report_errno("poll main_pfd", ret);
            return NULL;
        }
        if (!main_pfd[MP_TTY_IDX].revents)
            continue;
        sched_wake_task(&ConsoleInfo.console_wake);

        // Read data
        pthread_mutex_lock(&ConsoleInfo.lock);
        int receive_pos = ConsoleInfo.receive_pos;
        pthread_mutex_unlock(&ConsoleInfo.lock);
        uint8_t readsize = sizeof(ConsoleInfo.receive_buf) - receive_pos;
        if (readsize <= 0) {
            usleep(10); // XXX
            continue;
        }
        ret = read(main_pfd[MP_TTY_IDX].fd, &receive_buf[receive_pos]
                   , readsize);
        if (ret < 0) {
            if (errno == EWOULDBLOCK) {
                continue;
            } else {
                report_errno("read", ret);
                return NULL;
            }
        }

        // Check for forced shutdown indicator
        if (ret == 15 && receive_buf[receive_pos+14] == '\n'
            && memcmp(&receive_buf[receive_pos], "FORCE_SHUTDOWN\n", 15) == 0) {
            pthread_mutex_lock(&ConsoleInfo.lock);
            ConsoleInfo.force_shutdown = 1;
            timer_wake_task_from_thread(&ConsoleInfo.console_wake);
            pthread_mutex_unlock(&ConsoleInfo.lock);
            continue;
        }

        // Add to buffer
        pthread_mutex_lock(&ConsoleInfo.lock);
        int new_receive_pos = ConsoleInfo.receive_pos;
        if (new_receive_pos != receive_pos)
            memmove(&receive_buf[new_receive_pos], &receive_buf[receive_pos]
                    , receive_pos - new_receive_pos);
        ConsoleInfo.receive_pos = new_receive_pos + ret;

        timer_wake_task_from_thread(&ConsoleInfo.console_wake);

        pthread_mutex_unlock(&ConsoleInfo.lock);
    }
}


/****************************************************************
 * Console handling
 ****************************************************************/

void *
console_receive_buffer(void)
{
    return ConsoleInfo.receive_buf;
}

// Process any incoming commands
void
console_task(void)
{
    if (!sched_check_wake(&ConsoleInfo.console_wake))
        return;

    pthread_mutex_lock(&ConsoleInfo.lock);
    if (ConsoleInfo.force_shutdown) {
        ConsoleInfo.force_shutdown = 0;
        pthread_mutex_unlock(&ConsoleInfo.lock);
        shutdown("Force shutdown command");
    }

    // Find and dispatch message blocks in the input
    uint8_t *receive_buf = ConsoleInfo.receive_buf;
    int len = ConsoleInfo.receive_pos;
    uint_fast8_t pop_count, msglen = len > MESSAGE_MAX ? MESSAGE_MAX : len;
    int ret = command_find_and_dispatch(receive_buf, msglen, &pop_count);
    if (ret) {
        len -= pop_count;
        if (len) {
            memmove(receive_buf, &receive_buf[pop_count], len);
            sched_wake_task(&ConsoleInfo.console_wake);
        }
    }
    ConsoleInfo.receive_pos = len;
    pthread_mutex_unlock(&ConsoleInfo.lock);
}
DECL_TASK(console_task);

// Encode and transmit a "response" message
void
console_sendf(const struct command_encoder *ce, va_list args)
{
    // Generate message
    uint8_t buf[MESSAGE_MAX];
    uint_fast8_t msglen = command_encode_and_frame(buf, ce, args);

    // Transmit message
    int ret = write(ConsoleInfo.fd, buf, msglen);
    if (ret < 0)
        report_errno("write", ret);
}


/****************************************************************
 * Setup
 ****************************************************************/

int
set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        report_errno("fcntl getfl", flags);
        return -1;
    }
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        report_errno("fcntl setfl", flags);
        return -1;
    }
    return 0;
}

int
set_close_on_exec(int fd)
{
    int ret = fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (ret < 0) {
        report_errno("fcntl set cloexec", ret);
        return -1;
    }
    return 0;
}

int
console_setup(char *name)
{
    // Open pseudo-tty
    struct termios ti;
    memset(&ti, 0, sizeof(ti));
    int mfd, sfd, ret = openpty(&mfd, &sfd, NULL, &ti, NULL);
    if (ret) {
        report_errno("openpty", ret);
        return -1;
    }
    ret = set_non_blocking(mfd);
    if (ret)
        return -1;
    ret = set_close_on_exec(mfd);
    if (ret)
        return -1;
    ret = set_close_on_exec(sfd);
    if (ret)
        return -1;
    ConsoleInfo.fd = mfd;

    // Create symlink to tty
    unlink(name);
    char *tname = ttyname(sfd);
    if (!tname) {
        report_errno("ttyname", 0);
        return -1;
    }
    ret = symlink(tname, name);
    if (ret) {
        report_errno("symlink", ret);
        return -1;
    }
    ret = chmod(tname, 0660);
    if (ret) {
        report_errno("chmod", ret);
        return -1;
    }

    // Make sure stderr is non-blocking
    ret = set_non_blocking(STDERR_FILENO);
    if (ret)
        return -1;

    // Create background reading thread
    ret = pthread_mutex_init(&ConsoleInfo.lock, NULL);
    if (ret)
        return -1;

    pthread_t reader_tid; // Not used
    timer_disable_signals();
    ret = pthread_create(&reader_tid, NULL, console_thread, NULL);
    timer_enable_signals();
    if (ret)
        return -1;

    return 0;
}
