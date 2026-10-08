#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "dmx_rx.h"
#include "dmx_settings.h"

typedef enum {
    DmxToolScreenTx,
    DmxToolScreenRx,
    DmxToolScreenAnalyze,
    DmxToolScreenSetup,
    DmxToolScreenCount,
} DmxToolScreen;

typedef enum {
    DmxToolSetupStart,
    DmxToolSetupWidth,
    DmxToolSetupSlots,
    DmxToolSetupBlackout,
    DmxToolSetupClear,
    DmxToolSetupCount,
} DmxToolSetup;

typedef struct {
    FuriMessageQueue* input_queue;
    FuriMutex* tx_mutex;
    FuriThread* tx_thread;
    ViewPort* view_port;
    DmxReceiver* receiver;
    DmxToolScreen screen;
    uint32_t fps_sample_tick;
    uint32_t fps_sample_frames;
    uint32_t fps_tenths;
    uint16_t rx_first_channel;
    DmxSettings settings;
    uint8_t tx_selected;
    bool tx_running;
    bool tx_blackout;
    DmxToolSetup setup_selected;
    volatile bool tx_thread_alive;
    volatile uint32_t tx_frames;
} DmxToolApp;

static const char* const dmx_tool_screen_names[DmxToolScreenCount] = {
    "TX",
    "RX",
    "Ana",
    "Setup",
};

static void dmx_tool_draw_tx(Canvas* canvas, const DmxToolApp* app) {
    canvas_set_font(canvas, FontSecondary);
    char value[4];
    uint8_t tx_values[6];
    uint8_t tx_selected;
    uint8_t bank_width;
    uint16_t bank_start;
    bool tx_running;
    bool tx_blackout;

    furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
    bank_width = app->settings.bank_width;
    bank_start = app->settings.bank_start;
    memcpy(tx_values, &app->settings.values[bank_start], bank_width);
    tx_selected = app->tx_selected;
    tx_running = app->tx_running;
    tx_blackout = app->tx_blackout;
    furi_mutex_release(app->tx_mutex);

    const uint8_t spacing = (bank_width == 6U) ? 21U : 30U;
    const uint8_t bar_width = (bank_width == 6U) ? 13U : 17U;
    const uint8_t fill_width = bar_width - 4U;
    const uint8_t label_frame_width = (bank_width == 6U) ? 20U : 24U;
    for(uint8_t index = 0; index < bank_width; index++) {
        const uint8_t x = (bank_width == 6U ? 3U : 6U) + index * spacing;
        const uint8_t fill = (tx_values[index] * 18U + 254U) / 255U;
        canvas_draw_frame(canvas, x, 31, bar_width, 20);
        if(fill > 0) canvas_draw_box(canvas, x + 2, 49U - fill, fill_width, fill);

        snprintf(value, sizeof(value), "%3u", tx_values[index]);
        if(tx_selected == index) canvas_draw_frame(canvas, x - 2, 52, label_frame_width, 11);
        canvas_draw_str(canvas, x, 61, value);
    }

    char status[24];
    snprintf(
        status,
        sizeof(status),
        "%u-%u %s",
        bank_start + 1U,
        bank_start + bank_width,
        tx_blackout ? "BO" : (tx_running ? "ON" : "OFF"));
    canvas_draw_str(canvas, 62, 10, status);
}

static void dmx_tool_draw_rx(Canvas* canvas, const DmxRxSnapshot* rx, uint16_t first_channel) {
    canvas_set_font(canvas, FontSecondary);
    char line[40];

    if(!rx->ready) {
        canvas_draw_str(canvas, 5, 42, "USART busy");
    } else if(rx->frames == 0) {
        canvas_draw_str(canvas, 5, 39, "Waiting on RX pin 14...");
        snprintf(line, sizeof(line), "Breaks:%lu", rx->breaks);
        canvas_draw_str(canvas, 5, 52, line);
    } else {
        snprintf(
            line,
            sizeof(line),
            "SC:%u Slots:%u Ch:%u-%u",
            rx->start_code,
            rx->slots,
            first_channel + 1U,
            first_channel + 4U);
        canvas_draw_str(canvas, 2, 37, line);
        snprintf(
            line,
            sizeof(line),
            "%u:%3u %u:%3u",
            first_channel + 1U,
            rx->channels[0],
            first_channel + 2U,
            rx->channels[1]);
        canvas_draw_str(canvas, 5, 48, line);
        snprintf(
            line,
            sizeof(line),
            "%u:%3u %u:%3u",
            first_channel + 3U,
            rx->channels[2],
            first_channel + 4U,
            rx->channels[3]);
        canvas_draw_str(canvas, 5, 59, line);
    }
}

static void dmx_tool_draw_analyzer(Canvas* canvas, const DmxRxSnapshot* rx, uint32_t fps_tenths) {
    canvas_set_font(canvas, FontSecondary);
    char line[32];
    const uint32_t uart_errors = rx->noise_errors + rx->overrun_errors + rx->parity_errors;
    const uint32_t timing_errors = rx->short_breaks + rx->short_mabs;

    if(rx->timing_ready) {
        snprintf(line, sizeof(line), "B:%luus MAB:%luus", rx->break_us, rx->mab_us);
        canvas_draw_str(canvas, 2, 34, line);
        snprintf(
            line,
            sizeof(line),
            "Per:%lu.%lums %lu.%lufps",
            rx->period_us / 1000U,
            (rx->period_us % 1000U) / 100U,
            fps_tenths / 10U,
            fps_tenths % 10U);
        canvas_draw_str(canvas, 2, 43, line);
    } else {
        canvas_draw_str(canvas, 2, 34, "Timing: connect pin 2");
        snprintf(line, sizeof(line), "%lu.%lu fps", fps_tenths / 10U, fps_tenths % 10U);
        canvas_draw_str(canvas, 2, 43, line);
    }

    snprintf(line, sizeof(line), "Frm:%lu Brk:%lu", rx->frames, rx->timing_breaks);
    canvas_draw_str(canvas, 2, 52, line);
    snprintf(
        line,
        sizeof(line),
        "Err:%lu SB:%lu SM:%lu U:%lu",
        timing_errors + uart_errors,
        rx->short_breaks,
        rx->short_mabs,
        uart_errors);
    canvas_draw_str(canvas, 2, 61, line);
}

static void dmx_tool_draw_setup(Canvas* canvas, const DmxToolApp* app) {
    uint16_t bank_start;
    uint16_t frame_slots;
    uint8_t bank_width;
    bool blackout;
    DmxToolSetup selected;

    furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
    bank_start = app->settings.bank_start;
    frame_slots = app->settings.frame_slots;
    bank_width = app->settings.bank_width;
    blackout = app->tx_blackout;
    selected = app->setup_selected;
    furi_mutex_release(app->tx_mutex);

    canvas_set_font(canvas, FontSecondary);
    char line[32];
    snprintf(line, sizeof(line), "Setting %u/%u", selected + 1U, DmxToolSetupCount);
    canvas_draw_str(canvas, 3, 36, line);

    if(selected == DmxToolSetupStart) {
        snprintf(line, sizeof(line), "Start address: %u", bank_start + 1U);
    } else if(selected == DmxToolSetupWidth) {
        snprintf(line, sizeof(line), "Faders per bank: %u", bank_width);
    } else if(selected == DmxToolSetupSlots) {
        snprintf(line, sizeof(line), "Frame slots: %u", frame_slots);
    } else if(selected == DmxToolSetupBlackout) {
        snprintf(line, sizeof(line), "Blackout: %s", blackout ? "ON" : "OFF");
    } else {
        snprintf(line, sizeof(line), "Clear all 512 values");
    }
    canvas_draw_str(canvas, 3, 49, line);
    canvas_draw_str(canvas, 3, 61, "Up/Down edit  OK next");
}

static void dmx_tool_update_fps(DmxToolApp* app) {
    const uint32_t now = furi_get_tick();
    const uint32_t elapsed = now - app->fps_sample_tick;
    const uint32_t sample_period = furi_ms_to_ticks(1000);
    if(elapsed < sample_period) return;

    DmxRxSnapshot rx;
    dmx_receiver_get_snapshot(app->receiver, &rx, 0);
    const uint32_t frame_delta = rx.frames - app->fps_sample_frames;
    app->fps_tenths =
        (frame_delta * furi_kernel_get_tick_frequency() * 10U + elapsed / 2U) / elapsed;
    app->fps_sample_frames = rx.frames;
    app->fps_sample_tick = now;
}

static void dmx_tool_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    DmxToolApp* app = context;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "DMX Tool");
    canvas_set_font(canvas, FontSecondary);
    if(app->screen != DmxToolScreenTx) canvas_draw_str(canvas, 91, 10, "v0.5.0");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    for(uint8_t index = 0; index < DmxToolScreenCount; index++) {
        const uint8_t x = 2 + index * 32;
        if(app->screen == index) {
            canvas_draw_frame(canvas, x - 1, 16, 31, 12);
        }
        canvas_draw_str(canvas, x, 25, dmx_tool_screen_names[index]);
    }

    DmxRxSnapshot rx;
    dmx_receiver_get_snapshot(app->receiver, &rx, app->rx_first_channel);

    if(app->screen == DmxToolScreenTx) {
        dmx_tool_draw_tx(canvas, app);
    } else if(app->screen == DmxToolScreenRx) {
        dmx_tool_draw_rx(canvas, &rx, app->rx_first_channel);
    } else if(app->screen == DmxToolScreenAnalyze) {
        dmx_tool_draw_analyzer(canvas, &rx, app->fps_tenths);
    } else {
        dmx_tool_draw_setup(canvas, app);
    }
}

static int32_t dmx_tool_tx_thread(void* context) {
    DmxToolApp* app = context;
    uint32_t next_frame_tick = furi_get_tick();
    bool was_running = false;

    while(app->tx_thread_alive) {
        uint8_t tx_values[DMX_CHANNEL_COUNT];
        uint16_t frame_slots;
        bool tx_running;
        bool tx_blackout;
        furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
        frame_slots = app->settings.frame_slots;
        memcpy(tx_values, app->settings.values, frame_slots);
        tx_running = app->tx_running;
        tx_blackout = app->tx_blackout;
        furi_mutex_release(app->tx_mutex);

        if(!tx_running) {
            if(was_running) dmx_receiver_stop_tx(app->receiver);
            was_running = false;
            next_frame_tick = furi_get_tick();
            furi_delay_ms(5);
            continue;
        }

        if(tx_blackout) memset(tx_values, 0, frame_slots);
        if(!was_running) next_frame_tick = furi_get_tick();
        if(dmx_receiver_send_frame(app->receiver, tx_values, frame_slots)) app->tx_frames++;
        was_running = true;

        next_frame_tick += furi_ms_to_ticks(25);
        const uint32_t now = furi_get_tick();
        if((int32_t)(next_frame_tick - now) <= 0) {
            next_frame_tick = now + furi_ms_to_ticks(25);
        }
        furi_delay_until_tick(next_frame_tick);
    }

    dmx_receiver_stop_tx(app->receiver);
    return 0;
}

static void dmx_tool_input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    FuriMessageQueue* input_queue = context;
    furi_message_queue_put(input_queue, input_event, 0);
}

int32_t dmx_tool_app(void* p) {
    UNUSED(p);

    DmxToolApp* app = malloc(sizeof(DmxToolApp));
    memset(app, 0, sizeof(DmxToolApp));
    dmx_settings_load(&app->settings);
    app->screen = DmxToolScreenTx;
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    app->receiver = dmx_receiver_alloc();
    app->fps_sample_tick = furi_get_tick();
    app->tx_thread_alive = true;
    app->tx_thread = furi_thread_alloc_ex("DmxTx", 2048, dmx_tool_tx_thread, app);
    furi_thread_start(app->tx_thread);

    view_port_draw_callback_set(app->view_port, dmx_tool_draw_callback, app);
    view_port_input_callback_set(app->view_port, dmx_tool_input_callback, app->input_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        InputEvent event;
        const FuriStatus input_status =
            furi_message_queue_get(app->input_queue, &event, furi_ms_to_ticks(100));
        dmx_tool_update_fps(app);
        if(input_status != FuriStatusOk) {
            view_port_update(app->view_port);
            continue;
        }

        if(event.type == InputTypeLong && app->screen == DmxToolScreenTx) {
            furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
            if(event.key == InputKeyOk) {
                app->tx_running = !app->tx_running;
            } else if(event.key == InputKeyLeft || event.key == InputKeyRight) {
                const uint16_t width = app->settings.bank_width;
                const uint16_t maximum_start = DMX_CHANNEL_COUNT - width;
                if(event.key == InputKeyLeft) {
                    app->settings.bank_start = (app->settings.bank_start < width) ?
                                                   0U :
                                                   (uint16_t)(app->settings.bank_start - width);
                } else {
                    const uint16_t next = app->settings.bank_start + width;
                    app->settings.bank_start = (next > maximum_start) ? maximum_start : next;
                }
                const uint16_t bank_end = app->settings.bank_start + width;
                if(app->settings.frame_slots < bank_end) app->settings.frame_slots = bank_end;
                app->tx_selected = 0;
            }
            furi_mutex_release(app->tx_mutex);
            view_port_update(app->view_port);
            continue;
        }

        if(event.type != InputTypeShort && event.type != InputTypeRepeat) {
            continue;
        }

        if(event.key == InputKeyBack) {
            running = false;
        } else if(event.key == InputKeyLeft && event.type == InputTypeShort) {
            app->screen = (app->screen == DmxToolScreenTx) ? DmxToolScreenCount - 1 :
                                                             app->screen - 1;
            view_port_update(app->view_port);
        } else if(event.key == InputKeyRight && event.type == InputTypeShort) {
            app->screen = (app->screen + 1) % DmxToolScreenCount;
            view_port_update(app->view_port);
        } else if(
            event.key == InputKeyOk && event.type == InputTypeShort &&
            app->screen == DmxToolScreenTx) {
            furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
            app->tx_selected = (app->tx_selected + 1U) % app->settings.bank_width;
            furi_mutex_release(app->tx_mutex);
            view_port_update(app->view_port);
        } else if(event.key == InputKeyUp && app->screen == DmxToolScreenTx) {
            furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
            const uint16_t step = (event.type == InputTypeRepeat) ? 5U : 1U;
            const uint16_t channel = app->settings.bank_start + app->tx_selected;
            const uint16_t value = app->settings.values[channel] + step;
            app->settings.values[channel] = (value > 255U) ? 255U : value;
            furi_mutex_release(app->tx_mutex);
            view_port_update(app->view_port);
        } else if(event.key == InputKeyDown && app->screen == DmxToolScreenTx) {
            furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
            const uint8_t step = (event.type == InputTypeRepeat) ? 5U : 1U;
            const uint16_t channel = app->settings.bank_start + app->tx_selected;
            const uint8_t value = app->settings.values[channel];
            app->settings.values[channel] = (value < step) ? 0U : (uint8_t)(value - step);
            furi_mutex_release(app->tx_mutex);
            view_port_update(app->view_port);
        } else if(event.key == InputKeyUp && app->screen == DmxToolScreenRx) {
            if(app->rx_first_channel < 508U) app->rx_first_channel += 4U;
            view_port_update(app->view_port);
        } else if(event.key == InputKeyDown && app->screen == DmxToolScreenRx) {
            app->rx_first_channel = (app->rx_first_channel < 4U) ? 0U : app->rx_first_channel - 4U;
            view_port_update(app->view_port);
        } else if(
            event.key == InputKeyOk && event.type == InputTypeShort &&
            app->screen == DmxToolScreenSetup) {
            app->setup_selected = (app->setup_selected + 1U) % DmxToolSetupCount;
            view_port_update(app->view_port);
        } else if(
            (event.key == InputKeyUp || event.key == InputKeyDown) &&
            app->screen == DmxToolScreenSetup) {
            furi_mutex_acquire(app->tx_mutex, FuriWaitForever);
            const bool increase = event.key == InputKeyUp;
            const uint16_t step = (event.type == InputTypeRepeat) ? 10U : 1U;
            if(app->setup_selected == DmxToolSetupStart) {
                const uint16_t maximum_start = DMX_CHANNEL_COUNT - app->settings.bank_width;
                if(increase) {
                    const uint16_t next = app->settings.bank_start + step;
                    app->settings.bank_start = (next > maximum_start) ? maximum_start : next;
                } else {
                    app->settings.bank_start = (app->settings.bank_start < step) ?
                                                   0U :
                                                   (uint16_t)(app->settings.bank_start - step);
                }
                const uint16_t bank_end = app->settings.bank_start + app->settings.bank_width;
                if(app->settings.frame_slots < bank_end) app->settings.frame_slots = bank_end;
                app->tx_selected = 0;
            } else if(app->setup_selected == DmxToolSetupWidth) {
                if(event.type == InputTypeShort) {
                    app->settings.bank_width = (app->settings.bank_width == 4U) ? 6U : 4U;
                    const uint16_t maximum_start = DMX_CHANNEL_COUNT - app->settings.bank_width;
                    if(app->settings.bank_start > maximum_start) {
                        app->settings.bank_start = maximum_start;
                    }
                    const uint16_t bank_end = app->settings.bank_start + app->settings.bank_width;
                    if(app->settings.frame_slots < bank_end) {
                        app->settings.frame_slots = bank_end;
                    }
                    app->tx_selected = 0;
                }
            } else if(app->setup_selected == DmxToolSetupSlots) {
                const uint16_t minimum_slots = app->settings.bank_start + app->settings.bank_width;
                if(increase) {
                    const uint16_t next = app->settings.frame_slots + step;
                    app->settings.frame_slots = (next > DMX_CHANNEL_COUNT) ? DMX_CHANNEL_COUNT :
                                                                             next;
                } else {
                    app->settings.frame_slots =
                        (app->settings.frame_slots < minimum_slots + step) ?
                            minimum_slots :
                            app->settings.frame_slots - step;
                }
            } else if(app->setup_selected == DmxToolSetupBlackout && event.type == InputTypeShort) {
                app->tx_blackout = !app->tx_blackout;
            } else if(app->setup_selected == DmxToolSetupClear && event.type == InputTypeShort) {
                memset(app->settings.values, 0, sizeof(app->settings.values));
            }
            furi_mutex_release(app->tx_mutex);
            view_port_update(app->view_port);
        }
    }

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    app->tx_thread_alive = false;
    furi_thread_join(app->tx_thread);
    furi_thread_free(app->tx_thread);
    dmx_settings_save(&app->settings);
    furi_mutex_free(app->tx_mutex);
    dmx_receiver_free(app->receiver);
    free(app);

    return 0;
}
