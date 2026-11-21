// ============================================================
//  VELOCISTA RC - mando Xbox directo por Bluetooth (Bluepad32)
// ------------------------------------------------------------
//  Firmware para el velocista del Pascual Bravo:
//    Pico 2 W + puente H DRV8833, MISMAS patas del firmware PID:
//      MOTOR_IZQ = GP14 (avanza) / GP15 (retrocede)
//      MOTOR_DER = GP13 (avanza) / GP12 (retrocede)  [volteado]
//
//  Basado en el ejemplo "my_platform.c" de Bluepad32 (Public Domain)
// ============================================================

#include <stddef.h>
#include <string.h>

#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/time.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/clocks.h>
#include <uni.h>

#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "La Pico 2 W debe usar BLUEPAD32_PLATFORM_CUSTOM"
#endif

// ----------------- CONFIGURAME -----------------
#define PIN_IZQ_A 14   // motor izquierdo, avanza
#define PIN_IZQ_B 15   // motor izquierdo, retrocede
#define PIN_DER_A 13   // motor derecho, avanza
#define PIN_DER_B 12   // motor derecho, retrocede (volteado, como en MicroPython)
#define INVERTIR_IZQ 0  // el izquierdo gira al reves? -> 1
#define INVERTIR_DER 0  // el derecho gira al reves? -> 1
#define FREQ_PWM_HZ 1000  // frecuencia del puente H
#define LIMITE_VEL 70      // % tope de velocidad (A/TURBO siempre da 100)
#define DEADBAND_MOTOR 4   // % minimo para que el motor reaccione
#define STICK_DZ 62        // zona muerta del stick (-512..511), ~12%
#define DPAD_VEL 55        // % al manejar con la cruceta
#define VIGIA_MS 600       // ms sin datos del mando -> freno automatico
// -----------------------------------------------

// Estado del mando
static uint32_t s_last_data_ms = 0;
static bool s_control_activo = false;
static bool s_moviendo = false;
static int s_cur_x = 0;  // giro   -100..100 (suavizado)
static int s_cur_y = 0;  // gaz    -100..100
static btstack_timer_source_t s_vigia;

// Declaraciones adelantadas
static int suavizar(int actual, int objetivo);
static void trigger_event_on_gamepad(uni_hid_device_t* d);

// ---------------- motores (DRV8833, 2 pines por motor) ----------------
// Convencion DRV8833:  A = PWM y B = 0   ->  avanza
//                     A = 0   y B = PWM ->  retrocede

static void pwm_setup(void) {
    const uint pines[4] = {PIN_IZQ_A, PIN_IZQ_B, PIN_DER_A, PIN_DER_B};
    for (int i = 0; i < 4; i++)
        gpio_set_function(pines[i], GPIO_FUNC_PWM);

    // Cada "slice" drive 2 pines: 14/15 (izq) y 13/12 (der)
    uint slices[2] = {pwm_gpio_to_slice_num(PIN_IZQ_A), pwm_gpio_to_slice_num(PIN_DER_A)};
    uint64_t clk = clock_get_hz(clk_sys);
    float div = (float)clk / (FREQ_PWM_HZ * 65536.0f);
    if (div < 1.0f)
        div = 1.0f;
    for (int s = 0; s < 2; s++) {
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, div);
        pwm_config_set_wrap(&cfg, 65535);
        pwm_init(slices[s], &cfg, true);  // enabled
    }
}

static void motor_set(uint pa, uint pb, int vel, int invertir) {
    // vel: -100..100
    if (invertir)
        vel = -vel;
    if (vel > 100)
        vel = 100;
    if (vel < -100)
        vel = -100;
    if (vel > -DEADBAND_MOTOR && vel < DEADBAND_MOTOR) {
        pwm_set_gpio_level(pa, 0);
        pwm_set_gpio_level(pb, 0);
        return;
    }
    uint16_t duty;
    if (vel < 0) {
        duty = (uint16_t)((-vel) * 65535 / 100);
        pwm_set_gpio_level(pa, 0);
        pwm_set_gpio_level(pb, duty);
    } else {
        duty = (uint16_t)(vel * 65535 / 100);
        pwm_set_gpio_level(pa, duty);
        pwm_set_gpio_level(pb, 0);
    }
}

static void motores(int x, int y) {
    // x: giro -100..100 (+ = derecha) | y: gaz -100..100 (+ = adelante)
    // Con x=+-100 e y=0 el carro PIVOTA en el sitio (sobre su eje).
    int izq = y + x;
    int der = y - x;
    motor_set(PIN_IZQ_A, PIN_IZQ_B, izq, INVERTIR_IZQ);
    motor_set(PIN_DER_A, PIN_DER_B, der, INVERTIR_DER);
}

static void parar(void) {
    s_cur_x = 0;
    s_cur_y = 0;
    motores(0, 0);
    s_moviendo = false;
}

// ---------------- vitamina: suavizado ----------------
// Se acerca por mitades, y si ya esta cerca (dif < 3) engancha directo,
// asi nunca se queda trabado en 1 por el truncado de enteros.
static int suavizar(int actual, int objetivo) {
    int dif = objetivo - actual;
    if (dif > -3 && dif < 3)
        return objetivo;
    return actual + dif / 2;
}

// ---------------- vigia: freno automatico + LED ----------------
static void vigia_handler(btstack_timer_source_t* ts) {
    // re-programar el timer (btstack timers son de un solo uso)
    btstack_run_loop_set_timer(ts, 100);
    btstack_run_loop_add_timer(ts);

    uint32_t ahora = to_ms_since_boot(get_absolute_time());
    if (s_moviendo && (ahora - s_last_data_ms) > VIGIA_MS) {
        // se cayo el Bluetooth / el mando se apago -> frenar
        parar();
        logi("vigia: sin datos del mando -> freno\n");
    }

    // LED: parpadeo lento = buscando mando ; fijo = conectado ; rapido = manejando
    static int tick = 0;
    tick++;
    if (s_moviendo)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, tick & 1);
    else if (s_control_activo)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    else
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, (tick % 10) < 5);
}

//
// Platform Overrides
//
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("velocista rc: init\n");
    pwm_setup();
    parar();  // arrancar SIEMPRE con motores muertos (PWM huerfano)
}

static void my_platform_on_init_complete(void) {
    logi("velocista rc: listo. Enciende el mando Xbox (boton Xbox o emparejar)...\n");

    // Escanear y autoconectar mandos soportados.
    uni_bt_start_scanning_and_autoconnect_unsafe();

    // No borramos las claves: asi el mando se reconecta solo
    // en el siguiente encendido. (Si un dia se enreda:
    // descomentar la linea de abajo y recompilar.)
    // uni_bt_del_keys_unsafe();

    // Arrancar el vigia (freno automatico + LED)
    btstack_run_loop_set_timer_handler(&s_vigia, vigia_handler);
    btstack_run_loop_set_timer(&s_vigia, 100);
    btstack_run_loop_add_timer(&s_vigia);
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    ARG_UNUSED(addr);
    ARG_UNUSED(name);
    ARG_UNUSED(cod);
    ARG_UNUSED(rssi);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("velocista rc: mando conectado %p\n", d);
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("velocista rc: mando desconectado -> freno\n");
    ARG_UNUSED(d);
    parar();
    s_control_activo = false;
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("velocista rc: MANDO LISTO %p\n", d);
    s_control_activo = true;

    // LED de jugador en el mando (estetica Xbox)
    if (d->report_parser.set_player_leds != NULL)
        d->report_parser.set_player_leds(d, GAMEPAD_SEAT_A);

    // Bienvenida: pequeño rumble para confirmar
    if (d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0 /* delay ms */, 200 /* duracion ms */, 120 /* debil */, 0 /* fuerte */);

    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    static int turbo_prev = 0;

    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;
    uni_gamepad_t* gp = &ctl->gamepad;

    s_last_data_ms = to_ms_since_boot(get_absolute_time());

    // B = freno inmediato
    if (gp->buttons & BUTTON_B) {
        parar();
        return;
    }

    // Stick izquierdo (-512..511) con zona muerta
    int sx = gp->axis_x;
    int sy = gp->axis_y;
    if (sx > -STICK_DZ && sx < STICK_DZ)
        sx = 0;
    if (sy > -STICK_DZ && sy < STICK_DZ)
        sy = 0;
    int tx = sx * 100 / 512;   // -100..99
    int ty = -sy * 100 / 512;  // stick arriba = adelante

    // Gatillos estilo juego de carros: RT acelera, LT reversa (0..1023)
    int rt = gp->throttle;
    int lt = gp->brake;
    if (rt > 20 || lt > 20)
        ty = (rt - lt) * 100 / 1023;

    // Cruceta: manejo a velocidad fija
    if (tx == 0 || ty == 0) {
        if (gp->dpad & DPAD_UP)
            ty = DPAD_VEL;
        if (gp->dpad & DPAD_DOWN)
            ty = -DPAD_VEL;
        if (gp->dpad & DPAD_LEFT)
            tx = -DPAD_VEL;
        if (gp->dpad & DPAD_RIGHT)
            tx = DPAD_VEL;
    }

    // A = TURBO (100% siempre + el mando VIBRA al activarlo)
    int turbo = (gp->buttons & BUTTON_A) ? 1 : 0;
    if (turbo && !turbo_prev && d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 300, 160, 255);
    turbo_prev = turbo;

    int limite = turbo ? 100 : LIMITE_VEL;
    tx = tx * limite / 100;
    ty = ty * limite / 100;

    // Suavizado: evita el tiron brutal de 0 a 100 (protege los N20)
    s_cur_x = suavizar(s_cur_x, tx);
    s_cur_y = suavizar(s_cur_y, ty);

    if (tx == 0 && ty == 0 && s_cur_x == 0 && s_cur_y == 0) {
        parar();
        return;
    }
    motores(s_cur_x, s_cur_y);
    s_moviendo = true;
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON:
            // Boton Xbox presionado: saludar
            trigger_event_on_gamepad((uni_hid_device_t*)data);
            break;
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            logi("velocista rc: bluetooth %d\n", (bool)(data));
            break;
        default:
            break;
    }
}

//
// Helpers
//
static void trigger_event_on_gamepad(uni_hid_device_t* d) {
    if (d->report_parser.play_dual_rumble != NULL) {
        d->report_parser.play_dual_rumble(d, 0, 50, 128, 40);
    }
    if (d->report_parser.set_player_leds != NULL) {
        static uint8_t led = 0;
        led += 1;
        led &= 0xf;
        d->report_parser.set_player_leds(d, led);
    }
}

//
// Entry Point
//
struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "Velocista RC",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_discovered = my_platform_on_device_discovered,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };

    return &plat;
}
