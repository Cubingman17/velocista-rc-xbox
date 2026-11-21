# ============================================================
#  VELOCISTA RC - carro controlado con mando Xbox por Bluetooth
#  Firmware para la Pico 2 W del velocista (campeon Pascual Bravo)
#  Basado en el ejemplo de Bluepad32 para Pico W (Public Domain)
# ============================================================

#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "sdkconfig.h"

// Sanity check
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "La Pico 2 W debe usar BLUEPAD32_PLATFORM_CUSTOM (definido en sdkconfig.h del ejemplo)"
#endif

// Definido en my_platform.c
struct uni_platform* get_my_platform(void);

int main(void) {
    stdio_init_all();

    // Inicializa el chip CYW43439 (habilita Bluetooth).
    if (cyw43_arch_init()) {
        loge("no se pudo iniciar cyw43_arch\n");
        return -1;
    }

    // LED encendido mientras arranca...
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // Debe llamarse antes de uni_init()
    uni_platform_set_custom(get_my_platform());

    // Arranca Bluepad32 (escanea y autoconecta controles)
    uni_init(0, NULL);

    // No retorna: el lazo de BTstack vive para siempre.
    btstack_run_loop_execute();
    return 0;
}
