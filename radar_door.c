#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#define TRIG &gpio_ext_pc3
#define ECHO &gpio_ext_pb3
#define OPEN_CM 30.0f
#define HOLD_MS 3000

typedef struct { float cm; bool open; } State;

static void draw_cb(Canvas* c, void* ctx) {
    State* s = ctx;
    char buf[32];
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 20, 14, "Radar Door");
    canvas_set_font(c, FontSecondary);
    snprintf(buf, sizeof(buf), "Distance: %.0f cm", (double)s->cm);
    canvas_draw_str(c, 20, 34, buf);
    canvas_draw_str(c, 20, 50, s->open ? "Door: OPEN" : "Door: CLOSED");
}

static void input_cb(InputEvent* e, void* ctx) {
    FuriMessageQueue* q = ctx;
    furi_message_queue_put(q, e, 0);
}

static float measure_cm(void) {
    uint32_t ipus = furi_hal_cortex_instructions_per_microsecond();
    furi_hal_gpio_write(TRIG, false);
    furi_delay_us(2);
    furi_hal_gpio_write(TRIG, true);
    furi_delay_us(10);
    furi_hal_gpio_write(TRIG, false);

    FuriHalCortexTimer t = furi_hal_cortex_timer_get(30000);
    while(!furi_hal_gpio_read(ECHO))
        if(furi_hal_cortex_timer_is_expired(t)) return 999;

    uint32_t start = DWT->CYCCNT;
    t = furi_hal_cortex_timer_get(30000);
    while(furi_hal_gpio_read(ECHO))
        if(furi_hal_cortex_timer_is_expired(t)) return 999;

    float us = (float)(DWT->CYCCNT - start) / ipus;
    return us / 58.0f;
}

static void servo(bool open) {
    furi_hal_pwm_set_params(FuriHalPwmOutputIdTim1PA7, 50, open ? 10 : 5);
}

int32_t radar_door_app(void* p) {
    UNUSED(p);
    State st = {999, false};
    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));

    furi_hal_gpio_init(TRIG, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(ECHO, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
    furi_hal_power_enable_otg();
    furi_hal_pwm_start(FuriHalPwmOutputIdTim1PA7, 50, 5);

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, &st);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    uint32_t last_seen = 0;
    InputEvent ev;
    bool run = true;
    while(run) {
        if(furi_message_queue_get(q, &ev, 80) == FuriStatusOk &&
           ev.type == InputTypeShort && ev.key == InputKeyBack) run = false;

        st.cm = measure_cm();
        uint32_t now = furi_get_tick();
        if(st.cm < OPEN_CM) last_seen = now;
        bool want = (now - last_seen) < HOLD_MS && last_seen != 0;
        if(want != st.open) { st.open = want; servo(want); }
        view_port_update(vp);
    }

    furi_hal_pwm_stop(FuriHalPwmOutputIdTim1PA7);
    furi_hal_power_disable_otg();
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(q);
    return 0;
}
