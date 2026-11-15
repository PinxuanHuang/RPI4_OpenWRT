#define _POSIX_C_SOURCE 200809L
#include "telemetry_metrics.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <net/if.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int TelemetryMetrics_ValidateInterface(const char *name)
{
    if (name == NULL) return 0;
    size_t len = strlen(name);
    if (len == 0U || len >= IF_NAMESIZE || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return 0;
    for (size_t i = 0U; i < len; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':'))
            return 0;
    }
    return 1;
}

static int parse_unsigned(const char *p, uint64_t *result)
{
    if (p == NULL || result == NULL) return 0;
    while (isspace((unsigned char)*p)) ++p;
    if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(p, &end, 10);
    if (errno == ERANGE || end == p) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end != '\0') return 0;
    *result = (uint64_t)value;
    return 1;
}

static int read_counter(const char *path, uint64_t *value)
{
    char buf[96];
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;
    int ok = fgets(buf, sizeof buf, f) != NULL && parse_unsigned(buf, value);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

int TelemetryMetrics_ParseCpu(FILE *input, uint64_t *total, uint64_t *idle)
{
    if (input == NULL || total == NULL || idle == NULL) return 0;
    char line[512];
    if (fgets(line, sizeof line, input) == NULL ||
        strncmp(line, "cpu ", 4) != 0) return 0;
    const char *p = line + 4;
    uint64_t sum = 0U, idle_sum = 0U;
    unsigned count = 0U;
    while (*p != '\0' && count < 8U) {
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0') break;
        if (!isdigit((unsigned char)*p)) return 0;
        errno = 0;
        char *end = NULL;
        unsigned long long n = strtoull(p, &end, 10);
        if (end == p || errno == ERANGE) return 0;
        if (*end != '\0' && !isspace((unsigned char)*end)) return 0;
        uint64_t value = (uint64_t)n;
        if (UINT64_MAX - sum < value) return 0;
        sum += value;
        if (count == 3U || count == 4U) {
            if (UINT64_MAX - idle_sum < value) return 0;
            idle_sum += value; /* idle + iowait; exclude guest double-count */
        }
        p = end;
        ++count;
    }
    if (count < 4U || idle_sum > sum) return 0;
    *total = sum;
    *idle = idle_sum;
    return 1;
}

int TelemetryMetrics_ParseMemory(FILE *input, uint8_t *used_percent)
{
    if (input == NULL || used_percent == NULL) return 0;
    char line[256];
    uint64_t total = 0U, available = 0U;
    int have_total = 0, have_available = 0;
    while (fgets(line, sizeof line, input) != NULL) {
        char key[32], unit[16];
        unsigned long long value = 0ULL;
        if (sscanf(line, "%31s %llu %15s", key, &value, unit) != 3 ||
            strcmp(unit, "kB") != 0) continue;
        if (strcmp(key, "MemTotal:") == 0) {
            total = (uint64_t)value;
            have_total = 1;
        } else if (strcmp(key, "MemAvailable:") == 0) {
            available = (uint64_t)value;
            have_available = 1;
        }
    }
    if (!have_total || !have_available || total == 0U || available > total)
        return 0;
    /* The floating-point computation avoids uint64 multiplication overflow. */
    double pct = ((double)(total - available) * 100.0) / (double)total;
    if (pct < 0.0 || pct > 100.0) return 0;
    *used_percent = (uint8_t)(pct + 0.5);
    return 1;
}

int TelemetryMetrics_RateKbps(uint64_t previous, uint64_t current,
                              uint64_t elapsed_ms, uint32_t *kbps)
{
    if (kbps == NULL || elapsed_ms == 0U || current < previous) return 0;
    const uint64_t delta = current - previous;
    /* (bytes * 8 bits/byte) / (milliseconds) == decimal kilobits/second. */
    if (delta > UINT64_MAX / 8U) {
        *kbps = UINT32_MAX;
        return 1;
    }
    const uint64_t rate = (delta * 8U) / elapsed_ms;
    *kbps = rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
    return 1;
}

int TelemetryMetrics_CpuPercent(uint64_t prev_total, uint64_t prev_idle,
                                uint64_t total, uint64_t idle, uint8_t *used)
{
    if (used == NULL || total <= prev_total || idle < prev_idle) return 0;
    uint64_t delta = total - prev_total;
    uint64_t idle_delta = idle - prev_idle;
    if (idle_delta > delta) return 0;
    double pct = (double)(delta - idle_delta) * 100.0 / (double)delta;
    *used = (uint8_t)(pct + 0.5);
    return 1;
}

int TelemetryMetrics_ReadSnapshot(const char *interface_name,
                                  TelemetryMetricsSnapshot *sample)
{
    if (sample == NULL || !TelemetryMetrics_ValidateInterface(interface_name))
        return 0;
    TelemetryMetricsSnapshot next = {0};
    char path[128];
    int len = snprintf(path, sizeof path,
                       "/sys/class/net/%s/statistics/rx_bytes", interface_name);
    if (len < 0 || (size_t)len >= sizeof path ||
        !read_counter(path, &next.rx_bytes)) return 0;
    len = snprintf(path, sizeof path,
                   "/sys/class/net/%s/statistics/tx_bytes", interface_name);
    if (len < 0 || (size_t)len >= sizeof path ||
        !read_counter(path, &next.tx_bytes)) return 0;
    FILE *f = fopen("/proc/stat", "r");
    if (f == NULL) return 0;
    int ok = TelemetryMetrics_ParseCpu(f, &next.cpu_total, &next.cpu_idle);
    if (fclose(f) != 0) ok = 0;
    if (!ok) return 0;
    f = fopen("/proc/meminfo", "r");
    if (f == NULL) return 0;
    ok = TelemetryMetrics_ParseMemory(f, &next.memory_pct);
    if (fclose(f) != 0) ok = 0;
    if (!ok) return 0;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0 || ts.tv_sec < 0)
        return 0;
    next.monotonic_ms = (uint64_t)ts.tv_sec * 1000U +
                        (uint64_t)ts.tv_nsec / 1000000U;
    *sample = next;
    return 1;
}

int TelemetryMetrics_Compute(const TelemetryMetricsSnapshot *previous,
                             const TelemetryMetricsSnapshot *current,
                             RouterTelemetry *telemetry)
{
    if (previous == NULL || current == NULL || telemetry == NULL ||
        current->monotonic_ms <= previous->monotonic_ms) return 0;
    RouterTelemetry next = {0};
    uint64_t duration = current->monotonic_ms - previous->monotonic_ms;
    if (!TelemetryMetrics_RateKbps(previous->rx_bytes, current->rx_bytes,
                                   duration, &next.rx_kbps) ||
        !TelemetryMetrics_RateKbps(previous->tx_bytes, current->tx_bytes,
                                   duration, &next.tx_kbps) ||
        !TelemetryMetrics_CpuPercent(previous->cpu_total, previous->cpu_idle,
                                     current->cpu_total, current->cpu_idle,
                                     &next.cpu_usage)) return 0;
    if (current->memory_pct > 100U) return 0;
    next.mem_usage = current->memory_pct;
    *telemetry = next;
    return 1;
}
