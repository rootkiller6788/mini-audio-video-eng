#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtp.h"
#include "rtcp.h"

int main(void) {
    printf("=== Example: RTP Packet Send & Receive ===\n\n");
    /* Create RTP packet with video payload */
    rtp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.version = RTP_VERSION;
    pkt.header.payload_type = 96;
    pkt.header.sequence = 100;
    pkt.header.timestamp = 90000;
    pkt.header.ssrc = 0x12345678;
    const char *payload = "H.264 NAL Unit Data";
    pkt.payload = (const uint8_t *)payload;
    pkt.payload_len = strlen(payload);

    /* Serialize to network buffer */
    uint8_t wire[1500];
    int wire_len = rtp_serialize(&pkt, wire, sizeof(wire));
    printf("Serialized RTP packet: %d bytes\n", wire_len);

    /* Simulate network transmit, then parse on receiver */
    rtp_packet_t rx_pkt;
    if (rtp_parse(wire, (size_t)wire_len, &rx_pkt) == 0) {
        printf("Parsed RTP packet:\n");
        printf("  Version:     %d\n", rx_pkt.header.version);
        printf("  PT:          %d\n", rx_pkt.header.payload_type);
        printf("  Sequence:    %u\n", rx_pkt.header.sequence);
        printf("  Timestamp:   %u\n", rx_pkt.header.timestamp);
        printf("  SSRC:        0x%08X\n", rx_pkt.header.ssrc);
        printf("  Payload:     \"%.*s\" (%zu bytes)\n",
               (int)rx_pkt.payload_len, rx_pkt.payload, rx_pkt.payload_len);
    }

    /* Compute NTP timestamp for synchronization */
    uint32_t ntp_sec, ntp_frac;
    rtp_ts_to_ntp(pkt.header.timestamp, RTP_VIDEO_CLOCK_RATE,
                  &ntp_sec, &ntp_frac);
    printf("NTP mapping: %u.%09u seconds\n", ntp_sec,
           (unsigned)((double)ntp_frac / 4294967296.0 * 1e9));
    return 0;
}