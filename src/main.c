/*
 * main.c - Ejemplo mínimo bare-metal (blink) para ESP32-C3
 * --------------------------------------------------------
 * OBJETIVO: Mostrar cómo controlar un GPIO escribiendo registros directamente,
 *           sin ESP-IDF ni FreeRTOS, y sin librerías estándar (no printf, no malloc).
 *
 * FLUJO:
 *  - El startup inicializa memoria y llama a main.
 *  - Configuramos el GPIO como salida.
 *  - Entramos en un bucle infinito alternando el nivel lógico del pin.
 *
 * REGISTROS GPIO USADOS (base 0x6000_4000 según TRM ESP32-C3):
 *  - GPIO_OUT_W1TS_REG (offset 0x0008): Escribir bits en 1 pone esos pines en nivel alto.
 *  - GPIO_OUT_W1TC_REG (offset 0x000C): Escribir bits en 1 limpia (pone a 0) esos pines.
 *  - GPIO_ENABLE_W1TS_REG (offset 0x0024): Escribir bits en 1 habilita el modo salida del pin.
 *
 * NOTA: Operamos con registros *write-one-to-set* / *write-one-to-clear* para no alterar otros pines.
 */
#include <stdint.h>
 
/* Dirección base de los registros de GPIO (TRM ESP32-C3). */
#define GPIO_BASE              0x60004000UL
#define GPIO_OUT_REG           (*(volatile uint32_t*)(GPIO_BASE + 0x0004))
#define GPIO_OUT_W1TS_REG      (*(volatile uint32_t*)(GPIO_BASE + 0x0008))
#define GPIO_OUT_W1TC_REG      (*(volatile uint32_t*)(GPIO_BASE + 0x000C))
#define GPIO_ENABLE_W1TS_REG   (*(volatile uint32_t*)(GPIO_BASE + 0x0024))
 
/* Selección del pin del LED */
#define LED_GPIO 3
#define LED_MASK (1U << LED_GPIO)
 
/* Mantiene un segmento .rodata pequeño para el enlace en DROM. */
static const char app_banner[] __attribute__((used)) = "ESP32-C3 baremetal demo";
 
#define TIMG0_BASE 0x6001F000UL
#define TIMG1_BASE 0x60020000UL
#define TIMG_WDTCONFIG0_OFFSET 0x0048
#define TIMG_WDTCONFIG1_OFFSET 0x004C
#define TIMG_WDTCONFIG2_OFFSET 0x0050
#define TIMG_WDTCONFIG3_OFFSET 0x0054
#define TIMG_WDTCONFIG4_OFFSET 0x0058
#define TIMG_WDTCONFIG5_OFFSET 0x005C
#define TIMG_WDTFEED_OFFSET    0x0060
#define TIMG_WDTWPROTECT_OFFSET 0x0064
#define TIMG_WDT_UNLOCK_KEY 0x50D83AA1U
#define TIMG_WDT_STAGE0_MASK (0x3U << 29)
#define TIMG_WDT_STAGE1_MASK (0x3U << 27)
#define TIMG_WDT_STAGE2_MASK (0x3U << 25)
#define TIMG_WDT_STAGE3_MASK (0x3U << 23)
 
#define RTC_CNTL_BASE 0x60008000UL
#define RTC_CNTL_WDTCONFIG0_OFFSET 0x0090
#define RTC_CNTL_WDTCONFIG1_OFFSET 0x0094
#define RTC_CNTL_WDTCONFIG2_OFFSET 0x0098
#define RTC_CNTL_WDTCONFIG3_OFFSET 0x009C
#define RTC_CNTL_WDTCONFIG4_OFFSET 0x00A0
#define RTC_CNTL_WDTFEED_OFFSET    0x00A4
#define RTC_CNTL_WDTWPROTECT_OFFSET 0x00A8
#define RTC_CNTL_SWD_CONF_OFFSET    0x00AC
#define RTC_CNTL_SWD_WPROTECT_OFFSET 0x00B0
#define RTC_CNTL_WDT_UNLOCK_KEY 0x50D83AA1U
#define RTC_CNTL_SWD_UNLOCK_KEY 0x8F1D312AU
 
static void disable_timg_wdt(uint32_t timer_base) {
    volatile uint32_t *wdt_protect = (volatile uint32_t *)(timer_base + TIMG_WDTWPROTECT_OFFSET);
    volatile uint32_t *wdt_config0 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG0_OFFSET);
    volatile uint32_t *wdt_config1 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG1_OFFSET);
    volatile uint32_t *wdt_config2 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG2_OFFSET);
    volatile uint32_t *wdt_config3 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG3_OFFSET);
    volatile uint32_t *wdt_config4 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG4_OFFSET);
    volatile uint32_t *wdt_config5 = (volatile uint32_t *)(timer_base + TIMG_WDTCONFIG5_OFFSET);
    volatile uint32_t *wdt_feed    = (volatile uint32_t *)(timer_base + TIMG_WDTFEED_OFFSET);
 
    *wdt_protect = TIMG_WDT_UNLOCK_KEY;
    *wdt_feed = 1;
    *wdt_config1 = 0;
    *wdt_config2 = 0;
    *wdt_config3 = 0;
    *wdt_config4 = 0;
    *wdt_config5 = 0;
 
    uint32_t reg = *wdt_config0;
    reg &= ~(1U << 31);           /* TIMG_WDT_EN */
    reg &= ~(1U << 14);           /* TIMG_WDT_FLASHBOOT_MOD_EN */
    reg &= ~(1U << 13);           /* TIMG_WDT_PROCPU_RESET_EN */
    reg &= ~(1U << 12);           /* TIMG_WDT_APPCPU_RESET_EN (compat) */
    reg &= ~TIMG_WDT_STAGE0_MASK;
    reg &= ~TIMG_WDT_STAGE1_MASK;
    reg &= ~TIMG_WDT_STAGE2_MASK;
    reg &= ~TIMG_WDT_STAGE3_MASK;
    reg |= (1U << 22);            /* TIMG_WDT_CONF_UPDATE_EN */
    *wdt_config0 = reg;
 
    *wdt_protect = 0;
}
 
static void disable_rtc_wdts(void) {
    volatile uint32_t *wdt_protect = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTWPROTECT_OFFSET);
    volatile uint32_t *wdt_config0 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG0_OFFSET);
    volatile uint32_t *wdt_config1 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG1_OFFSET);
    volatile uint32_t *wdt_config2 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG2_OFFSET);
    volatile uint32_t *wdt_config3 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG3_OFFSET);
    volatile uint32_t *wdt_config4 = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTCONFIG4_OFFSET);
    volatile uint32_t *wdt_feed    = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_WDTFEED_OFFSET);
    volatile uint32_t *swd_protect = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_SWD_WPROTECT_OFFSET);
    volatile uint32_t *swd_conf    = (volatile uint32_t *)(RTC_CNTL_BASE + RTC_CNTL_SWD_CONF_OFFSET);
 
    *wdt_protect = RTC_CNTL_WDT_UNLOCK_KEY;
    *wdt_feed = (1U << 31);
    *wdt_config1 = 0;
    *wdt_config2 = 0;
    *wdt_config3 = 0;
    *wdt_config4 = 0;
 
    uint32_t reg = *wdt_config0;
    reg &= ~(1U << 31); /* RTC_CNTL_WDT_EN */
    reg &= ~(1U << 12); /* RTC_CNTL_WDT_FLASHBOOT_MOD_EN */
    reg &= ~(1U << 11); /* RTC_CNTL_WDT_PROCPU_RESET_EN */
    reg &= ~(1U << 10); /* RTC_CNTL_WDT_APPCPU_RESET_EN */
    reg &= ~(7U << 28);
    reg &= ~(7U << 25);
    reg &= ~(7U << 22);
    reg &= ~(7U << 19);
    *wdt_config0 = reg;
    *wdt_protect = 0;
 
    *swd_protect = RTC_CNTL_SWD_UNLOCK_KEY;
    *swd_conf |= (1U << 30);      /* Deshabilitar super WDT */
    *swd_protect = 0;
}
 
/* delay(): Espera ocupada (busy-wait). Cada iteración ejecuta un NOP.
 * - No es precisa (depende de la frecuencia de CPU y optimizaciones).
 * - Se reemplazará en ejercicios futuros por un timer hardware o SYSTIMER.
 */
static void delay(volatile uint32_t cycles) {
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}
 
int main(void) {
    disable_timg_wdt(TIMG0_BASE);
    disable_timg_wdt(TIMG1_BASE);
    disable_rtc_wdts();
 
    /* Configurar LED_GPIO como salida (pone a 1 el bit correspondiente en el registro ENABLE_W1TS). */
    GPIO_ENABLE_W1TS_REG = LED_MASK;
 
    while (1) {
        /* Poner el LED en alto escribiendo 1 en su bit (GPIO_OUT_W1TS_REG). */
        GPIO_OUT_W1TS_REG = LED_MASK;
        delay(2000000); /* ~visible ON time */
 
    /* Poner el LED en bajo limpiando el bit (GPIO_OUT_W1TC_REG). */
        GPIO_OUT_W1TC_REG = LED_MASK;
        delay(2000000); /* ~visible OFF time */
    }
}
 