#include "router_protocol.h"

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}
uint32_t RouterProtocol_Crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xEDB88320) : 0U);
    }
    return crc ^ UINT32_MAX;
}
RouterProtocolResult RouterProtocol_Encode(const RouterFrame *frame,
                                         uint8_t *out, size_t capacity)
{
    if (frame == NULL || out == NULL) return ROUTER_PROTOCOL_ARGUMENT;
    if (capacity < ROUTER_PROTOCOL_FRAME_SIZE) return ROUTER_PROTOCOL_LENGTH;
    if (frame->telemetry.cpu_usage > 100U || frame->telemetry.mem_usage > 100U)
        return ROUTER_PROTOCOL_RANGE;
    out[0] = ROUTER_PROTOCOL_MAGIC_FIRST;
    out[1] = ROUTER_PROTOCOL_MAGIC_SECOND;
    out[2] = ROUTER_PROTOCOL_VERSION;
    out[3] = ROUTER_PROTOCOL_TELEMETRY;
    write32(out + 4, frame->sequence);
    out[8] = ROUTER_PROTOCOL_PAYLOAD_SIZE;
    out[9] = 0U;
    write32(out + 10, frame->telemetry.rx_kbps);
    write32(out + 14, frame->telemetry.tx_kbps);
    out[18] = frame->telemetry.cpu_usage;
    out[19] = frame->telemetry.mem_usage;
    write32(out + 20, RouterProtocol_Crc32(out + 2, 18U));
    return ROUTER_PROTOCOL_OK;
}
RouterProtocolResult RouterProtocol_Decode(const uint8_t *data, size_t length,
                                         RouterFrame *out)
{
    if (data == NULL || out == NULL) return ROUTER_PROTOCOL_ARGUMENT;
    if (length != ROUTER_PROTOCOL_FRAME_SIZE) return ROUTER_PROTOCOL_LENGTH;
    if (data[0] != ROUTER_PROTOCOL_MAGIC_FIRST ||
        data[1] != ROUTER_PROTOCOL_MAGIC_SECOND ||
        data[2] != ROUTER_PROTOCOL_VERSION || data[3] != ROUTER_PROTOCOL_TELEMETRY)
        return ROUTER_PROTOCOL_HEADER;
    if (data[8] != ROUTER_PROTOCOL_PAYLOAD_SIZE || data[9] != 0U)
        return ROUTER_PROTOCOL_LENGTH;
    if (read32(data + 20) != RouterProtocol_Crc32(data + 2, 18U))
        return ROUTER_PROTOCOL_CRC;
    if (data[18] > 100U || data[19] > 100U) return ROUTER_PROTOCOL_RANGE;
    RouterFrame decoded;
    decoded.sequence = read32(data + 4);
    decoded.telemetry.rx_kbps = read32(data + 10);
    decoded.telemetry.tx_kbps = read32(data + 14);
    decoded.telemetry.cpu_usage = data[18];
    decoded.telemetry.mem_usage = data[19];
    *out = decoded;
    return ROUTER_PROTOCOL_OK;
}
