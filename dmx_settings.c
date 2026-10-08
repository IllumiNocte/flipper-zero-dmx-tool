#include "dmx_settings.h"

#include <storage/storage.h>
#include <toolbox/saved_struct.h>
#include <string.h>

#define DMX_SETTINGS_PATH    APP_DATA_PATH("settings.bin")
#define DMX_SETTINGS_MAGIC   0xD8U
#define DMX_SETTINGS_VERSION 1U

void dmx_settings_set_defaults(DmxSettings* settings) {
    settings->bank_start = 0;
    settings->frame_slots = 4;
    settings->bank_width = 4;
    memset(settings->values, 0, sizeof(settings->values));
}

static bool dmx_settings_validate(DmxSettings* settings) {
    if(settings->bank_width != 4U && settings->bank_width != 6U) return false;
    if(settings->frame_slots == 0U || settings->frame_slots > DMX_CHANNEL_COUNT) return false;
    if(settings->bank_start + settings->bank_width > DMX_CHANNEL_COUNT) return false;

    const uint16_t bank_end = settings->bank_start + settings->bank_width;
    if(settings->frame_slots < bank_end) settings->frame_slots = bank_end;
    return true;
}

bool dmx_settings_load(DmxSettings* settings) {
    dmx_settings_set_defaults(settings);
    if(!saved_struct_load(
           DMX_SETTINGS_PATH,
           settings,
           sizeof(DmxSettings),
           DMX_SETTINGS_MAGIC,
           DMX_SETTINGS_VERSION)) {
        return false;
    }

    if(!dmx_settings_validate(settings)) {
        dmx_settings_set_defaults(settings);
        return false;
    }
    return true;
}

bool dmx_settings_save(const DmxSettings* settings) {
    return saved_struct_save(
        DMX_SETTINGS_PATH, settings, sizeof(DmxSettings), DMX_SETTINGS_MAGIC, DMX_SETTINGS_VERSION);
}
