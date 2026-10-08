#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct DmxReceiver DmxReceiver;

typedef struct {
    bool ready;
    uint8_t start_code;
    uint16_t slots;
    uint8_t channels[4];
    uint32_t frames;
    uint32_t breaks;
    uint32_t noise_errors;
    uint32_t overrun_errors;
    uint32_t parity_errors;
    bool timing_ready;
    uint32_t timing_breaks;
    uint32_t break_us;
    uint32_t mab_us;
    uint32_t period_us;
    uint32_t short_breaks;
    uint32_t short_mabs;
} DmxRxSnapshot;

DmxReceiver* dmx_receiver_alloc(void);
void dmx_receiver_free(DmxReceiver* receiver);
void dmx_receiver_get_snapshot(
    DmxReceiver* receiver,
    DmxRxSnapshot* snapshot,
    uint16_t first_channel);
bool dmx_receiver_send_frame(DmxReceiver* receiver, const uint8_t* channels, uint16_t count);
void dmx_receiver_stop_tx(DmxReceiver* receiver);
