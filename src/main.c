/*
 * main.c — PIR (HC-SR501) -> Servo a 0 deg / 90 deg con PWM por software a 50 Hz
 * Plataforma: ESP32-C3 (bare-metal)
 *
 * Idea:
 *  - PIR en GPIO4 (entrada digital). 1 = movimiento, 0 = sin movimiento.
 *  - SERVO en GPIO2 (salida). Generamos pulsos de 1.0 ms (aprox 0 deg) o 1.5 ms (aprox 90 deg)
 *    cada 20 ms (50 Hz). La senal del servo es de 3.3 V. Alimentar el servo con 5 V y GND comun.
 */

#include <stdint.h>

/* ==========================================================
 * BASES DE PERIFERICOS (direcciones base de bloques)
 * ========================================================== */
#define GPIO_BASE        0x60004000UL
#define IO_MUX_BASE      0x60009000UL
#define TIMG0_BASE       0x6001F000UL
#define TIMG1_BASE       0x60020000UL
#define SYSTEM_BASE      0x600C0000UL
#define RTC_CNTL_BASE    0x60008000UL

/* ==========================================================
 * REGISTROS GPIO (offsets desde GPIO_BASE)
 * ========================================================== */
#define GPIO_OUT_REG          (*(volatile uint32_t*)(GPIO_BASE + 0x0004))
#define GPIO_OUT_W1TS_REG     (*(volatile uint32_t*)(GPIO_BASE + 0x0008))
#define GPIO_OUT_W1TC_REG     (*(volatile uint32_t*)(GPIO_BASE + 0x000C))
#define GPIO_ENABLE_REG       (*(volatile uint32_t*)(GPIO_BASE + 0x0020))
#define GPIO_ENABLE_W1TS_REG  (*(volatile uint32_t*)(GPIO_BASE + 0x0024))
#define GPIO_ENABLE_W1TC_REG  (*(volatile uint32_t*)(GPIO_BASE + 0x0028))
#define GPIO_IN_REG           (*(volatile uint32_t*)(GPIO_BASE + 0x003C))  /* lectura de nivel GPIO */

/* ==========================================================
 * REGISTROS IO_MUX usados (offsets desde IO_MUX_BASE)
 *  GPIO2 -> offset 0x000C
 *  GPIO4 -> offset 0x0014
 * ========================================================== */
#define IO_MUX_GPIO2_REG      (*(volatile uint32_t*)(IO_MUX_BASE + 0x000C))
#define IO_MUX_GPIO4_REG      (*(volatile uint32_t*)(IO_MUX_BASE + 0x0014))

/* Bits IO_MUX (familia ESP32; usar TRM para confirmar) */
#define IO_MUX_FUN_WPD_BIT    7    /* pull-down enable */
#define IO_MUX_FUN_WPU_BIT    8    /* pull-up enable */
#define IO_MUX_FUN_IE_BIT     9    /* input enable */
#define IO_MUX_MCU_SEL_MASK   0xF  /* bits [3:0] seleccion de funcion */
#define IO_MUX_MCU_SEL_GPIO   1    /* 1 = funcion GPIO */

/* ==========================================================
 * TIMER GROUP 0 — TIMER0 (T0)
 * ========================================================== */
#define TIMG_T0CONFIG_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x0000))
#define TIMG_T0LO_REG         (*(volatile uint32_t*)(TIMG0_BASE + 0x0004))
#define TIMG_T0HI_REG         (*(volatile uint32_t*)(TIMG0_BASE + 0x0008))
#define TIMG_T0UPDATE_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x000C))
#define TIMG_T0ALARMLO_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x0010))
#define TIMG_T0ALARMHI_REG    (*(volatile uint32_t*)(TIMG0_BASE + 0x0014))
#define TIMG_T0LOADLO_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x0018))
#define TIMG_T0LOADHI_REG     (*(volatile uint32_t*)(TIMG0_BASE + 0x001C))
#define TIMG_T0LOAD_REG       (*(volatile uint32_t*)(TIMG0_BASE + 0x0020))
#define TIMG_REGCLK_REG       (*(volatile uint32_t*)(TIMG0_BASE + 0x00FC))

/* ==========================================================
 * CLOCK GATING (puede variar segun revision)
 * ========================================================== */
#define SYSTEM_PERIP_CLK_EN0_REG      (*(volatile uint32_t*)(SYSTEM_BASE + 0x0000))
#define SYSTEM_TIMERGROUP_CLK_EN_MASK (1U << 13)

/* ==========================================================
 * WATCHDOGS (macros resumidas)
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

/* ==========================================================
 * GPIO usados (PIR y SERVO)
 * ========================================================== */
#define PIR_GPIO   4
#define PIR_MASK   (1U << PIR_GPIO)

#define SERVO_GPIO 2
#define SERVO_MASK (1U << SERVO_GPIO)

/* ==========================================================
 * Helpers: deshabilitar WDTs (TIMG0/1 y RTC)
 * ========================================================== */
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
    reg &= ~(1U << 31); /* EN = 0 */
    reg &= ~(1U << 14); /* FLASHBOOT_MOD_EN = 0 */
    reg &= ~(1U << 13); /* PROCPU_RESET_EN = 0 */
    reg &= ~(1U << 12); /* APPCPU_RESET_EN = 0 (compat) */
    reg |= (1U << 22);  /* CONF_UPDATE_EN = 1 */
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
    *wdt_feed    = (1U << 31);
    *wdt_config1 = 0;
    *wdt_config2 = 0;
    *wdt_config3 = 0;
    *wdt_config4 = 0;

    uint32_t reg = *wdt_config0;
    reg &= ~(1U << 31);
    reg &= ~(1U << 12);
    reg &= ~(1U << 11);
    reg &= ~(1U << 10);
    *wdt_config0 = reg;
    *wdt_protect = 0;

    *swd_protect = RTC_CNTL_SWD_UNLOCK_KEY;
    *swd_conf   |= (1U << 30);
    *swd_protect = 0;
}

/* ==========================================================
 * GPIO init: SERVO como salida; PIR como entrada (IO_MUX IE)
 * ========================================================== */
static void gpio_init(void)
{
    /* SERVO (GPIO2) salida en bajo inicial */
    GPIO_ENABLE_W1TS_REG = SERVO_MASK;  /* habilita salida en GPIO2 */
    GPIO_OUT_W1TC_REG    = SERVO_MASK;  /* pone nivel bajo */

    /* IO_MUX GPIO2: funcion GPIO, sin pulls, IE habilitado (no afecta salida) */
    uint32_t m2 = IO_MUX_GPIO2_REG;
    m2 &= ~((1U << IO_MUX_FUN_WPD_BIT) | (1U << IO_MUX_FUN_WPU_BIT));
    m2 |=  (1U << IO_MUX_FUN_IE_BIT);
    m2 = (m2 & ~IO_MUX_MCU_SEL_MASK) | IO_MUX_MCU_SEL_GPIO;
    IO_MUX_GPIO2_REG = m2;

    /* PIR (GPIO4) entrada: deshabilitar salida y habilitar IE en IO_MUX */
    GPIO_ENABLE_W1TC_REG = PIR_MASK;    /* asegura que GPIO4 no sea salida */
    uint32_t m4 = IO_MUX_GPIO4_REG;
    m4 &= ~((1U << IO_MUX_FUN_WPD_BIT) | (1U << IO_MUX_FUN_WPU_BIT));
    m4 |=  (1U << IO_MUX_FUN_IE_BIT);   /* input enable */
    m4 = (m4 & ~IO_MUX_MCU_SEL_MASK) | IO_MUX_MCU_SEL_GPIO;
    IO_MUX_GPIO4_REG = m4;
}

/* ==========================================================
 * TIMG0 init: T0 con 1 tick = 1 us (APB_CLK ~ 80 MHz -> divisor 80)
 * ========================================================== */
static void timg0_init_1us_ticks(void)
{
    SYSTEM_PERIP_CLK_EN0_REG |= SYSTEM_TIMERGROUP_CLK_EN_MASK; /* gating TG0 */
    TIMG_REGCLK_REG |= (1U << 30);                             /* is active */

    TIMG_T0CONFIG_REG &= ~(1U << 31);  /* T0_EN = 0 */
    TIMG_T0CONFIG_REG &= ~(1U << 9);   /* T0_USE_XTAL = 0 -> usa APB_CLK */
    TIMG_T0CONFIG_REG |=  (1U << 30);  /* T0_INCREASE = 1 */

    TIMG_T0CONFIG_REG &= ~(0xFFFFU << 13); /* limpia divisor */
    TIMG_T0CONFIG_REG |=  (80U << 13);     /* divisor = 80 -> 1 us por tick si APB=80 MHz */
    TIMG_T0CONFIG_REG |=  (1U << 12);      /* T0_DIVIDER_RST latch */

    TIMG_T0LOADLO_REG = 0;
    TIMG_T0LOADHI_REG = 0;
    TIMG_T0LOAD_REG   = 1;

    TIMG_T0CONFIG_REG |=  (1U << 31);  /* T0_EN = 1 */
}

/* Lectura de tiempo actual en microsegundos del T0 (64 bits) */
static uint64_t t0_now_us(void)
{
    TIMG_T0UPDATE_REG = (1U << 31);
    while (TIMG_T0UPDATE_REG & (1U << 31)) { }
    uint32_t lo = TIMG_T0LO_REG;
    uint32_t hi = TIMG_T0HI_REG;
    return (((uint64_t)hi) << 32) | lo;
}

/* Delay activo en microsegundos usando T0 */
static void delay_us(uint32_t us)
{
    uint64_t start = t0_now_us();
    while ((t0_now_us() - start) < (uint64_t)us) { }
}

/* ==========================================================
 * PWM del SERVO por software:
 *  - width_us: tipico 1000 us (0 deg), 1500 us (90 deg), 2000 us (180 deg)
 *  - Periodo fijo: 20 ms (50 Hz)
 * ========================================================== */
#define SERVO_PERIOD_US   20000U

static void servo_pulse_us(uint32_t width_us)
{
    if (width_us < 500U)  width_us = 500U;   /* limites de seguridad */
    if (width_us > 2500U) width_us = 2500U;

    GPIO_OUT_W1TS_REG = SERVO_MASK;   /* pulso en alto */
    delay_us(width_us);               /* ancho de pulso */
    GPIO_OUT_W1TC_REG = SERVO_MASK;   /* fin del pulso (nivel bajo) */
    {
        uint32_t rest = (SERVO_PERIOD_US > width_us) ? (SERVO_PERIOD_US - width_us) : 0U;
        delay_us(rest);               /* completa 20 ms totales */
    }
}

/* ==========================================================
 * MAIN
 * ========================================================== */
int main(void)
{
    /* 1) Deshabilitar watchdogs */
    disable_timg_wdt(TIMG0_BASE);
    disable_timg_wdt(TIMG1_BASE);
    disable_rtc_wdts();

    /* 2) Inicializar GPIOs */
    gpio_init();

    /* 3) Inicializar TIMG0 para base de 1 us por tick */
    timg0_init_1us_ticks();

    /* 4) Loop: PIR 0 -> servo 0 deg (1000 us); PIR 1 -> servo 90 deg (1500 us) */
    for (;;)
    {
        uint32_t pir_state = (GPIO_IN_REG & PIR_MASK) ? 1U : 0U;
        uint32_t width_us  = pir_state ? 1500U : 1000U;

        /* Generar un frame de 20 ms con el ancho correspondiente */
        servo_pulse_us(width_us);

        /* Si el estado cambia, en el siguiente frame se actualiza el ancho. */
    }

    /* no retorna */
    // return 0;
}

