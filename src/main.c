/*
 * main.c - Blink con timer interno (TIMG0 Timer0) en ESP32-C3
 * ------------------------------------------------------------
 * - Usa el timer general TIMG0.T0 para medir tiempo en lugar de delay por software.
 * - Polling del bit de "timer reached target".
 * - GPIO3 se usa para encender/apagar un LED externo.
 * - No se usa ESP-IDF ni FreeRTOS.
 */

#include <stdint.h>

/* === BASES DE REGISTROS (según TRM del ESP32-C3) === */
#define GPIO_BASE              0x60004000UL
#define GPIO_OUT_W1TS_REG      (*(volatile uint32_t*)(GPIO_BASE + 0x0008))
#define GPIO_OUT_W1TC_REG      (*(volatile uint32_t*)(GPIO_BASE + 0x000C))
#define GPIO_ENABLE_W1TS_REG   (*(volatile uint32_t*)(GPIO_BASE + 0x0024))

#define LED_GPIO 3
#define LED_MASK (1U << LED_GPIO)

/* === TIMER GROUP 0 (TIMG0) === */
#define TIMG0_BASE             0x6001F000UL
#define TIMG_T0CONFIG_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x0000))
#define TIMG_T0LO_REG          (*(volatile uint32_t*)(TIMG0_BASE + 0x0004))
#define TIMG_T0HI_REG          (*(volatile uint32_t*)(TIMG0_BASE + 0x0008))
#define TIMG_T0UPDATE_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x000C))
#define TIMG_T0ALARMLO_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x0010))
#define TIMG_T0ALARMHI_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x0014))
#define TIMG_T0LOADLO_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x0018))
#define TIMG_T0LOADHI_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x001C))
#define TIMG_T0LOAD_REG        (*(volatile uint32_t*)(TIMG0_BASE + 0x0020))
#define TIMG_T0INT_CLR_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x0024))
#define TIMG_T0CONFIG_EN       (1U << 31)
#define TIMG_T0CONFIG_INCREASE (1U << 30)
#define TIMG_T0CONFIG_AUTOREL  (1U << 29)
#define TIMG_T0CONFIG_ALARM_EN (1U << 10)
#define TIMG_T0CONFIG_DIVIDER_M 0xFFFF

/* === WATCHDOGS (mantener tu código anterior) === */
#define TIMG_WDTCONFIG0_OFFSET 0x0048
#define TIMG_WDTFEED_OFFSET    0x0060
#define TIMG_WDTWPROTECT_OFFSET 0x0064
#define TIMG_WDT_UNLOCK_KEY 0x50D83AA1U
#define RTC_CNTL_BASE 0x60008000UL
#define RTC_CNTL_WDTCONFIG0_OFFSET 0x0090
#define RTC_CNTL_WDTWPROTECT_OFFSET 0x00A8
#define RTC_CNTL_SWD_CONF_OFFSET    0x00AC
#define RTC_CNTL_SWD_WPROTECT_OFFSET 0x00B0
#define RTC_CNTL_WDT_UNLOCK_KEY 0x50D83AA1U
#define RTC_CNTL_SWD_UNLOCK_KEY 0x8F1D312AU

static void disable_timg_wdt(uint32_t timer_base) {
    volatile uint32_t *wdt_protect = (volatile uint32_t *)(timer_base + TIMG_WDTWPROTECT_OFFSET);
    volatile uint32_t *wdt_config0 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG0_OFFSET);
    volatile uint32_t *wdt_feed    = (volatile uint32_t *)(timer_base + TIMG_WDTFEED_OFFSET);
    *wdt_protect = TIMG_WDT_UNLOCK_KEY;
    *wdt_feed = 1;
    *wdt_config0 &= ~(1U << 31);
    *wdt_protect = 0;
}

static void disable_rtc_wdts(void) {
    volatile uint32_t *wdt_protect = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTWPROTECT_OFFSET);
    volatile uint32_t *wdt_config0 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG0_OFFSET);
    volatile uint32_t *swd_protect = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_SWD_WPROTECT_OFFSET);
    volatile uint32_t *swd_conf    = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_SWD_CONF_OFFSET);
    *wdt_protect = RTC_CNTL_WDT_UNLOCK_KEY;
    *wdt_config0 &= ~(1U << 31);
    *wdt_protect = 0;
    *swd_protect = RTC_CNTL_SWD_UNLOCK_KEY;
    *swd_conf |= (1U << 30);
    *swd_protect = 0;
}

/* === FUNCIONES TIMER === */
static void timer0_init(uint32_t ticks) {
    // Deshabilitar timer antes de configurar
    TIMG_T0CONFIG_REG &= ~TIMG_T0CONFIG_EN;

    // Prescaler: divide reloj APB (80 MHz) por 80 → 1 MHz (1 tick = 1 µs)
    TIMG_T0CONFIG_REG = (79 & TIMG_T0CONFIG_DIVIDER_M);

    // Modo incremento, autoreload habilitado
    TIMG_T0CONFIG_REG |= TIMG_T0CONFIG_INCREASE | TIMG_T0CONFIG_AUTOREL;

    // Valor inicial = 0
    TIMG_T0LOADLO_REG = 0;
    TIMG_T0LOADHI_REG = 0;
    TIMG_T0LOAD_REG = 1;  // Cargar

    // Configurar alarma en “ticks”
    TIMG_T0ALARMLO_REG = ticks;
    TIMG_T0ALARMHI_REG = 0;
    TIMG_T0CONFIG_REG |= TIMG_T0CONFIG_ALARM_EN;

    // Iniciar el timer
    TIMG_T0CONFIG_REG |= TIMG_T0CONFIG_EN;
}

static void timer0_wait_alarm(void) {
    // Polling: esperar a que el contador alcance el valor de alarma
    while (!(TIMG_T0CONFIG_REG & (1U << 10))) { /* Espera activa */ }
    TIMG_T0INT_CLR_REG = 1; // Limpiar interrupción
}

/* === MAIN === */
int main(void) {
    disable_timg_wdt(TIMG0_BASE);
    disable_timg_wdt(0x60020000UL); // TIMG1
    disable_rtc_wdts();

    GPIO_ENABLE_W1TS_REG = LED_MASK;

    timer0_init(500000); // 500ms @ 1MHz

    while (1) {
        GPIO_OUT_W1TS_REG = LED_MASK; // LED ON
        timer0_wait_alarm();

        GPIO_OUT_W1TC_REG = LED_MASK; // LED OFF
        timer0_wait_alarm();
    }

    return 0;
}
