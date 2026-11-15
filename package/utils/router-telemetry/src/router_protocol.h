#ifndef ROUTER_PROTOCOL_H_
#define ROUTER_PROTOCOL_H_
#include <stddef.h>
#include <stdint.h>
#define ROUTER_PROTOCOL_MAGIC_FIRST 0xA5U
#define ROUTER_PROTOCOL_MAGIC_SECOND 0x5AU
#define ROUTER_PROTOCOL_VERSION 1U
#define ROUTER_PROTOCOL_TELEMETRY 1U
#define ROUTER_PROTOCOL_HEADER_SIZE 10U
#define ROUTER_PROTOCOL_PAYLOAD_SIZE 10U
#define ROUTER_PROTOCOL_FRAME_SIZE 24U

typedef struct {
    uint32_t rx_kbps;
    uint32_t tx_kbps;
    uint8_t cpu_usage;
    uint8_t mem_usage;
} RouterTelemetry;
typedef struct {
    uint32_t sequence;
    RouterTelemetry telemetry;
} RouterFrame;
typedef enum {
    ROUTER_PROTOCOL_OK = 0,
    ROUTER_PROTOCOL_ARGUMENT,
    ROUTER_PROTOCOL_LENGTH,
    ROUTER_PROTOCOL_HEADER,
    ROUTER_PROTOCOL_CRC,
    ROUTER_PROTOCOL_RANGE
} RouterProtocolResult;
/* CRC-32/ISO-HDLC; NULL is permitted only for length zero. */
uint32_t RouterProtocol_Crc32(const uint8_t *data, size_t length);
/* Buffers must not overlap input/output objects. Output remains unchanged on
 * failure. Decode requires exactly one frame; encode writes exactly 24 bytes. */
RouterProtocolResult RouterProtocol_Encode(const RouterFrame *frame,
                                         uint8_t *out, size_t capacity);
RouterProtocolResult RouterProtocol_Decode(const uint8_t *data, size_t length,
                                         RouterFrame *out);
#endif
