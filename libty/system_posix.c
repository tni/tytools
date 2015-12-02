/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ty/common.h"
#include "compat.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#ifdef __APPLE__
    #include <mach/mach_time.h>
    #include <sys/select.h>
#else
    #include <poll.h>
#endif
#include "ty/system.h"

struct child_report {
    ty_err err;
    char msg[512];
};

static struct termios orig_termios;
static bool saved_termios;

#ifdef __APPLE__

uint64_t ty_millis(void)
{
    static mach_timebase_info_data_t tb;
    if (!tb.numer)
        mach_timebase_info(&tb);

    return (uint64_t)mach_absolute_time() * tb.numer / tb.denom / 1000000;
}

#else

uint64_t ty_millis(void)
{
    struct timespec ts;
    int r;

#ifdef CLOCK_MONOTONIC_RAW
    r = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    r = clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    assert(!r);

    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 10000000;
}

#endif

void ty_delay(unsigned int ms)
{
    struct timespec t, rem;
    int r;

    t.tv_sec = (int)(ms / 1000);
    t.tv_nsec = (int)((ms % 1000) * 1000000);

    do {
        r = nanosleep(&t, &rem);
        if (r < 0) {
            if (errno != EINTR) {
                ty_error(TY_ERROR_SYSTEM, "nanosleep() failed: %s", strerror(errno));
                return;
            }

            t = rem;
        }
    } while (r);
}

#ifdef __APPLE__

int ty_poll(const ty_descriptor_set *set, int timeout)
{
    assert(set);
    assert(set->count);
    assert(set->count <= 64);

    fd_set fds;
    uint64_t start;
    struct timeval tv;
    int r;

    FD_ZERO(&fds);
    for (unsigned int i = 0; i < set->count; i++)
        FD_SET(set->desc[i], &fds);

    start = ty_millis();
restart:
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
    }

    r = select(FD_SETSIZE, &fds, NULL, NULL, timeout >= 0 ? &tv : NULL);
    if (r < 0) {
        switch (errno) {
        case EINTR:
            timeout = ty_adjust_timeout(timeout, start);
            goto restart;
        case ENOMEM:
            return ty_error(TY_ERROR_MEMORY, NULL);
        }
        return ty_error(TY_ERROR_SYSTEM, "poll() failed: %s", strerror(errno));
    }
    if (!r)
        return 0;

    for (unsigned int i = 0; i < set->count; i++) {
        if (FD_ISSET(set->desc[i], &fds))
            return set->id[i];
    }
    assert(false);
}

#else

int ty_poll(const ty_descriptor_set *set, int timeout)
{
    assert(set);
    assert(set->count);
    assert(set->count <= 64);

    struct pollfd pfd[64];
    uint64_t start;
    int r;

    for (unsigned int i = 0; i < set->count; i++) {
        pfd[i].events = POLLIN;
        pfd[i].fd = set->desc[i];
    }

    if (timeout < 0)
        timeout = -1;

    start = ty_millis();
restart:
    r = poll(pfd, (nfds_t)set->count, ty_adjust_timeout(timeout, start));
    if (r < 0) {
        switch (errno) {
        case EINTR:
            goto restart;
        case ENOMEM:
            return ty_error(TY_ERROR_MEMORY, NULL);
        }
        return ty_error(TY_ERROR_SYSTEM, "poll() failed: %s", strerror(errno));
    }
    if (!r)
        return 0;

    for (unsigned int i = 0; i < set->count; i++) {
        if (pfd[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
            return set->id[i];
    }
    assert(false);
}

#endif

bool ty_terminal_available(ty_descriptor desc)
{
    return isatty(desc);
}

int ty_terminal_setup(int flags)
{
    struct termios tio;
    int r;

    r = tcgetattr(STDIN_FILENO, &tio);
    if (r < 0) {
        if (errno == ENOTTY)
            return ty_error(TY_ERROR_UNSUPPORTED, "Not a terminal");
        return ty_error(TY_ERROR_SYSTEM, "tcgetattr() failed: %s", strerror(errno));
    }

    if (!saved_termios) {
        orig_termios = tio;
        saved_termios = true;

        atexit(ty_terminal_restore);
    }

    if (flags & TY_TERMINAL_RAW) {
        cfmakeraw(&tio);
        tio.c_oflag |= OPOST | ONLCR;
        tio.c_lflag |= ISIG;
    } else {
        tio.c_iflag = TTYDEF_IFLAG;
        tio.c_oflag = TTYDEF_OFLAG;
        tio.c_lflag = TTYDEF_LFLAG;
        tio.c_cflag = TTYDEF_CFLAG;
    }
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    tio.c_lflag |= ECHO;
    if (flags & TY_TERMINAL_SILENT)
        tio.c_lflag &= (unsigned int)~ECHO;

    r = tcsetattr(STDIN_FILENO, TCSADRAIN, &tio);
    if (r < 0)
        return ty_error(TY_ERROR_SYSTEM, "tcsetattr() failed: %s", strerror(errno));

    return 0;
}

void ty_terminal_restore(void)
{
    if (!saved_termios)
        return;

    tcsetattr(STDIN_FILENO, TCSADRAIN, &orig_termios);
}