#ifndef TELEMETRY_METRICS_H_
#define TELEMETRY_METRICS_H_

#include <stdint.h>
#include <stdio.h>
#include "router_protocol.h"

typedef struct {
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t cpu_total;
    uint64_t cpu_idle;
    uint64_t monotonic_ms;
    uint8_t memory_pct;
} TelemetryMetricsSnapshot;

/* Read-only parser helpers: return 1 on success, 0 on malformed/missing data. */
int TelemetryMetrics_ParseCpu(FILE *input, uint64_t *total, uint64_t *idle);
int TelemetryMetrics_ParseMemory(FILE *input, uint8_t *used_percent);
int TelemetryMetrics_RateKbps(uint64_t previous, uint64_t current,
                              uint64_t elapsed_ms, uint32_t *kbps);
int TelemetryMetrics_CpuPercent(uint64_t previous_total, uint64_t previous_idle,
                                uint64_t total, uint64_t idle, uint8_t *used_percent);
int TelemetryMetrics_ValidateInterface(const char *interface_name);
/* Returns 0 (rather than fabricated values) if an input is unavailable. */
int TelemetryMetrics_ReadSnapshot(const char *interface_name,
                                  TelemetryMetricsSnapshot *sample);
/* Counter reset, missing interval or backward monotonic time => invalid. */
int TelemetryMetrics_Compute(const TelemetryMetricsSnapshot *previous,
                             const TelemetryMetricsSnapshot *current,
                             RouterTelemetry *telemetry);
#endif
