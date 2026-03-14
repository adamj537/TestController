#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_console.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cmd_vdac.h"
#include "cmd_swd.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_log.h"

/* SWD pin assignments — routed to DUT CN6 via TIE J15 */
#define SWCLK_GPIO  37
#define SWDIO_GPIO  38

/* HEF4051 mux (U8) GPIO assignments — power-button latch control */
#define MUX_A0_GPIO  17   /* address select A0 */
#define MUX_A1_GPIO  18   /* address select A1 */
#define MUX_A2_GPIO  21   /* address select A2 */
#define MUX_EN_GPIO  35   /* /EN — active low */
#define MUX_SIG_GPIO 36   /* SIG — active low for PB-A */

/* Clock half-period µs — runtime-tunable via `swd speed <half_us>`.
 * Tested range: 0–10.  half_us=0 (GPIO-limited, ~2–4 MHz actual) verified
 * reliable across 5 consecutive erase+program+readback cycles. */
static int s_swd_half_us = 0;    /* default: max speed (GPIO-limited) */

/* ── HEF4051 mux helpers — PB-A latch control ────────────────────────────── */

/* Assert PB-A via U8 mux throughout the SWD flash operation.
 * Ch0 = PB-A: A0=A1=A2=0 (channel select), /EN=0 (mux enabled), SIG=0 (active low).
 * Must be held for the entire flash sequence — a blank DUT has no firmware to drive
 * KEEPALIVE (PD14), so the power latch drops the moment PB-A releases. */
static void mux_pba_assert(void)
{
    const int gpios[] = {MUX_A0_GPIO, MUX_A1_GPIO, MUX_A2_GPIO, MUX_EN_GPIO, MUX_SIG_GPIO};
    for (int i = 0; i < 5; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << gpios[i]),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        gpio_set_level((gpio_num_t)gpios[i], 0);  /* A0=A1=A2=0, /EN=0, SIG=0 */
    }
}

/* Release PB-A: de-assert SIG then disable mux.
 * After this the DUT must assert KEEPALIVE itself; new DUT firmware handles this. */
static void mux_pba_release(void)
{
    gpio_set_level((gpio_num_t)MUX_SIG_GPIO, 1);  /* release PB-A */
    gpio_set_level((gpio_num_t)MUX_EN_GPIO,  1);  /* disable mux */
}

/* ── GPIO helpers ─────────────────────────────────────────────────────────── */

static void swclk_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << SWCLK_GPIO),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(SWCLK_GPIO, 0);
}

static void swdio_output(void) { gpio_set_direction((gpio_num_t)SWDIO_GPIO, GPIO_MODE_OUTPUT); }
static void swdio_input(void)  { gpio_set_direction((gpio_num_t)SWDIO_GPIO, GPIO_MODE_INPUT);  }

static void swdio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << SWDIO_GPIO),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(SWDIO_GPIO, 1);
}

/* ── Bit-bang primitives ──────────────────────────────────────────────────── */

static void swd_write_bit(int bit)
{
    gpio_set_level(SWDIO_GPIO, bit ? 1 : 0);
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 1);
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 0);
}

static int swd_read_bit(void)
{
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 1);
    ets_delay_us(s_swd_half_us);
    int bit = gpio_get_level(SWDIO_GPIO);
    gpio_set_level(SWCLK_GPIO, 0);
    return bit;
}

/* Host-releases-SWDIO turnaround (before target drives ACK).
 * Returns ACK bit 0: the STM32L476 begins driving ACK on this same clock,
 * so we capture it here rather than discarding it. */
static int swd_trn_to_target(void)
{
    swdio_input();
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 1);
    ets_delay_us(s_swd_half_us);
    int ack0 = gpio_get_level(SWDIO_GPIO);
    gpio_set_level(SWCLK_GPIO, 0);
    return ack0;
}

/* Target-releases-SWDIO turnaround (after target drives data, before host writes) */
static void swd_trn_to_host(void)
{
    /* SWDIO still input; clock once, then take back */
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 1);
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 0);
    swdio_output();
}

/* ── SWD protocol sequences ───────────────────────────────────────────────── */

static void swd_idle(int cycles)
{
    swdio_output();
    gpio_set_level(SWDIO_GPIO, 0);
    for (int i = 0; i < cycles; i++) {
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 1);
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 0);
    }
}

/* ≥50 clocks SWDIO=1 — resets SWD/JTAG state machine */
static void swd_line_reset(void)
{
    swdio_output();
    gpio_set_level(SWDIO_GPIO, 1);
    for (int i = 0; i < 56; i++) {
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 1);
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 0);
    }
}

/* JTAG-to-SWD: 0xE79E LSB-first = same wire sequence as 0x79E7 MSB-first
 * per ARM IHI0031F §4.5.3  */
static void swd_jtag_to_swd(void)
{
    swdio_output();
    uint16_t seq = 0xE79E;
    for (int i = 0; i < 16; i++) {
        swd_write_bit(seq & 1);
        seq >>= 1;
    }
}

static void swd_write_request(uint8_t req)
{
    swdio_output();
    for (int i = 0; i < 8; i++) {
        swd_write_bit(req & 1);
        req >>= 1;
    }
}

/* SWD read transaction — returns data, sets *ack and *parity_ok */
static uint32_t swd_dp_read(uint8_t req, int *ack, bool *parity_ok)
{
    swd_write_request(req);

    /* Turnaround clock doubles as ACK bit 0 — STM32L476 starts driving on it */
    *ack = swd_trn_to_target();
    for (int i = 1; i < 3; i++) *ack |= (swd_read_bit() << i);

    uint32_t data = 0;
    if (*ack == 1) {
        /* OK: read 32 data + 1 parity driven by target */
        int par = 0;
        for (int i = 0; i < 32; i++) {
            int b = swd_read_bit();
            data |= ((uint32_t)b << i);
            par  ^= b;
        }
        int par_bit = swd_read_bit();
        *parity_ok = (par_bit == par);
    } else {
        /* FAULT/WAIT: ARM IHI0031 B4.3.4 — no data phase on non-OK read.
         * The target does NOT drive data bits.  Clocking 32+1 extra bits here
         * would desynchronise the bus (target counts them as next transaction). */
        *parity_ok = false;
    }

    swd_trn_to_host();
    swd_idle(8);
    return data;
}

/* SWD write transaction — returns ack.
 *
 * Framing: 38 clocks post-request.  Experimentally verified that 37 clocks
 * (TRN merged with ACK[0]) shifts write data by 1 bit on the STM32L476.
 * 38 clocks (TRN as dead cycle + 3 ACK) aligns data correctly.
 *
 *   TRN(1, discarded) + ACK(3) + TRN→host(1) + WDATA(32) + WPAR(1) = 38
 *
 * The target drives ACK[0] at the TRN position (confirmed by read-path
 * IDCODE success), so discarding TRN shifts our ACK reading by 1 bit:
 * what we read as ACK[0:2] is actually {ACK[1], ACK[2], pull-up(1)}.
 * We correct for this offset before returning. */
static int swd_dp_write(uint8_t req, uint32_t data)
{
    swd_write_request(req);

    /* TRN→target: dead cycle (target starts driving ACK here, but we
     * must discard this clock to keep 38-clock frame alignment) */
    swdio_input();
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 1);
    ets_delay_us(s_swd_half_us);
    gpio_set_level(SWCLK_GPIO, 0);

    /* Read 3 clocks as ACK — shifted by 1 due to TRN discard */
    int raw_ack = 0;
    for (int i = 0; i < 3; i++) raw_ack |= (swd_read_bit() << i);

    /* TRN→host: target releases, host takes over */
    swd_trn_to_host();

    /* ARM IHI0031 B4.3.5: host MUST complete the data phase on EVERY write,
     * even on FAULT/WAIT.  Skipping the data clocks desynchronises the bus. */
    int par = 0;
    for (int i = 0; i < 32; i++) {
        int b = (data >> i) & 1;
        swd_write_bit(b);
        par ^= b;
    }
    swd_write_bit(par);

    swd_idle(8);

    /* Correct ACK offset: target drove ACK[0] at TRN (discarded), so our
     * raw_ack = {ACK[1], ACK[2], 1(pull-up)}.  Map back to standard values:
     *   raw 4 (100) → OK    (001) = 1    (ACK[1:2]=00, ACK[0] was 1)
     *   raw 5 (101) → WAIT  (010) = 2    (ACK[1]=1)
     *   raw 6 (110) → FAULT (100) = 4    (ACK[2]=1)
     *   raw 7 (111) → no response  = 7   (line floating) */
    int ack;
    switch (raw_ack & 3) {   /* bits [1:0] carry ACK[1:2] */
        case 0: ack = 1; break;   /* OK */
        case 1: ack = 2; break;   /* WAIT */
        case 2: ack = 4; break;   /* FAULT */
        default: ack = 7; break;  /* no response */
    }
    return ack;
}

/* Send N bits from a byte array, LSB-first within each byte.
 * Used for multi-byte canned SWD/SWJ sequences (dormant-to-SWD, etc.). */
static void swd_send_bits(const uint8_t *data, int nbits)
{
    swdio_output();
    for (int i = 0; i < nbits; i++) {
        swd_write_bit((data[i / 8] >> (i % 8)) & 1);
    }
}

/* Full switch + reset sequence (legacy JTAG-to-SWD) */
static void swd_connect(void)
{
    swd_line_reset();
    swd_jtag_to_swd();
    swd_line_reset();
    swd_idle(4);
}

/* Dormant-to-SWD connect sequence (ADIv5.2/ADIv6).
 * Works regardless of whether the DP is currently in JTAG, SWD, or dormant state.
 * Sequence per ARM IHI0031G and pyocd/OpenOCD implementations:
 *   1. Line reset (covers SWD reset + JTAG TLR)
 *   2. JTAG-to-dormant (39 bits: TMS-high + 31-bit select)
 *   3. Selection alert (8 HIGH + 128-bit alert pattern)
 *   4. SWD activation code (4 LOW + 8-bit code 0x1A)
 *   5. Line reset
 *   6. Idle cycles */
static void swd_connect_dormant(void)
{
    /* 1. Line reset — ≥50 clocks SWDIO=1 */
    swd_line_reset();

    /* 2. JTAG-to-dormant — 39 bits of 0x33BBBBBA LSB-first
     * (pyocd: swj_sequence(39, 0x33bbbbba)) */
    static const uint8_t j2d[] = {0xBA, 0xBB, 0xBB, 0x33, 0x00};
    swd_send_bits(j2d, 39);

    /* 3. Selection alert — 8 clocks HIGH + 128-bit alert
     * (OpenOCD swd.h: swd_seq_dormant_to_swd bytes 0..16) */
    static const uint8_t alert[] = {
        0xFF,  /* 8 clocks HIGH — abort any ongoing selection alert */
        0x92, 0xF3, 0x09, 0x62, 0x95, 0x2D, 0x85, 0x86,
        0xE9, 0xAF, 0xDD, 0xE3, 0xA2, 0x0E, 0xBC, 0x19,
    };
    swd_send_bits(alert, 136);

    /* 4. SWD activation code — 4 LOW + 0x1A LSB-first = 12 bits
     * (pyocd: swj_sequence(12, 0x01a0)) */
    static const uint8_t act[] = {0xA0, 0x01};
    swd_send_bits(act, 12);

    /* 5. Line reset — ≥50 clocks SWDIO=1 */
    swd_line_reset();

    /* 6. ≥2 idle cycles */
    swd_idle(4);
}

/* ── SWD request bytes ────────────────────────────────────────────────────── */

/* DP register request bytes:  start(1) APnDP RnW A[2] A[3] parity stop(0) park(1) */
#define REQ_IDCODE_R   0xA5   /* DP read  DPIDR    A=00 */
#define REQ_CTRLSTAT_R 0x8D   /* DP read  CTRL/STAT A=01 */
#define REQ_ABORT_W    0x81   /* DP write ABORT    A=00 */
#define REQ_CTRLSTAT_W 0xA9   /* DP write CTRL/STAT A=01 */
#define REQ_DP_W_SELECT 0xB1  /* DP write SELECT   A=10 */
#define REQ_DP_R_RDBUFF 0xBD  /* DP read  RDBUFF   A=11 */

/* AP register request bytes (AHB-AP, bank 0) */
#define REQ_AP_W_CSW   0xA3   /* AP write CSW  A=00 */
#define REQ_AP_R_CSW   0x87   /* AP read  CSW  A=00 */
#define REQ_AP_W_TAR   0x8B   /* AP write TAR  A=01 */
#define REQ_AP_W_DRW   0xBB   /* AP write DRW  A=11 */
#define REQ_AP_R_DRW   0x9F   /* AP read  DRW  A=11 */

/* CTRL/STAT bits for debug domain power-up */
#define CDBGPWRUPREQ  (1UL << 28)
#define CSYSPWRUPREQ  (1UL << 30)
#define CDBGPWRUPACK  (1UL << 29)
#define CSYSPWRUPACK  (1UL << 31)
/* MASKLANE [11:8]: pyocd includes 0x00000F00 in every CTRL/STAT write.
 * Without it, some SW-DP implementations may not latch the power request. */
#define MASKLANE      0x00000F00UL

/* AHB-AP CSW value matching pyocd's AHB-AP init for Cortex-M4:
 *   bit 31 = DbgSwEnable             (0x80000000) — REQUIRED; without it AP rejects all accesses
 *   bit 29 = MasterType=debug master (0x20000000)
 *   bit 25 = HPROT[1] data access   (0x02000000)
 *   bit 24 = HPROT[0] privileged    (0x01000000)
 *   bit  4 = AddrInc=single         (0x00000010)
 *   bit  1 = Size=word (32-bit)     (0x00000002)
 * Previous value 0xA2000052 also had bit31, but had wrong bit6 (reserved on STM32L4).
 * MASKLANE was also missing from CTRL/STAT write — both now fixed. */
#define AHB_CSW_32BIT_INC  0xA3000012UL

/* STM32L476 Flash controller (APB1 base 0x40022000) */
#define FLASH_REGS_BASE  0x40022000UL
#define FLASH_KEYR       (FLASH_REGS_BASE + 0x08UL)
#define FLASH_SR         (FLASH_REGS_BASE + 0x10UL)
#define FLASH_CR         (FLASH_REGS_BASE + 0x14UL)
#define FLASH_KEY1       0x45670123UL
#define FLASH_KEY2       0xCDEF89ABUL
#define FLASH_SR_BSY     (1UL << 16)
#define FLASH_SR_ERR     0x0000C3FAUL   /* OPERR|PROGERR|WRPERR|PGAERR|SIZERR|PGSERR|MISERR|FASTERR|RDERR|OPTVERR */
#define FLASH_CR_PG      (1UL << 0)
#define FLASH_CR_PER     (1UL << 1)
#define FLASH_CR_PNB(n)  ((uint32_t)(n) << 3)  /* page number bits [9:3] */
#define FLASH_CR_STRT    (1UL << 16)
#define FLASH_CR_LOCK    (1UL << 31)

/* STM32 Cortex-M4 debug registers */
#define DHCSR            0xE000EDF0UL
#define DHCSR_DBGKEY     0xA05F0000UL
#define DHCSR_C_DEBUGEN  (1UL << 0)
#define DHCSR_C_HALT     (1UL << 1)
#define AIRCR            0xE000ED0CUL
#define AIRCR_VECTKEY    0x05FA0000UL
#define AIRCR_SYSRESETREQ (1UL << 2)

#define STM32L4_FLASH_START 0x08000000UL
#define STM32L4_PAGE_SIZE   0x800UL        /* 2 KB per page */

static const char *ack_name(int ack);   /* forward declaration */

/* ── AHB-AP / memory access helpers ──────────────────────────────────────── */

static int ahb_ap_init(void)
{
    int ack; bool par;

    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t cs; int cs_ack; bool cs_par;

        if (attempt > 0) {
            printf("AHB-AP: retry %d — DAPABORT + re-select\n", attempt);
            /* Full AP reset: DAPABORT(0) + all sticky flags(4:1) */
            swd_dp_write(REQ_ABORT_W, 0x0000001F);
            ets_delay_us(500);
            cs = swd_dp_read(REQ_CTRLSTAT_R, &cs_ack, &cs_par);
            printf("  CS after ABORT: ACK=%d  CS=0x%08" PRIX32 " (STICKYERR=%d)\n",
                   cs_ack, cs, (int)((cs >> 5) & 1));
        }

        /* Select AHB-AP (AP=0, DP bank=0) */
        if (swd_dp_write(REQ_DP_W_SELECT, 0x00000000) != 1) {
            printf("AHB-AP SELECT failed (attempt %d)\n", attempt); continue;
        }

        /* Probe: read CSW before writing it.  Posted read: ACK reflects previous DP write. */
        swd_dp_read(REQ_AP_R_CSW, &ack, &par);   /* post CSW read — ACK from SELECT */
        uint32_t csw_pre = swd_dp_read(REQ_DP_R_RDBUFF, &ack, &par);
        cs = swd_dp_read(REQ_CTRLSTAT_R, &cs_ack, &cs_par);
        printf("  CSW pre-write: RDBUFF=0x%08" PRIX32 " ACK=%d  CS=0x%08" PRIX32 " STICKYERR=%d\n",
               csw_pre, ack, cs, (int)((cs >> 5) & 1));

        if (cs & (1UL << 5)) {
            printf("  AP already errored before CSW write — skip\n");
            continue;
        }

        /* Write CSW: DbgSwEnable(31) | MasterDebug(29) | HPROT1(25) | AddrInc=single | Size=word */
        if (swd_dp_write(REQ_AP_W_CSW, AHB_CSW_32BIT_INC) != 1) {
            printf("AHB-AP CSW write ACK failed (attempt %d)\n", attempt); continue;
        }

        /* Check CTRL/STAT for STICKYERR from the posted CSW write */
        cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
        if (ack != 1) { printf("AHB-AP: CTRL/STAT read failed ACK=%d\n", ack); continue; }
        if (cs & (1UL << 5)) {
            printf("AHB-AP: STICKYERR after CSW write (CS=0x%08" PRIX32 ", attempt %d)\n", cs, attempt);
            /* Check if CSW write actually worked despite STICKYERR (e.g. CSYSPWRUACK=0 transient) */
            swd_dp_write(REQ_ABORT_W, 0x0000001F);   /* clear STICKYERR */
            ets_delay_us(500);
            swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
            swd_dp_read(REQ_AP_R_CSW, &ack, &par);   /* posted read */
            swd_dp_read(REQ_AP_R_CSW, &ack, &par);   /* second posted read to flush stale RDBUFF */
            uint32_t csw_post = swd_dp_read(REQ_DP_R_RDBUFF, &ack, &par);
            cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
            printf("  CSW post-write (after ABORT): 0x%08" PRIX32 "  CS=0x%08" PRIX32 " STICKYERR=%d\n",
                   csw_post, cs, (int)((cs >> 5) & 1));
            /* If DbgSwEnable (bit31) is now 1, the write silently succeeded; proceed. */
            if ((csw_post & 0x80000000UL) && !(cs & (1UL << 5))) {
                printf("  CSW write worked silently (DbgSwEnable now 1) — proceeding\n");
                /* Re-write full CSW now that AP is enabled */
                swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
                swd_dp_write(REQ_AP_W_CSW, AHB_CSW_32BIT_INC);
                cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
                if (cs & (1UL << 5)) { printf("  STICKYERR on second write too\n"); continue; }
                printf("AHB-AP init OK (via silent-write path)  CS=0x%08" PRIX32 "\n", cs);
                return 0;
            }
            continue;  /* loop: DAPABORT at top of next iteration */
        }

        /* Verify CSW was actually written — read it back via RDBUFF pattern */
        swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
        swd_dp_read(REQ_AP_R_CSW, &ack, &par);   /* posted read */
        uint32_t csw_rb = swd_dp_read(REQ_DP_R_RDBUFF, &ack, &par);
        if (ack != 1) {
            printf("AHB-AP: CSW readback failed ACK=%d (attempt %d)\n", ack, attempt); continue;
        }
        printf("AHB-AP init OK  CS=0x%08" PRIX32 "  CSW=0x%08" PRIX32 "\n", cs, csw_rb);
        return 0;
    }

    printf("AHB-AP init failed after 3 attempts\n");
    return -1;
}

/* Write one 32-bit word to an AHB address */
static int mem_write32(uint32_t addr, uint32_t val)
{
    int a; bool par2;
    a = swd_dp_write(REQ_AP_W_TAR, addr);
    if (a != 1) {
        uint32_t cs2 = swd_dp_read(REQ_CTRLSTAT_R, &a, &par2);
        printf("  TAR write FAULT addr=0x%08" PRIX32 "  CS=0x%08" PRIX32 "\n", addr, cs2);
        return -1;
    }
    a = swd_dp_write(REQ_AP_W_DRW, val);
    if (a != 1) {
        uint32_t cs2 = swd_dp_read(REQ_CTRLSTAT_R, &a, &par2);
        printf("  DRW write FAULT addr=0x%08" PRIX32 "  CS=0x%08" PRIX32 "\n", addr, cs2);
        return -1;
    }
    return 0;
}

/* Read one 32-bit word from an AHB address (uses posted-read + RDBUFF) */
static int mem_read32(uint32_t addr, uint32_t *val)
{
    int ack; bool par;
    if (swd_dp_write(REQ_AP_W_TAR, addr) != 1) return -1;
    swd_dp_read(REQ_AP_R_DRW, &ack, &par);   /* posted — initiates transfer */
    if (ack != 1) return -1;
    *val = swd_dp_read(REQ_DP_R_RDBUFF, &ack, &par);
    return (ack == 1) ? 0 : -1;
}

/* ── STM32L476 Flash helpers ──────────────────────────────────────────────── */

/* Poll FLASH_SR until BSY=0; timeout ~5 s */
static int flash_wait_done(void)
{
    for (int i = 0; i < 50000; i++) {
        uint32_t sr;
        if (mem_read32(FLASH_SR, &sr) != 0) return -1;
        if (!(sr & FLASH_SR_BSY)) {
            if (sr & FLASH_SR_ERR) {
                printf("FLASH_SR error bits: 0x%08" PRIX32 "\n", sr);
                return -1;
            }
            return 0;
        }
        ets_delay_us(100);
    }
    printf("FLASH_SR BSY timeout\n");
    return -1;
}

static int flash_unlock(void)
{
    if (mem_write32(FLASH_KEYR, FLASH_KEY1) != 0) return -1;
    if (mem_write32(FLASH_KEYR, FLASH_KEY2) != 0) return -1;
    return 0;
}

static int flash_lock(void)
{
    return mem_write32(FLASH_CR, FLASH_CR_LOCK);
}

/* Erase one 2 KB page.  For Bank 1 pages 0-255: BKER=0, PNB=page.
 * For Bank 2 pages 0-255 (addresses 0x08100000+): set BKER=1. */
static int flash_erase_page(uint32_t page_num, bool bank2)
{
    if (flash_wait_done() != 0) return -1;
    uint32_t cr = FLASH_CR_PER | FLASH_CR_PNB(page_num);
    if (bank2) cr |= (1UL << 11);   /* BKER */
    if (mem_write32(FLASH_CR, cr | FLASH_CR_STRT) != 0) return -1;
    return flash_wait_done();
}

/* Program one 64-bit double-word at an 8-byte-aligned flash address.
 * STM32L476 requires two consecutive 32-bit writes without any intervening
 * access.  We exploit AHB-AP auto-increment (AddrInc=single in CSW). */
static int flash_program_dword(uint32_t addr, uint32_t lo, uint32_t hi)
{
    if (flash_wait_done() != 0) return -1;
    /* Set PG bit — TAR is left at FLASH_CR after this */
    if (mem_write32(FLASH_CR, FLASH_CR_PG) != 0) return -1;
    /* Write two consecutive words; TAR auto-increments after each DRW write */
    if (swd_dp_write(REQ_AP_W_TAR, addr) != 1) return -1;
    if (swd_dp_write(REQ_AP_W_DRW, lo)   != 1) return -1;
    if (swd_dp_write(REQ_AP_W_DRW, hi)   != 1) return -1;
    return flash_wait_done();
}

/* ── swd flash: HTTP download + SWD flash ─────────────────────────────────── */

#define SWD_FLASH_MAX_SIZE (64 * 1024)  /* 64 KB cap — fits internal heap at runtime; G3 DUT fw ~11 KB */

/* dut_fw partition header — 8 bytes at offset 0 of the "dut_fw" data partition.
 * Firmware binary follows immediately after the header. */
#define DUT_FW_PART_MAGIC  0xD07F0001UL
typedef struct {
    uint32_t magic;   /* DUT_FW_PART_MAGIC */
    uint32_t size;    /* firmware size in bytes */
} dut_fw_hdr_t;

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    bool     overflow;
} fw_dl_t;

static esp_err_t fw_http_event(esp_http_client_event_t *evt)
{
    fw_dl_t *dl = (fw_dl_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (dl->len + (size_t)evt->data_len > dl->cap) {
            dl->overflow = true;
            return ESP_FAIL;
        }
        memcpy(dl->buf + dl->len, evt->data, evt->data_len);
        dl->len += evt->data_len;
    }
    return ESP_OK;
}

static int do_swd_flash(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: swd flash <url> [nopwrcycle] [--store]\n"
               "       swd flash local [nopwrcycle]\n"
               "  <url>        download from HTTP URL, then flash DUT\n"
               "  --store      after URL download, save to dut_fw partition\n"
               "  local        flash DUT from stored dut_fw partition image\n"
               "  nopwrcycle   skip DUT power cycle (connect to running target)\n");
        return 1;
    }

    bool is_local   = (strcmp(argv[1], "local") == 0);
    bool nopwrcycle = false;
    bool store      = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "nopwrcycle") == 0) nopwrcycle = true;
        if (strcmp(argv[i], "--store")    == 0) store = true;
    }

    /* ── 1. Acquire firmware binary ── */
    fw_dl_t dl = { .cap = SWD_FLASH_MAX_SIZE };
    /* Prefer PSRAM to avoid exhausting internal heap on large DUT firmware */
    dl.buf = (uint8_t *)heap_caps_malloc(dl.cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dl.buf) {
        dl.buf = (uint8_t *)malloc(dl.cap);  /* fallback: internal heap */
    }
    if (!dl.buf) { printf("malloc failed (%u bytes)\n", (unsigned)dl.cap); return 1; }

    if (is_local) {
        /* Load from dut_fw partition */
        const esp_partition_t *part = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "dut_fw");
        if (!part) {
            printf("dut_fw partition not found\n");
            free(dl.buf); return 1;
        }
        dut_fw_hdr_t hdr;
        esp_err_t err = esp_partition_read(part, 0, &hdr, sizeof(hdr));
        if (err != ESP_OK || hdr.magic != DUT_FW_PART_MAGIC) {
            printf("dut_fw partition invalid (magic=0x%08" PRIX32 ", err=%s)\n"
                   "  Flash DUT from URL with --store first.\n",
                   hdr.magic, esp_err_to_name(err));
            free(dl.buf); return 1;
        }
        if (hdr.size == 0 || hdr.size > SWD_FLASH_MAX_SIZE) {
            printf("dut_fw partition size invalid (%u bytes)\n", (unsigned)hdr.size);
            free(dl.buf); return 1;
        }
        err = esp_partition_read(part, sizeof(hdr), dl.buf, hdr.size);
        if (err != ESP_OK) {
            printf("dut_fw partition read failed: %s\n", esp_err_to_name(err));
            free(dl.buf); return 1;
        }
        dl.len = hdr.size;
        printf("Loaded %u bytes from dut_fw partition\n", (unsigned)dl.len);
    } else {
        /* HTTP download */
        const char *url = argv[1];
        printf("Downloading %s ...\n", url);

        esp_http_client_config_t hcfg = {
            .url           = url,
            .event_handler = fw_http_event,
            .user_data     = &dl,
            .timeout_ms    = 15000,
            .buffer_size   = 4096,
        };
        esp_http_client_handle_t client = esp_http_client_init(&hcfg);
        esp_err_t err = esp_http_client_perform(client);
        int http_status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err != ESP_OK || dl.overflow || dl.len == 0 || http_status != 200) {
            printf("Download failed: %s  HTTP %d  %u bytes\n",
                   esp_err_to_name(err), http_status, (unsigned)dl.len);
            free(dl.buf);
            return 1;
        }
        printf("Downloaded %u bytes (HTTP %d)\n", (unsigned)dl.len, http_status);

        if (store) {
            const esp_partition_t *part = esp_partition_find_first(
                    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "dut_fw");
            if (!part) {
                printf("[WARN] dut_fw partition not found — skipping store\n");
            } else {
                /* Header + firmware must fit; erase in 4 KB sectors */
                size_t erase_size = sizeof(dut_fw_hdr_t) + dl.len;
                erase_size = (erase_size + 0xFFF) & ~0xFFF; /* round up to 4 KB */
                esp_err_t e = esp_partition_erase_range(part, 0, erase_size);
                dut_fw_hdr_t hdr = { .magic = DUT_FW_PART_MAGIC,
                                     .size  = (uint32_t)dl.len };
                if (e == ESP_OK) e = esp_partition_write(part, 0, &hdr, sizeof(hdr));
                if (e == ESP_OK) e = esp_partition_write(part, sizeof(hdr), dl.buf, dl.len);
                if (e == ESP_OK) {
                    printf("Stored %u bytes to dut_fw partition\n", (unsigned)dl.len);
                } else {
                    printf("[WARN] dut_fw store failed: %s\n", esp_err_to_name(e));
                }
            }
        }
    }

    /* ── 2. Assert PB-A and power-cycle DUT ────────────────────────────────── *
     * The HEF4051 mux (U8) must hold PB-A asserted for the entire flash         *
     * operation.  A blank DUT has no firmware to drive KEEPALIVE (PD14), so     *
     * the power latch drops the instant PB-A releases.                           *
     *                                                                             *
     * Sequence: assert PB-A → preset VDUT1 duty → disable 300 ms → enable       *
     * → wait 50 ms for DUT VDD to stabilise before SWD connect.                 */
    /* nopwrcycle: skip power cycle — connect to already-running DUT (set above) */
    mux_pba_assert();
    vdac_set_duty(0, 80);
    if (nopwrcycle) {
        printf("Asserting PB-A, ensuring VDUT1 on (no power cycle) ...\n");
        vdac_set_enable(0, true);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        printf("Asserting PB-A and power-cycling DUT (duty=80%%, off 1000 ms, on) ...\n");
        vdac_set_enable(0, false);
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1 s: ensure DUT board caps fully discharge → clean POR */
        vdac_set_enable(0, true);
        vTaskDelay(pdMS_TO_TICKS(200));  /* wait for DUT to boot (firmware starts within ~5 ms) */
    }

    /* ── 3. Connect SWD ── */
    swclk_init();
    swdio_init();

    /* Try dormant-to-SWD first (robust: works regardless of DP's prior state).
     * Fall back to legacy JTAG-to-SWD if dormant path fails IDCODE read. */
    int ack; bool par;
    uint32_t idcode;
    bool connected = false;

    printf("Connecting SWD (dormant-to-SWD) ...\n");
    swd_connect_dormant();
    idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
    if (ack == 1 && idcode == 0x2BA01477) {
        connected = true;
    } else {
        printf("  dormant path: ACK=%d ID=0x%08" PRIX32 " — trying JTAG-to-SWD ...\n", ack, idcode);
        swd_connect();
        idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
        if (ack == 1 && idcode == 0x2BA01477) connected = true;
    }
    if (!connected) {
        printf("IDCODE fail  ACK=%d  ID=0x%08" PRIX32 "  (exp 0x2BA01477)\n", ack, idcode);
        free(dl.buf); return 1;
    }
    printf("IDCODE OK: 0x%08" PRIX32 "\n", idcode);

    /* Step 3: ABORT — DAPABORT (bit 0) aborts pending AP transactions;
     * bits [4:1] clear all DP sticky error flags. */
    int abort_ack = swd_dp_write(REQ_ABORT_W, 0x0000001F);
    printf("ABORT ACK=%d (%s)\n", abort_ack, ack_name(abort_ack));

    /* Step 3b: Set SELECT=0 explicitly (DP bank 0, AP 0) before power-up write.
     * After a line reset the DP SELECT field is undefined; ensure it's 0 so
     * subsequent CTRL/STAT accesses map to bank 0 and not DLCR/TARGETID. */
    swd_dp_write(REQ_DP_W_SELECT, 0x00000000);

    /* Step 4: Request power-up for debug + system domains.
     * Include MASKLANE [11:8] as pyocd does in every CTRL/STAT write. */
    int pwrup_ack = swd_dp_write(REQ_CTRLSTAT_W, CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE);
    printf("Power-up write ACK=%d (%s)\n", pwrup_ack, ack_name(pwrup_ack));
    if (pwrup_ack != 1) { free(dl.buf); return 1; }

    /* Step 5: Poll CTRL/STAT for both power ACKs (allow 5 s — pyocd uses 5 s).
     * Print first sample (shows reset state) and then every 50 ms. */
    uint32_t cs = 0;
    bool pwrup_ok = false;
    for (int i = 0; i < 50000; i++) {
        cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
        if (ack == 1 && (cs & CDBGPWRUPACK) && (cs & CSYSPWRUPACK)) { pwrup_ok = true; break; }
        if (i == 0 || i % 500 == 0)
            printf("  [%3d ms] CS=0x%08" PRIX32 " ACK=%d par=%d "
                   "CDBGACK=%d CSYSACK=%d STICKYERR=%d\n",
                   i/10, cs, ack, (int)par,
                   (int)((cs >> 29) & 1), (int)((cs >> 31) & 1), (int)((cs >> 5) & 1));
        ets_delay_us(100);
    }
    printf("Power-up poll done: ACK=%d  CS=0x%08" PRIX32 "  CDBGACK=%d  CSYSACK=%d\n",
           ack, cs, (int)((cs >> 29) & 1), (int)((cs >> 31) & 1));

    if (ack != 1 || !(cs & CDBGPWRUPACK)) {
        printf("Debug power-up failed (no CDBGPWRUPACK)\n");
        free(dl.buf); return 1;
    }

    /* Step 5b: CSYSPWRUACK=0 — attempt CDBGRSTREQ to reset the debug domain.
     * This clears the AP state (including DbgSwEnable) and re-initialises it.
     * A previous SWD session that wrote CSW with DbgSwEnable=0 (bit31=0) will
     * leave the AP disabled even across reconnects; CDBGRSTREQ is the only
     * software path to reset it without a full chip power cycle. */
    if (!pwrup_ok) {
        printf("CSYSPWRUACK=0 — trying CDBGRSTREQ to reset AP ...\n");
        swd_dp_write(REQ_CTRLSTAT_W,
                     CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE | (1UL << 26)); /* CDBGRSTREQ */
        /* Poll for CDBGRSTACK (bit 27) up to 200 ms */
        uint32_t cdbgrstack_seen = 0;
        for (int i = 0; i < 2000; i++) {
            cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
            if (cs & (1UL << 27)) { cdbgrstack_seen = 1; break; }
            ets_delay_us(100);
        }
        printf("  CDBGRSTACK=%d  CS=0x%08" PRIX32 "\n", (int)cdbgrstack_seen, cs);
        /* Clear CDBGRSTREQ, keep power requests */
        swd_dp_write(REQ_CTRLSTAT_W, CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE);
        /* Re-poll for CSYSPWRUACK (1 s) */
        for (int i = 0; i < 10000; i++) {
            cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
            if (ack == 1 && (cs & CDBGPWRUPACK) && (cs & CSYSPWRUPACK)) {
                printf("CSYSPWRUACK asserted after CDBGRSTREQ\n");
                break;
            }
            if (i % 200 == 0)
                printf("  [%3d ms] CS=0x%08" PRIX32 " CSYSACK=%d\n",
                       i/10, cs, (int)((cs >> 31) & 1));
            ets_delay_us(100);
        }
        printf("After CDBGRSTREQ: ACK=%d  CS=0x%08" PRIX32
               "  CDBGACK=%d  CSYSACK=%d\n",
               ack, cs, (int)((cs >> 29) & 1), (int)((cs >> 31) & 1));
        if (ack != 1 || !(cs & CDBGPWRUPACK)) {
            printf("Debug power-up still failed after CDBGRSTREQ\n");
            free(dl.buf); return 1;
        }
        if (!(cs & CSYSPWRUPACK)) {
            printf("CSYSPWRUACK still 0 after CDBGRSTREQ — proceeding anyway\n");
        }
    }

    /* ── 3. Init AHB-AP ── */
    if (ahb_ap_init() != 0) { free(dl.buf); return 1; }

    /* ── 4. Verify AHB access, then halt CPU ── */
    /* Try reading DHCSR first — if this FAULT, AHB is truly inaccessible
     * (possible RDP=2 or system domain not powered).  The read doesn't write
     * anything so it's safe to attempt before the halt write. */
    uint32_t dhcsr_pre = 0;
    if (mem_read32(DHCSR, &dhcsr_pre) != 0) {
        printf("AHB read FAULT (DHCSR) — AHB inaccessible; possible RDP=2 or system domain off\n");
        free(dl.buf); return 1;
    }
    printf("DHCSR read=0x%08" PRIX32 "  (AHB accessible)\n", dhcsr_pre);

    if (mem_write32(DHCSR, DHCSR_DBGKEY | DHCSR_C_HALT | DHCSR_C_DEBUGEN) != 0) {
        printf("CPU halt write failed\n"); free(dl.buf); return 1;
    }
    /* Brief wait, then verify halt */
    ets_delay_us(500);
    uint32_t dhcsr_val = 0;
    mem_read32(DHCSR, &dhcsr_val);
    printf("CPU halted  DHCSR=0x%08" PRIX32 "\n", dhcsr_val);

    /* ── 5. Unlock flash ── */
    if (flash_unlock() != 0) {
        printf("Flash unlock failed\n");
        free(dl.buf); return 1;
    }
    printf("Flash unlocked\n");

    /* ── 6. Erase required pages ── */
    uint32_t fw_len_pad = (dl.len + 7) & ~(uint32_t)7;   /* round up to 8-byte boundary */
    uint32_t pages = (fw_len_pad + STM32L4_PAGE_SIZE - 1) / STM32L4_PAGE_SIZE;
    printf("Erasing %u pages (%u KB) ...\n", (unsigned)pages, (unsigned)(pages * 2));

    /* All pages within Bank 1 (up to page 255, addresses 0x08000000–0x080FFFFF) */
    for (uint32_t p = 0; p < pages; p++) {
        if (flash_erase_page(p, false) != 0) {
            printf("Erase page %u failed\n", (unsigned)p);
            flash_lock(); free(dl.buf); return 1;
        }
    }
    printf("Erase done\n");

    /* ── 7. Program double-words ── */
    /* Pad firmware buffer to 8-byte boundary with 0xFF (erased flash value) */
    while (dl.len < fw_len_pad) dl.buf[dl.len++] = 0xFF;

    printf("Programming %u bytes ...\n", (unsigned)fw_len_pad);
    uint32_t addr = STM32L4_FLASH_START;
    uint32_t written = 0;
    for (size_t i = 0; i < fw_len_pad; i += 8, addr += 8) {
        uint32_t lo, hi;
        memcpy(&lo, dl.buf + i,     4);
        memcpy(&hi, dl.buf + i + 4, 4);
        if (flash_program_dword(addr, lo, hi) != 0) {
            printf("Program failed at 0x%08" PRIX32 "\n", addr);
            flash_lock(); free(dl.buf); return 1;
        }
        written += 8;
        /* Progress every 2 KB */
        if ((written % STM32L4_PAGE_SIZE) == 0) {
            printf("  %u / %u bytes\n", (unsigned)written, (unsigned)fw_len_pad);
        }
    }
    printf("Program done\n");

    /* ── 8. Lock flash ── */
    flash_lock();

    /* ── 9. Reset target (clear debug halt, then system reset) ── */
    mem_write32(DHCSR, DHCSR_DBGKEY);   /* clear C_DEBUGEN | C_HALT */
    ets_delay_us(100);
    mem_write32(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

    /* Wait briefly for reset to complete, then release PB-A.
     * The newly flashed DUT firmware must assert KEEPALIVE (PD14) to maintain
     * the power latch after PB-A is released. */
    vTaskDelay(pdMS_TO_TICKS(200));
    mux_pba_release();

    free(dl.buf);
    printf("[PASS] swd flash complete  %u bytes at 0x%08" PRIX32 "\n",
           (unsigned)fw_len_pad, STM32L4_FLASH_START);
    return 0;
}

/* ── swd probe command ────────────────────────────────────────────────────── */

static const char *ack_name(int ack)
{
    return ack == 1 ? "OK" : ack == 2 ? "WAIT" : ack == 4 ? "FAULT" : "???";
}

static int do_swd_probe(int argc, char **argv)
{
    /* optional: swd probe [half_us] [nojtagtoswd]
     * half_us on the command line sets the global s_swd_half_us so all
     * bit-level primitives (swd_write_bit, swd_read_bit, etc.) use it. */
    bool skip_j2s = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "nojtagtoswd") == 0) skip_j2s = true;
        else {
            int v = atoi(argv[i]);
            if (v > 0) s_swd_half_us = v;
        }
    }

    printf("SWD probe  SWCLK=GPIO%d  SWDIO=GPIO%d  half=%dµs (~%dkHz)  %s\n",
           SWCLK_GPIO, SWDIO_GPIO, s_swd_half_us,
           s_swd_half_us > 0 ? 1000 / (2 * s_swd_half_us) : 9999,
           skip_j2s ? "no-j2swd" : "with-j2swd");

    swclk_init();
    swdio_init();

    /* local wrapper using the global — kept for connect-sequence readability */
#define HALF() ets_delay_us(s_swd_half_us)

    for (int attempt = 1; attempt <= 3; attempt++) {
        printf("Attempt %d\n", attempt);

        /* ── connect sequence ── */
        /* line reset: ≥50 clocks SWDIO=1 */
        swdio_output();
        gpio_set_level(SWDIO_GPIO, 1);
        for (int i = 0; i < 56; i++) {
            HALF(); gpio_set_level(SWCLK_GPIO, 1);
            HALF(); gpio_set_level(SWCLK_GPIO, 0);
        }

        if (!skip_j2s) {
            /* JTAG-to-SWD magic 0xE79E LSB-first */
            uint16_t seq = 0xE79E;
            for (int i = 0; i < 16; i++) {
                gpio_set_level(SWDIO_GPIO, seq & 1 ? 1 : 0);
                HALF(); gpio_set_level(SWCLK_GPIO, 1);
                HALF(); gpio_set_level(SWCLK_GPIO, 0);
                seq >>= 1;
            }
            /* second line reset */
            gpio_set_level(SWDIO_GPIO, 1);
            for (int i = 0; i < 56; i++) {
                HALF(); gpio_set_level(SWCLK_GPIO, 1);
                HALF(); gpio_set_level(SWCLK_GPIO, 0);
            }
        }

        /* idle cycles */
        gpio_set_level(SWDIO_GPIO, 0);
        for (int i = 0; i < 8; i++) {
            HALF(); gpio_set_level(SWCLK_GPIO, 1);
            HALF(); gpio_set_level(SWCLK_GPIO, 0);
        }

        /* ── read IDCODE (req=0xA5) ── */
        int ack; bool parity_ok;
        uint32_t idcode = swd_dp_read(REQ_IDCODE_R, &ack, &parity_ok);
        printf("  IDCODE   ACK=%d (%s)  val=0x%08" PRIX32 "  par=%s\n",
               ack, ack_name(ack), idcode, parity_ok ? "OK" : "FAIL");

        if (ack == 1 && parity_ok) {
            if (idcode == 0x2BA01477) {
                printf("[PASS] STM32L476 Cortex-M4 confirmed\n");
                return 0;
            }
            printf("[FAIL] Unexpected IDCODE (exp 0x2BA01477)\n");
            return 1;
        }

        /* ── read CTRL/STAT before abort ── */
        uint32_t ctrlstat = swd_dp_read(REQ_CTRLSTAT_R, &ack, &parity_ok);
        printf("  CS_pre   ACK=%d (%s)  val=0x%08" PRIX32 "\n",
               ack, ack_name(ack), ctrlstat);

        /* ── ABORT: clear all sticky errors ── */
        int abort_ack = swd_dp_write(REQ_ABORT_W, 0x0000001E);
        printf("  ABORT    ACK=%d (%s)\n", abort_ack, ack_name(abort_ack));

        /* ── read CTRL/STAT after abort ── */
        ctrlstat = swd_dp_read(REQ_CTRLSTAT_R, &ack, &parity_ok);
        printf("  CS_post  ACK=%d (%s)  val=0x%08" PRIX32 "\n",
               ack, ack_name(ack), ctrlstat);

        /* ── power-up debug domain ── */
        swd_dp_write(REQ_CTRLSTAT_W, CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE);
        ctrlstat = swd_dp_read(REQ_CTRLSTAT_R, &ack, &parity_ok);
        printf("  CS_pwrup ACK=%d (%s)  val=0x%08" PRIX32 "\n",
               ack, ack_name(ack), ctrlstat);

        /* ── retry IDCODE ── */
        idcode = swd_dp_read(REQ_IDCODE_R, &ack, &parity_ok);
        printf("  IDCODE2  ACK=%d (%s)  val=0x%08" PRIX32 "  par=%s\n",
               ack, ack_name(ack), idcode, parity_ok ? "OK" : "FAIL");

        if (ack == 1 && parity_ok && idcode == 0x2BA01477) {
            printf("[PASS] STM32L476 Cortex-M4 confirmed\n");
            return 0;
        }
    }
#undef HALF

    printf("[FAIL] Could not read IDCODE after 3 attempts\n");
    return 1;
}

/* ── swd bare: minimal connect then N repeated IDCODE reads (no reconnect) ── */

static int do_swd_bare(int argc, char **argv)
{
    int n = (argc >= 2) ? atoi(argv[1]) : 5;
    if (n < 1) n = 1;

    swclk_init();
    swdio_init();

    printf("bare: line-reset + 4 idle + %d x IDCODE reads (no reconnect between)\n", n);

    /* one connect: line reset → idle (no JTAG-to-SWD) */
    swdio_output();
    gpio_set_level(SWDIO_GPIO, 1);
    for (int i = 0; i < 56; i++) {
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 1);
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 0);
    }
    gpio_set_level(SWDIO_GPIO, 0);
    for (int i = 0; i < 4; i++) {
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 1);
        ets_delay_us(s_swd_half_us);
        gpio_set_level(SWCLK_GPIO, 0);
    }

    for (int r = 0; r < n; r++) {
        int ack; bool parity_ok;
        uint32_t id = swd_dp_read(REQ_IDCODE_R, &ack, &parity_ok);
        printf("  read[%d]  ACK=%d (%s)  val=0x%08" PRIX32 "  par=%s\n",
               r, ack, ack_name(ack), id, parity_ok ? "OK" : "FAIL");
    }
    return 0;
}

/* ── swd gpio diagnostic ──────────────────────────────────────────────────── */

static int do_swd_gpio(int argc, char **argv)
{
    (void)argc; (void)argv;
    swclk_init();
    swdio_init();   /* output, pull-up, initial HIGH */

    printf("SWDIO=GPIO%d  SWCLK=GPIO%d\n", SWDIO_GPIO, SWCLK_GPIO);

    /* Drive HIGH, switch to input, sample 5× */
    gpio_set_level(SWDIO_GPIO, 1);
    ets_delay_us(50);
    swdio_input();
    printf("After drive-HIGH → input: ");
    for (int i = 0; i < 5; i++) { printf("%d ", gpio_get_level(SWDIO_GPIO)); ets_delay_us(100); }
    printf("\n");

    /* Drive LOW, switch to input, sample 5× at increasing intervals */
    swdio_output();
    gpio_set_level(SWDIO_GPIO, 0);
    ets_delay_us(50);
    swdio_input();
    printf("After drive-LOW → input (0 10 50 200 1000 us): ");
    { static const int d[] = {0, 10, 50, 200, 1000};
      for (int i = 0; i < 5; i++) { ets_delay_us(d[i]); printf("%d ", gpio_get_level(SWDIO_GPIO)); } }
    printf("\n");

    /* Restore */
    swdio_output();
    gpio_set_level(SWDIO_GPIO, 1);
    printf("Done\n");
    return 0;
}

/* ── swd cs: connect + power-up + CTRL/STAT + AP IDR diagnostic ───────────── */

static void cs_decode(const char *label, uint32_t cs)
{
    printf("  %s: 0x%08" PRIX32 "  CDBGPREQ=%d CDBGPACK=%d CSYSPREQ=%d CSYSPACK=%d "
           "MASKLANE=0x%X WDATAERR=%d STICKYERR=%d READOK=%d\n",
           label, cs,
           (int)((cs >> 28) & 1), (int)((cs >> 29) & 1),
           (int)((cs >> 30) & 1), (int)((cs >> 31) & 1),
           (int)((cs >> 8) & 0xF), (int)((cs >> 7) & 1),
           (int)((cs >> 5) & 1), (int)((cs >> 6) & 1));
}

static int do_swd_cs(int argc, char **argv)
{
    bool use_dormant = (argc >= 2 && strcmp(argv[1], "dormant") == 0);
    swclk_init(); swdio_init();
    if (use_dormant) {
        printf("Using dormant-to-SWD connect\n");
        swd_connect_dormant();
    } else {
        swd_connect();
    }

    int ack; bool par;
    uint32_t idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
    printf("IDCODE ACK=%d  val=0x%08" PRIX32 "  par=%d\n", ack, idcode, par);
    if (ack != 1) return 1;

    /* Pristine CS read — before ANY writes, shows true cold-boot/reset state */
    uint32_t cs_pristine = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
    cs_decode("CS pristine (pre-ABORT)", cs_pristine);

    /* ABORT — clear sticky errors from previous sessions */
    int ab = swd_dp_write(REQ_ABORT_W, 0x0000001E);
    printf("ABORT ACK=%d\n", ab);

    /* SELECT=0 — DPBANKSEL=0 so A[3:2]=01 → CTRL/STAT */
    swd_dp_write(REQ_DP_W_SELECT, 0x00000000);

    uint32_t cs0 = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
    cs_decode("CS after ABORT+SELECT", cs0);

    /* Write power-up request (CDBGPWRUPREQ + CSYSPWRUPREQ + MASKLANE) */
    uint32_t pwrup = CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE;
    printf("Writing CTRL/STAT = 0x%08" PRIX32 "\n", pwrup);
    int pa = swd_dp_write(REQ_CTRLSTAT_W, pwrup);
    printf("PwrUp write ACK=%d\n", pa);

    uint32_t cs1 = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
    cs_decode("CS after write", cs1);

    /* Poll for power ACKs (up to 200 ms) */
    uint32_t cs = cs1;
    if (!(cs & CSYSPWRUPACK)) {
        for (int i = 0; i < 2000; i++) {
            cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
            if (i % 200 == 0)
                printf("  [%3dms] CS=0x%08" PRIX32 "\n", i / 10, cs);
            if (ack == 1 && (cs & CDBGPWRUPACK) && (cs & CSYSPWRUPACK)) {
                printf("  [%3dms] Both power ACKs asserted!\n", i / 10);
                break;
            }
            ets_delay_us(100);
        }
    }
    cs_decode("CS final", cs);

    /* ── Verify SELECT works: read TARGETID at DPBANKSEL=2 ── */
    printf("\n--- SELECT verify (TARGETID at DPBANKSEL=2) ---\n");
    swd_dp_write(REQ_DP_W_SELECT, 0x00000002);
    uint32_t tid = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
    printf("  DPBANKSEL=2 A=01: ACK=%d  val=0x%08" PRIX32 "\n", ack, tid);

    swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
    uint32_t cs_back = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
    printf("  DPBANKSEL=0 A=01: ACK=%d  val=0x%08" PRIX32 "\n", ack, cs_back);

    if (tid != cs_back)
        printf("  SELECT works! TARGETID=0x%08" PRIX32 " vs CS=0x%08" PRIX32 "\n", tid, cs_back);
    else
        printf("  Same value — DPv0 (no TARGETID register)\n");

    return 0;
}

/* ── swd sniff: passive bit capture on SWCLK rising edges ────────────────── *
 * Both pins become high-impedance inputs.  Bits are sampled on each rising   *
 * edge of SWCLK.  Use while STLink (or any SWD master) drives the lines.    *
 *                                                                             *
 * Output: raw bit stream, then decoded as SWD request/ack/data frames.       *
 * SWD turnaround cycles appear as direction changes in the bit stream —      *
 * the decoder marks request bytes (8b), ACK (3b), data (32b+par).           */

/* Direct register read for sub-us GPIO sampling on ESP32-S3.
 * SWCLK=GPIO37 and SWDIO=GPIO38 both live in GPIO_IN1 (GPIOs 32–63).
 * Bit offset within the register = GPIO# - 32. */
#define GPIO_IN1_ADDR   0x60004040UL  /* GPIO_IN1_REG: input for GPIO32-49 on ESP32-S3 */
#define SWCLK_BIT       (1UL << (SWCLK_GPIO - 32))
#define SWDIO_BIT       (1UL << (SWDIO_GPIO - 32))
#define GPIO_IN1()      (*(volatile uint32_t *)GPIO_IN1_ADDR)

static int do_swd_sniff(int argc, char **argv)
{
    int nbits = (argc >= 2) ? atoi(argv[1]) : 2000;
    if (nbits < 8)    nbits = 8;
    if (nbits > 8000) nbits = 8000;

    /* Allocate bit buffer */
    uint8_t *bits = (uint8_t *)malloc((size_t)nbits);
    if (!bits) { printf("malloc failed\n"); return 1; }

    /* SWDIO: pull-up (external pull-up on TIE/DUT side, matches idle HIGH).
     * SWCLK: pull-down (hardware floats LOW when disconnected; pull-up would
     *        cause the shared-deadline timeout logic to wrap on first use). */
    gpio_config_t cfg_swdio = {
        .pin_bit_mask  = (1ULL << SWDIO_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    /* SWCLK: floating input — no pull-up or pull-down.
     * A pull-down would divide STLink's driven voltage if there is a series
     * resistor in the TCC path, potentially dropping below the detection
     * threshold.  STLink holds SWCLK LOW when idle so floating is safe. */
    gpio_config_t cfg_swclk = {
        .pin_bit_mask  = (1ULL << SWCLK_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg_swdio);
    gpio_config(&cfg_swclk);

    printf("swd sniff: waiting for SWCLK activity (%d bits)...\n"
           "  (start SWD master now; 30 s timeout)\n", nbits);
    fflush(stdout);

    /* Wait up to 30 s for first SWCLK rising edge using esp_timer for safety.
     * Busy-wait is intentional — we can't afford task context switches during
     * capture.  Use a time-based deadline to avoid uint32_t wrap bugs. */
    int64_t deadline_us = esp_timer_get_time() + 30000000LL;  /* 30 s */

    /* Drain any current HIGH state (master may be idle-high) */
    while ((GPIO_IN1() & SWCLK_BIT) && esp_timer_get_time() < deadline_us) {}
    /* Wait for rising edge */
    while (!(GPIO_IN1() & SWCLK_BIT) && esp_timer_get_time() < deadline_us) {}

    if (esp_timer_get_time() >= deadline_us) {
        printf("Timeout — no SWCLK activity\n");
        free(bits);
        return 1;
    }

    /* Capture nbits: sample SWDIO at each rising SWCLK edge.
     * Pure busy-wait — no ets_delay, no task yields — minimises jitter. */
    for (int i = 0; i < nbits; i++) {
        /* We arrive here on a rising edge (SWCLK just went HIGH). Sample. */
        bits[i] = (GPIO_IN1() & SWDIO_BIT) ? 1 : 0;
        /* Wait for SWCLK LOW */
        while (GPIO_IN1() & SWCLK_BIT);
        /* Wait for next SWCLK HIGH */
        while (!(GPIO_IN1() & SWCLK_BIT));
    }

    /* ── Print raw bits ── */
    printf("\n--- raw bits (%d), LSB-first within byte groups ---\n", nbits);
    for (int i = 0; i < nbits; i++) {
        printf("%d", bits[i]);
        if ((i + 1) % 8  == 0) printf(" ");
        if ((i + 1) % 64 == 0) printf("\n");
    }
    if (nbits % 64 != 0) printf("\n");

    /* ── Decode SWD frames ──────────────────────────────────────────────────
     * Walk the bit stream recognising SWD packets by structure:
     *   REQUEST (8b)  START=1  APnDP  RnW  A[2]  A[3]  parity  STOP=0  PARK=1
     *   ACK     (3b)  ACK[0..2]  (1=OK, 2=WAIT, 4=FAULT)
     *   DATA   (32b)  + 1 parity
     *   IDLE    (≥1)  SWDIO=0 trailing bits
     * We heuristically detect REQUEST bytes by checking START=1, STOP=0, PARK=1. */
    printf("\n--- SWD decode ---\n");
    int pos = 0;
    int frame = 0;

    while (pos + 8 <= nbits) {
        uint8_t b = 0;
        for (int k = 0; k < 8; k++) b |= (bits[pos + k] << k);

        bool start = (b & 0x01) != 0;
        bool stop  = (b & 0x40) == 0;
        bool park  = (b & 0x80) != 0;

        if (!start || !stop || !park) {
            /* Not a valid request byte — skip one bit */
            pos++;
            continue;
        }

        bool apndp  = (b >> 1) & 1;
        bool rnw    = (b >> 2) & 1;
        uint8_t adr = (b >> 3) & 0x3;
        uint8_t par = (b >> 4) & 0x1;

        /* Verify parity: START(1)+APnDP+RnW+A[2]+A[3] should XOR to par */
        uint8_t exp_par = 1 ^ (apndp?1:0) ^ (rnw?1:0) ^ ((adr)&1) ^ ((adr>>1)&1);
        if (exp_par != par) { pos++; continue; }

        /* Looks like a request byte */
        printf("[%4d] REQ=0x%02X  %s %s  A=%d  ", pos, b,
               apndp ? "AP" : "DP",
               rnw   ? "R " : "W ",
               adr);

        pos += 8;   /* consume request */
        /* Turnaround: 1 bit (not printed — direction changes here) */
        pos += 1;

        /* ACK: 3 bits */
        if (pos + 3 > nbits) { printf("(truncated ACK)\n"); break; }
        int ack = bits[pos] | (bits[pos+1] << 1) | (bits[pos+2] << 2);
        printf("ACK=%d(%s)", ack, ack==1?"OK":ack==2?"WAIT":ack==4?"FAULT":"?");
        pos += 3;

        if (ack == 1) {
            if (pos + 33 > nbits) { printf("  (truncated data)\n"); break; }
            uint32_t data = 0;
            int dpar = 0;
            for (int k = 0; k < 32; k++) {
                data |= ((uint32_t)bits[pos+k] << k);
                dpar ^= bits[pos+k];
            }
            int dpar_bit = bits[pos+32];
            printf("  DATA=0x%08" PRIX32 "  par=%s", data,
                   dpar_bit == dpar ? "OK" : "FAIL");
            pos += 33;
            if (!rnw) pos += 1;   /* write: one turnaround after data */
        } else {
            /* FAULT or WAIT: target still drives 33 clocks, data invalid */
            pos += 33;
            pos += 1;   /* turnaround */
        }
        printf("\n");
        frame++;
    }
    printf("[decode] %d frames, %d bits consumed / %d captured\n", frame, pos, nbits);

    free(bits);
    return 0;
}

/* ── swd gpio watch: count SWCLK HIGH samples over N ms ─────────────────── */
static int do_swd_gpio_watch(int argc, char **argv)
{
    int ms = (argc >= 2) ? atoi(argv[1]) : 5000;
    if (ms < 100)  ms = 100;
    if (ms > 30000) ms = 30000;

    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << SWCLK_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    printf("Watching GPIO%d for %d ms (floating input)...\n", SWCLK_GPIO, ms);
    fflush(stdout);

    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000LL;
    uint64_t highs = 0, total = 0;
    while (esp_timer_get_time() < end) {
        if (GPIO_IN1() & SWCLK_BIT) highs++;
        total++;
    }
    printf("Samples: %llu  HIGH: %llu  LOW: %llu  (%.2f%% HIGH)\n",
           (unsigned long long)total,
           (unsigned long long)highs,
           (unsigned long long)(total - highs),
           total ? 100.0 * (double)highs / (double)total : 0.0);
    return 0;
}

/* ── STM32 UID96 read ────────────────────────────────────────────────────── */

/* STM32L476 unique device ID: 96 bits at 0x1FFF7590 (RM0351 §47.1) */
#define STM32_UID_BASE  0x1FFF7590UL

/* Read the STM32 UID96 via SWD.  Assumes SWD is already connected and
 * AHB-AP is initialised.  Writes 12 bytes to uid_out.
 * Returns 0 on success, -1 on read failure. */
static int swd_read_uid96(uint8_t uid_out[12])
{
    for (int i = 0; i < 3; i++) {
        uint32_t w;
        if (mem_read32(STM32_UID_BASE + (uint32_t)(i * 4), &w) != 0) {
            printf("UID96 read failed at word %d\n", i);
            return -1;
        }
        uid_out[i * 4 + 0] = (uint8_t)(w >>  0);
        uid_out[i * 4 + 1] = (uint8_t)(w >>  8);
        uid_out[i * 4 + 2] = (uint8_t)(w >> 16);
        uid_out[i * 4 + 3] = (uint8_t)(w >> 24);
    }
    return 0;
}

/* Public API — connect SWD, read UID96, format as 24-char uppercase hex.
 * out must be at least 25 bytes.  Returns length written (24) or 0. */
int swd_read_dut_uid(char *out, size_t out_sz)
{
    if (out_sz < 25) return 0;
    out[0] = '\0';

    swclk_init();
    swdio_init();

    /* Connect — try dormant-to-SWD, fall back to legacy JTAG-to-SWD */
    int ack; bool par;
    uint32_t idcode;
    bool connected = false;

    swd_connect_dormant();
    idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
    if (ack == 1 && idcode == 0x2BA01477) {
        connected = true;
    } else {
        swd_connect();
        idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
        if (ack == 1 && idcode == 0x2BA01477) connected = true;
    }
    if (!connected) return 0;

    /* ABORT + SELECT + power-up */
    swd_dp_write(REQ_ABORT_W, 0x0000001F);
    swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
    swd_dp_write(REQ_CTRLSTAT_W, CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE);

    /* Poll for power-up ACK (100 ms timeout) */
    uint32_t cs = 0;
    for (int i = 0; i < 1000; i++) {
        cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
        if ((cs & (CDBGPWRUPACK | CSYSPWRUPACK)) == (CDBGPWRUPACK | CSYSPWRUPACK)) break;
        ets_delay_us(100);
    }
    if ((cs & (CDBGPWRUPACK | CSYSPWRUPACK)) != (CDBGPWRUPACK | CSYSPWRUPACK)) return 0;

    /* Init AHB-AP */
    if (ahb_ap_init() != 0) return 0;

    /* Read UID96 */
    uint8_t uid[12];
    if (swd_read_uid96(uid) != 0) return 0;

    /* Format as 24-char uppercase hex */
    for (int i = 0; i < 12; i++) {
        snprintf(out + i * 2, 3, "%02X", uid[i]);
    }
    return 24;
}

static int do_swd_uid(int argc, char **argv)
{
    (void)argc; (void)argv;
    char uid_str[25];
    int len = swd_read_dut_uid(uid_str, sizeof(uid_str));
    if (len == 0) {
        printf("[FAIL] Could not read STM32 UID96\n");
        return 1;
    }
    printf("STM32 UID96: %s\n", uid_str);
    return 0;
}

/* swd read <addr_hex> [n_words] — read memory via SWD (AHB-AP).
 * Requires DUT powered and SWD connected (run swd probe or swd uid first,
 * or this command does its own connect). */
static int do_swd_read(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: swd read <addr_hex> [n_words]\n"); return 1; }
    uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 16);
    int nwords = (argc >= 3) ? atoi(argv[2]) : 1;
    if (nwords < 1 || nwords > 256) { printf("n_words must be 1-256\n"); return 1; }

    /* Connect + init AHB-AP */
    swclk_init();
    swdio_init();
    int ack; bool par;
    swd_connect_dormant();
    uint32_t idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
    if (ack != 1 || idcode != 0x2BA01477) {
        swd_connect();
        idcode = swd_dp_read(REQ_IDCODE_R, &ack, &par);
        if (ack != 1 || idcode != 0x2BA01477) {
            printf("[FAIL] SWD connect failed\n"); return 1;
        }
    }
    swd_dp_write(REQ_ABORT_W, 0x0000001F);
    swd_dp_write(REQ_DP_W_SELECT, 0x00000000);
    swd_dp_write(REQ_CTRLSTAT_W, CDBGPWRUPREQ | CSYSPWRUPREQ | MASKLANE);
    uint32_t cs = 0;
    for (int i = 0; i < 1000; i++) {
        cs = swd_dp_read(REQ_CTRLSTAT_R, &ack, &par);
        if ((cs & (CDBGPWRUPACK | CSYSPWRUPACK)) == (CDBGPWRUPACK | CSYSPWRUPACK)) break;
        ets_delay_us(100);
    }
    if (ahb_ap_init() != 0) { printf("[FAIL] AHB-AP init failed\n"); return 1; }

    for (int i = 0; i < nwords; i++) {
        uint32_t val;
        if (mem_read32(addr + (uint32_t)(i * 4), &val) != 0) {
            printf("0x%08" PRIX32 ": READ FAIL\n", addr + (uint32_t)(i * 4));
            return 1;
        }
        printf("0x%08" PRIX32 ": 0x%08" PRIX32 "\n", addr + (uint32_t)(i * 4), val);
    }
    return 0;
}

/* ── Command dispatcher ───────────────────────────────────────────────────── */

static int do_swd_speed(int argc, char **argv)
{
    if (argc >= 2) {
        int v = atoi(argv[1]);
        if (v < 0) { printf("half_us must be >= 0\n"); return 1; }
        s_swd_half_us = v;
    }
    int khz = s_swd_half_us > 0 ? 1000 / (2 * s_swd_half_us) : 9999;
    printf("SWD clock half-period = %d µs  (~%d kHz)\n", s_swd_half_us, khz);
    return 0;
}

static int do_swd(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "speed") == 0)
        return do_swd_speed(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "probe") == 0)
        return do_swd_probe(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "uid") == 0)
        return do_swd_uid(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "read") == 0)
        return do_swd_read(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "flash") == 0)
        return do_swd_flash(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "cs") == 0)
        return do_swd_cs(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "gpio") == 0)
        return do_swd_gpio(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "bare") == 0)
        return do_swd_bare(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "sniff") == 0)
        return do_swd_sniff(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "watch") == 0)
        return do_swd_gpio_watch(argc - 1, argv + 1);

    int khz = s_swd_half_us > 0 ? 1000 / (2 * s_swd_half_us) : 9999;
    printf("Usage:\n");
    printf("  swd speed [half_us]  get/set clock half-period µs (now %d = ~%d kHz)\n",
           s_swd_half_us, khz);
    printf("  swd probe [half_us] [nojtagtoswd]\n");
    printf("    nojtagtoswd  skip JTAG-to-SWD sequence\n");
    printf("  swd uid          read STM32 UID96 (24 hex chars) via SWD\n");
    printf("  swd read <addr> [n]  read N words from AHB address (hex)\n");
    printf("  swd flash <url>  download binary from HTTP and flash DUT via SWD\n");
    printf("  swd bare [N]    line-reset only then N repeated IDCODE reads\n");
    printf("  swd gpio        SWDIO line level diagnostic\n");
    printf("  swd sniff [N]   passive bit capture on SWCLK rising edges (default 2000)\n");
    return 1;
}

/* ── DCMD-triggered DUT firmware store ────────────────────────────────────── */

static void dut_fw_store_task(void *arg)
{
    char *url = (char *)arg;

    ESP_LOGI("swd", "DUT FW store: downloading %s", url);
    printf("DUT FW store: downloading %s\n", url);

    fw_dl_t dl = { .cap = SWD_FLASH_MAX_SIZE };
    dl.buf = (uint8_t *)heap_caps_malloc(dl.cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dl.buf) dl.buf = (uint8_t *)malloc(dl.cap);
    if (!dl.buf) {
        ESP_LOGE("swd", "DUT FW store: malloc failed");
        free(url);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t hcfg = {
        .url           = url,
        .event_handler = fw_http_event,
        .user_data     = &dl,
        .timeout_ms    = 30000,
        .buffer_size   = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hcfg);
    esp_err_t err = esp_http_client_perform(client);
    int http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(url);

    if (err != ESP_OK || dl.overflow || dl.len == 0 || http_status != 200) {
        ESP_LOGE("swd", "DUT FW store: download failed %s HTTP %d",
                 esp_err_to_name(err), http_status);
        free(dl.buf);
        vTaskDelete(NULL);
        return;
    }

    const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "dut_fw");
    if (!part) {
        ESP_LOGE("swd", "DUT FW store: dut_fw partition not found");
        free(dl.buf);
        vTaskDelete(NULL);
        return;
    }

    size_t erase_size = sizeof(dut_fw_hdr_t) + dl.len;
    erase_size = (erase_size + 0xFFF) & ~0xFFF;
    esp_err_t e = esp_partition_erase_range(part, 0, erase_size);
    dut_fw_hdr_t hdr = { .magic = DUT_FW_PART_MAGIC, .size = (uint32_t)dl.len };
    if (e == ESP_OK) e = esp_partition_write(part, 0, &hdr, sizeof(hdr));
    if (e == ESP_OK) e = esp_partition_write(part, sizeof(hdr), dl.buf, dl.len);
    free(dl.buf);

    if (e == ESP_OK) {
        ESP_LOGI("swd", "DUT FW store: saved %u bytes to dut_fw partition",
                 (unsigned)dl.len);
        printf("DUT FW store: OK  %u bytes\n", (unsigned)dl.len);
    } else {
        ESP_LOGE("swd", "DUT FW store: write failed: %s", esp_err_to_name(e));
        printf("DUT FW store: FAILED %s\n", esp_err_to_name(e));
    }
    vTaskDelete(NULL);
}

void dut_fw_store_from_url(const char *url)
{
    char *url_copy = strdup(url);
    if (!url_copy) {
        ESP_LOGE("swd", "dut_fw_store_from_url: alloc failed");
        return;
    }
    xTaskCreate(dut_fw_store_task, "dut_fw_store", 8192, url_copy, 5, NULL);
}

void register_swd_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "swd",
        .help    = "SWD bit-bang: swd probe | swd flash <url> | swd bare | swd gpio",
        .hint    = NULL,
        .func    = &do_swd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
