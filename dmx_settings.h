#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DMX_CHANNEL_COUNT 512U

typedef struct {
    uint16_t bank_start;
    uint16_t frame_slots;
    uint8_t bank_width;
    uint8_t values[DMX_CHANNEL_COUNT];
} DmxSettings;

void dmx_settings_set_defaults(DmxSettings* settings);
bool dmx_settings_load(DmxSettings* settings);
bool dmx_settings_save(const DmxSettings* settings);
