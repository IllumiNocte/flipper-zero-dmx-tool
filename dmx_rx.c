#include "dmx_rx.h"

#include <expansion/expansion.h>
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#define DMX_BAUD_RATE          250000UL
#define DMX_MAX_SLOTS          512U
#define DMX_BREAK_CANDIDATE_US 44U
#define DMX_BREAK_MIN_US       88U
#define DMX_MAB_MIN_US         8U

struct DmxReceiver {
    Expansion* expansion;
    FuriHalSerialHandle* serial;
    bool expansion_disabled;
    bool ready;
    bool tx_enabled;
    uint8_t tx_frame[DMX_MAX_SLOTS + 1];

    volatile bool waiting_for_start_code;
    volatile bool frame_active;
    volatile uint8_t current_start_code;
    volatile uint16_t current_slots;
    uint8_t current_buffer;
    volatile uint8_t channel_buffers[2][DMX_MAX_SLOTS];

    volatile uint32_t sequence;
    volatile uint8_t last_start_code;
    volatile uint16_t last_slots;
    volatile uint8_t published_buffer;
    volatile uint32_t frames;
    volatile uint32_t breaks;
    volatile uint32_t noise_errors;
    volatile uint32_t overrun_errors;
    volatile uint32_t parity_errors;

    uint32_t cycles_per_us;
    volatile bool edge_low_seen;
    volatile bool waiting_for_mab_end;
    volatile bool previous_break_seen;
    volatile uint32_t low_start_cycles;
    volatile uint32_t break_end_cycles;
    volatile uint32_t previous_break_start_cycles;
    volatile bool timing_ready;
    volatile uint32_t timing_breaks;
    volatile uint32_t break_us;
    volatile uint32_t mab_us;
    volatile uint32_t period_us;
    volatile uint32_t short_breaks;
    volatile uint32_t short_mabs;
};

static uint32_t dmx_receiver_cycles_to_us(const DmxReceiver* receiver, uint32_t cycles) {
    return (cycles + receiver->cycles_per_us / 2U) / receiver->cycles_per_us;
}

static void dmx_receiver_timing_callback(void* context) {
    DmxReceiver* receiver = context;
    const uint32_t now = DWT->CYCCNT;
    const bool level = furi_hal_gpio_read(&gpio_ext_pa7);

    if(!level) {
        if(receiver->waiting_for_mab_end) {
            const uint32_t mab_us =
                dmx_receiver_cycles_to_us(receiver, now - receiver->break_end_cycles);
            receiver->mab_us = mab_us;
            if(mab_us < DMX_MAB_MIN_US) receiver->short_mabs++;
            receiver->waiting_for_mab_end = false;
            receiver->timing_ready = true;
        }

        receiver->low_start_cycles = now;
        receiver->edge_low_seen = true;
        return;
    }

    if(!receiver->edge_low_seen) return;
    receiver->edge_low_seen = false;

    const uint32_t break_us =
        dmx_receiver_cycles_to_us(receiver, now - receiver->low_start_cycles);
    if(break_us < DMX_BREAK_CANDIDATE_US) return;

    receiver->break_us = break_us;
    receiver->timing_breaks++;
    if(break_us < DMX_BREAK_MIN_US) receiver->short_breaks++;

    if(receiver->previous_break_seen) {
        receiver->period_us = dmx_receiver_cycles_to_us(
            receiver, receiver->low_start_cycles - receiver->previous_break_start_cycles);
    }
    receiver->previous_break_start_cycles = receiver->low_start_cycles;
    receiver->previous_break_seen = true;
    receiver->break_end_cycles = now;
    receiver->waiting_for_mab_end = true;
}

static void dmx_receiver_publish_frame(DmxReceiver* receiver) {
    receiver->sequence++;
    receiver->last_start_code = receiver->current_start_code;
    receiver->last_slots = receiver->current_slots;
    const uint8_t old_published_buffer = receiver->published_buffer;
    receiver->published_buffer = receiver->current_buffer;
    receiver->current_buffer = old_published_buffer;
    receiver->frames++;
    receiver->sequence++;
}

static void dmx_receiver_begin_break(DmxReceiver* receiver) {
    if(!receiver->waiting_for_start_code) {
        if(receiver->frame_active) {
            dmx_receiver_publish_frame(receiver);
        }

        receiver->breaks++;
        receiver->waiting_for_start_code = true;
        receiver->frame_active = false;
        receiver->current_slots = 0;
    }
}

static void dmx_receiver_process_byte(DmxReceiver* receiver, uint8_t byte) {
    if(receiver->waiting_for_start_code) {
        receiver->current_start_code = byte;
        receiver->current_slots = 0;
        receiver->waiting_for_start_code = false;
        receiver->frame_active = true;
        return;
    }

    if(!receiver->frame_active) {
        return;
    }

    if(receiver->current_slots < DMX_MAX_SLOTS) {
        receiver->channel_buffers[receiver->current_buffer][receiver->current_slots] = byte;
        receiver->current_slots++;
    }
}

static void dmx_receiver_serial_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    DmxReceiver* receiver = context;

    if(event & FuriHalSerialRxEventNoiseError) {
        receiver->noise_errors++;
    }
    if(event & FuriHalSerialRxEventOverrunError) {
        receiver->overrun_errors++;
    }
    if(event & FuriHalSerialRxEventParityError) {
        receiver->parity_errors++;
    }

    if(event & FuriHalSerialRxEventFrameError) {
        dmx_receiver_begin_break(receiver);

        // A DMX Break is reported as one or more framing-error bytes. Drain
        // them here so the first valid byte after MAB becomes the start code.
        while(furi_hal_serial_async_rx_available(handle)) {
            furi_hal_serial_async_rx(handle);
        }
        return;
    }

    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            dmx_receiver_process_byte(receiver, furi_hal_serial_async_rx(handle));
        }
    }
}

DmxReceiver* dmx_receiver_alloc(void) {
    DmxReceiver* receiver = malloc(sizeof(DmxReceiver));
    memset(receiver, 0, sizeof(DmxReceiver));
    receiver->published_buffer = 1;

    receiver->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(receiver->expansion);
    receiver->expansion_disabled = true;

    receiver->cycles_per_us = furi_hal_cortex_instructions_per_microsecond();
    furi_hal_gpio_init(&gpio_ext_pa7, GpioModeInterruptRiseFall, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_add_int_callback(&gpio_ext_pa7, dmx_receiver_timing_callback, receiver);

    receiver->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!receiver->serial) {
        expansion_enable(receiver->expansion);
        receiver->expansion_disabled = false;
        return receiver;
    }

    furi_hal_serial_init(receiver->serial, DMX_BAUD_RATE);
    furi_hal_serial_configure_framing(
        receiver->serial, FuriHalSerialDataBits8, FuriHalSerialParityNone, FuriHalSerialStopBits2);
    furi_hal_serial_disable_direction(receiver->serial, FuriHalSerialDirectionTx);
    furi_hal_serial_async_rx_start(receiver->serial, dmx_receiver_serial_callback, receiver, true);
    receiver->ready = true;

    return receiver;
}

void dmx_receiver_free(DmxReceiver* receiver) {
    if(!receiver) return;

    furi_hal_gpio_remove_int_callback(&gpio_ext_pa7);
    furi_hal_gpio_init(&gpio_ext_pa7, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

    if(receiver->serial) {
        furi_hal_serial_async_rx_stop(receiver->serial);
        furi_hal_serial_deinit(receiver->serial);
        furi_hal_serial_control_release(receiver->serial);
    }
    if(receiver->expansion_disabled) {
        expansion_enable(receiver->expansion);
    }
    furi_record_close(RECORD_EXPANSION);
    free(receiver);
}

void dmx_receiver_get_snapshot(
    DmxReceiver* receiver,
    DmxRxSnapshot* snapshot,
    uint16_t first_channel) {
    furi_assert(receiver);
    furi_assert(snapshot);

    uint32_t sequence_before;
    uint32_t sequence_after;
    do {
        sequence_before = receiver->sequence;
        if(sequence_before & 1U) continue;

        snapshot->start_code = receiver->last_start_code;
        snapshot->slots = receiver->last_slots;
        const uint8_t published_buffer = receiver->published_buffer;
        for(uint8_t index = 0; index < 4; index++) {
            const uint16_t channel = first_channel + index;
            snapshot->channels[index] = (channel < receiver->last_slots) ?
                                            receiver->channel_buffers[published_buffer][channel] :
                                            0;
        }
        snapshot->frames = receiver->frames;
        sequence_after = receiver->sequence;
    } while((sequence_before != sequence_after) || (sequence_after & 1U));

    snapshot->ready = receiver->ready;
    snapshot->breaks = receiver->breaks;
    snapshot->noise_errors = receiver->noise_errors;
    snapshot->overrun_errors = receiver->overrun_errors;
    snapshot->parity_errors = receiver->parity_errors;
    snapshot->timing_ready = receiver->timing_ready;
    snapshot->timing_breaks = receiver->timing_breaks;
    snapshot->break_us = receiver->break_us;
    snapshot->mab_us = receiver->mab_us;
    snapshot->period_us = receiver->period_us;
    snapshot->short_breaks = receiver->short_breaks;
    snapshot->short_mabs = receiver->short_mabs;
}

bool dmx_receiver_send_frame(DmxReceiver* receiver, const uint8_t* channels, uint16_t count) {
    furi_assert(receiver);
    furi_assert(channels);
    if(!receiver->serial || count > DMX_MAX_SLOTS) return false;

    receiver->tx_frame[0] = 0;
    memcpy(&receiver->tx_frame[1], channels, count);

    if(!receiver->tx_enabled) {
        furi_hal_serial_enable_direction(receiver->serial, FuriHalSerialDirectionTx);
        receiver->tx_enabled = true;
    }

    furi_hal_gpio_write(&gpio_usart_tx, false);
    furi_hal_gpio_init(&gpio_usart_tx, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_cortex_delay_us(100);

    // Returning the pin to USART alternate function ends Break. Keep the UART
    // transmitter enabled between frames so the following delay is the actual
    // MAB instead of including direction-enable overhead.
    furi_hal_gpio_init_ex(
        &gpio_usart_tx,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedVeryHigh,
        GpioAltFn7USART1);
    furi_hal_cortex_delay_us(12);

    furi_hal_serial_tx(receiver->serial, receiver->tx_frame, count + 1U);
    furi_hal_serial_tx_wait_complete(receiver->serial);
    return true;
}

void dmx_receiver_stop_tx(DmxReceiver* receiver) {
    furi_assert(receiver);
    if(receiver->serial) {
        furi_hal_serial_disable_direction(receiver->serial, FuriHalSerialDirectionTx);
        receiver->tx_enabled = false;
    }
}
