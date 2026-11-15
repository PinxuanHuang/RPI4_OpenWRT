#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "telemetry_metrics.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void on_signal(int signum)
{
    (void)signum;
    stopping = 1;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
        "Usage: %s --interface IFACE (--device /dev/ttyXXX | --stdout) "
        "[--interval-ms 1000] [--count N] [--verbose]\n"
        "--count 0 means unlimited; --stdout emits ONLY binary 24-byte frames.\n",
        program);
}

static int parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || *text == '\0' || *text == '-') return 0;
    errno = 0;
    char *end = NULL;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || end == text || n > UINT32_MAX)
        return 0;
    *value = (uint32_t)n;
    return 1;
}

static int configure_serial(int fd)
{
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return 0;
    cfmakeraw(&tty);
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    tty.c_cflag |= (CS8 | CLOCAL | CREAD);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (cfsetispeed(&tty, B115200) != 0 ||
        cfsetospeed(&tty, B115200) != 0 ||
        tcsetattr(fd, TCSANOW, &tty) != 0) return 0;
    return 1;
}

static int open_serial(const char *device)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    if (!configure_serial(fd)) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

static int write_frame(int fd, const uint8_t *data, size_t length)
{
    size_t written = 0U;
    /* The complete frame has a bounded deadline. Partial writes are never
     * mistaken for success. A failed link is reopened next sampling period. */
    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) return 0;
    while (written < length && !stopping) {
        ssize_t n = write(fd, data + written, length - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return 0;
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
        int64_t elapsed = (int64_t)(now.tv_sec - started.tv_sec) * 1000 +
                          (now.tv_nsec - started.tv_nsec) / 1000000;
        if (elapsed >= 200) { errno = ETIMEDOUT; return 0; }
        struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
        int ready = poll(&pfd, 1, (int)(200 - elapsed));
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) { errno = ETIMEDOUT; return 0; }
        if (ready < 0) return 0;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO; return 0;
        }
    }
    if (written != length) errno = EINTR;
    return written == length;
}

static void sleep_interval(uint32_t milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000U),
        .tv_nsec = (long)(milliseconds % 1000U) * 1000000L
    };
    while (!stopping && nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) break;
    }
}

int main(int argc, char **argv)
{
    const char *iface = NULL, *device = NULL;
    uint32_t interval_ms = 1000U, max_frames = 0U;
    int stdout_mode = 0, verbose = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc)
            iface = argv[++i];
        else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
            device = argv[++i];
        else if (strcmp(argv[i], "--stdout") == 0)
            stdout_mode = 1;
        else if (strcmp(argv[i], "--verbose") == 0)
            verbose = 1;
        else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &interval_ms) || interval_ms < 200U ||
                interval_ms > 60000U) { usage(stderr, argv[0]); return 2; }
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &max_frames)) {
                usage(stderr, argv[0]); return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]); return 0;
        } else {
            usage(stderr, argv[0]); return 2;
        }
    }
    if (!TelemetryMetrics_ValidateInterface(iface) ||
        (stdout_mode != 0) == (device != NULL)) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (device != NULL && device[0] != '/') {
        fprintf(stderr, "--device needs an absolute tty path\n");
        return 2;
    }
    struct sigaction action = {.sa_handler = on_signal};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGPIPE, &(struct sigaction){.sa_handler = SIG_IGN}, NULL) != 0) {
        perror("sigaction"); return 1;
    }

    int fd = stdout_mode ? STDOUT_FILENO : -1;
    TelemetryMetricsSnapshot previous;
    int have_previous = TelemetryMetrics_ReadSnapshot(iface, &previous);
    if (!have_previous) fprintf(stderr, "no initial stats for %s; retrying\n", iface);
    uint32_t sequence = 0U, sent = 0U;
    while (!stopping && (max_frames == 0U || sent < max_frames)) {
        sleep_interval(interval_ms);
        if (stopping) break;
        TelemetryMetricsSnapshot current;
        if (!TelemetryMetrics_ReadSnapshot(iface, &current)) {
            fprintf(stderr, "metrics unavailable (%s); skipping interval\n", iface);
            have_previous = 0;
            continue;
        }
        if (!have_previous) {
            previous = current;
            have_previous = 1;
            continue;
        }
        RouterTelemetry telemetry;
        if (!TelemetryMetrics_Compute(&previous, &current, &telemetry)) {
            fprintf(stderr, "counter reset/invalid sample; rebaselining\n");
            previous = current;
            continue;
        }
        previous = current;
        RouterFrame frame = {.sequence = sequence, .telemetry = telemetry};
        uint8_t bytes[ROUTER_PROTOCOL_FRAME_SIZE];
        if (RouterProtocol_Encode(&frame, bytes, sizeof bytes) != ROUTER_PROTOCOL_OK) {
            fprintf(stderr, "protocol encode failed\n");
            continue;
        }
        if (!stdout_mode && fd < 0) {
            fd = open_serial(device);
            if (fd < 0) {
                fprintf(stderr, "serial open %s: %s (retry next interval)\n",
                        device, strerror(errno));
                continue;
            }
        }
        if (!write_frame(fd, bytes, sizeof bytes)) {
            fprintf(stderr, "serial/output write failed: %s\n", strerror(errno));
            if (!stdout_mode) {
                (void)close(fd);
                fd = -1;
            } else return 1;
            continue;
        }
        if (verbose) fprintf(stderr,
            "seq=%" PRIu32 " rx=%" PRIu32 "kbps tx=%" PRIu32
            "kbps cpu=%u%% mem=%u%%\n", frame.sequence,
            telemetry.rx_kbps, telemetry.tx_kbps,
            (unsigned)telemetry.cpu_usage, (unsigned)telemetry.mem_usage);
        ++sequence;
        ++sent;
    }
    if (!stdout_mode && fd >= 0) (void)close(fd);
    return 0;
}
