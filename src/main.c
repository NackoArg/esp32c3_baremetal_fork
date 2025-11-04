/*
 * Baremetal LED toggle en ESP32-C3 usando TIMG0 y GPIO3.
 * Autor: Nacko (correcciones integradas)
 */

#include <stdint.h>

/* ==========================================================
 * BASES DE PERIFÉRICOS
 * ========================================================== */
#define GPIO_BASE            0x60004000UL
#define TIMG0_BASE           0x6001F000UL
#define TIMG1_BASE           0x60020000UL
#define SYSTEM_BASE          0x600C0000UL
#define RTC_CNTL_BASE        0x60008000UL

/* ==========================================================
 * REGISTROS GPIO (offsets desde GPIO_BASE)
 * ========================================================== */
#define GPIO_OUT_REG         (*(volatile uint32_t*)(GPIO_BASE + 0x0004))
#define GPIO_OUT_W1TS_REG    (*(volatile uint32_t*)(GPIO_BASE + 0x0008))
#define GPIO_OUT_W1TC_REG    (*(volatile uint32_t*)(GPIO_BASE + 0x000C))
#define GPIO_ENABLE_REG      (*(volatile uint32_t*)(GPIO_BASE + 0x0020))
#define GPIO_ENABLE_W1TS_REG (*(volatile uint32_t*)(GPIO_BASE + 0x0024))
#define GPIO_ENABLE_W1TC_REG (*(volatile uint32_t*)(GPIO_BASE + 0x0028))

/* Pin del LED y su máscara */
#define LED_GPIO 3
#define LED_MASK (1U << LED_GPIO)

/* Mantiene un segmento .rodata pequeño para el enlace en DROM. */
static const char app_banner[] __attribute__((used)) = "ESP32-C3 baremetal demo";

/* ==========================================================
 * TIMG0: REGISTROS TIMER0 (offsets desde TIMG0_BASE)
 * ========================================================== */
#define TIMG_T0CONFIG_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x0000))
#define TIMG_T0LO_REG        (*(volatile uint32_t*)(TIMG0_BASE + 0x0004))
#define TIMG_T0HI_REG        (*(volatile uint32_t*)(TIMG0_BASE + 0x0008))
#define TIMG_T0UPDATE_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x000C))
#define TIMG_T0ALARMLO_REG   (*(volatile uint32_t*)(TIMG0_BASE + 0x0010))
#define TIMG_T0ALARMHI_REG   (*(volatile uint32_t*)(TIMG0_BASE + 0x0014))
#define TIMG_T0LOADLO_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x0018))
#define TIMG_T0LOADHI_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x001C))
#define TIMG_T0LOAD_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x0020))
#define TIMG_REGCLK_REG      (*(volatile uint32_t*)(TIMG0_BASE + 0x00FC))

/* ==========================================================
 * CLOCK GATING a nivel de sistema
 * ========================================================== */
#define SYSTEM_PERIP_CLK_EN0_REG      (*(volatile uint32_t*)(SYSTEM_BASE + 0x0000))
#define SYSTEM_TIMERGROUP_CLK_EN_MASK (1U << 13)  /* habilita APB clk a TIMG0 */

/* ==========================================================
 * WATCHDOGS (TIMGx MWDT y RTC WDT/SWD)
 * ========================================================== */
#define TIMG_WDTCONFIG0_OFFSET 0x0048
#define TIMG_WDTCONFIG1_OFFSET 0x004C
#define TIMG_WDTCONFIG2_OFFSET 0x0050
#define TIMG_WDTCONFIG3_OFFSET 0x0054
#define TIMG_WDTCONFIG4_OFFSET 0x0058
#define TIMG_WDTCONFIG5_OFFSET 0x005C
#define TIMG_WDTFEED_OFFSET    0x0060
#define TIMG_WDTWPROTECT_OFFSET 0x0064
#define TIMG_WDT_UNLOCK_KEY    0x50D83AA1U
#define TIMG_WDT_STAGE0_MASK   (0x3U << 29)
#define TIMG_WDT_STAGE1_MASK   (0x3U << 27)
#define TIMG_WDT_STAGE2_MASK   (0x3U << 25)
#define TIMG_WDT_STAGE3_MASK   (0x3U << 23)

#define RTC_CNTL_WDTCONFIG0_OFFSET    0x0090
#define RTC_CNTL_WDTCONFIG1_OFFSET    0x0094
#define RTC_CNTL_WDTCONFIG2_OFFSET    0x0098
#define RTC_CNTL_WDTCONFIG3_OFFSET    0x009C
#define RTC_CNTL_WDTCONFIG4_OFFSET    0x00A0
#define RTC_CNTL_WDTFEED_OFFSET       0x00A4
#define RTC_CNTL_WDTWPROTECT_OFFSET   0x00A8
#define RTC_CNTL_SWD_CONF_OFFSET      0x00AC
#define RTC_CNTL_SWD_WPROTECT_OFFSET  0x00B0
#define RTC_CNTL_WDT_UNLOCK_KEY       0x50D83AA1U
#define RTC_CNTL_SWD_UNLOCK_KEY       0x8F1D312AU

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
    reg &= ~(1U << 31); /* TIMG_WDT_EN */
    reg &= ~(1U << 14); /* TIMG_WDT_FLASHBOOT_MOD_EN */
    reg &= ~(1U << 13); /* TIMG_WDT_PROCPU_RESET_EN */
    reg &= ~(1U << 12); /* TIMG_WDT_APPCPU_RESET_EN (compat) */
    reg &= ~TIMG_WDT_STAGE0_MASK;
    reg &= ~TIMG_WDT_STAGE1_MASK;
    reg &= ~TIMG_WDT_STAGE2_MASK;
    reg &= ~TIMG_WDT_STAGE3_MASK;
    reg |= (1U << 22);  /* TIMG_WDT_CONF_UPDATE_EN */
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
    *swd_conf |= (1U << 30);  /* Deshabilitar super WDT */
    *swd_protect = 0;
}

/* ==========================================================
 * GPIO: salida en LED_GPIO
 * ========================================================== */
static void gpio_init(void) {
    GPIO_ENABLE_W1TS_REG = LED_MASK;  /* habilita GPIO como salida */
    GPIO_OUT_W1TC_REG    = LED_MASK;  /* lo deja apagado (nivel bajo) */
}

/* ==========================================================
 * TIMG0:T0 - Configuración
 *  - Fuente: APB_CLK (T0_USE_XTAL = 0)
 *  - Conteo ascendente (T0_INCREASE = 1)
 *  - Prescaler: 0 => divisor 65536 (máximo)
 *  - Carga inicial: 0 (LOAD y luego LOAD_REG)
 *  - Habilitar reloj de periférico y del timer
 * ========================================================== */
static void timer0_init(void) {
    /* 1) Habilitar clock gating del sistema al Timer Group0 (APB -> TIMG0) */
    SYSTEM_PERIP_CLK_EN0_REG |= SYSTEM_TIMERGROUP_CLK_EN_MASK;

    /* 2) Asegurar clock interno del timer activo (bit de TIMER_CLK_IS_ACTIVE).
          En muchas revisiones es el bit 30; si ya está en 1, no pasa nada. */
    TIMG_REGCLK_REG |= (1U << 30);

    /* 3) Detener Timer0 antes de configurar */
    TIMG_T0CONFIG_REG &= ~(1U << 31); /* T0_EN = 0 */

    /* 4) Seleccionar APB (deshabilitar XTAL explícitamente) */
    TIMG_T0CONFIG_REG &= ~(1U << 9);  /* T0_USE_XTAL = 0 => APB_CLK */

    /* 5) Conteo ascendente */
    TIMG_T0CONFIG_REG |= (1U << 30);  /* T0_INCREASE = 1 */

    /* 6) Prescaler: poner 0 en el campo [28:13] => divisor 65536 (muy lento) */
    TIMG_T0CONFIG_REG &= ~(0xFFFFU << 13);  /* limpiar campo divisor */
    TIMG_T0CONFIG_REG |=  (1U << 12);       /* T0_DIVIDER_RST = 1 (aplica nuevo divisor) */

    /* 7) Cargar valor inicial 0 y aplicar carga inmediata */
    TIMG_T0LOADLO_REG = 0;
    TIMG_T0LOADHI_REG = 0;
    TIMG_T0LOAD_REG   = 1;   /* trigger de recarga */

    /* 8) Habilitar Timer0: comienza a contar */
    TIMG_T0CONFIG_REG |= (1U << 31);  /* T0_EN = 1 */
}

/* ==========================================================
 * MAIN: Polling del bit 4 del contador (latch con T0UPDATE)
 * ========================================================== */
int main(void) {
    /* Deshabilitar watchdogs (evita resets inesperados en baremetal) */
    disable_timg_wdt(TIMG0_BASE);
    disable_timg_wdt(TIMG1_BASE);
    disable_rtc_wdts();

    gpio_init();
    timer0_init();

    uint32_t prev_bit4 = 0;

    while (1) {
        /* Latch del contador: escribir 1<<31 y esperar a que HW lo limpie */
        TIMG_T0UPDATE_REG = (1U << 31);
        while (TIMG_T0UPDATE_REG & (1U << 31)) {
            /* espera a que el latch se haga efectivo */
        }

        uint32_t lo   = TIMG_T0LO_REG;   /* 32 bits bajos del contador latcheado */
        uint32_t bit4 = lo & (1U << 11);  /* extraer bit4. Si lo cambio conmuta mas lento. Cuenta hasta 16, o sea que debe ser menor o igual a 16 */

        if (bit4 != prev_bit4) {         /* detecta flanco en bit4 */
            prev_bit4 = bit4;
            if (bit4) {
                GPIO_OUT_W1TS_REG = LED_MASK;  /* LED ON */
            } else {
                GPIO_OUT_W1TC_REG = LED_MASK;  /* LED OFF */
            }
        }
    }

    /* no retorna */
    // return 0;
}
