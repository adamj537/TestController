#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "cmd_i2c.h"

static i2c_master_bus_handle_t s_bus = NULL;
static int s_sda = 15;      /* TCC carrier: SDA on GPIO15 */
static int s_scl = 16;      /* TCC carrier: SCL on GPIO16 */
static int s_speed = 400000; /* default 400kHz; override via i2c init [sda] [scl] [hz] */

static int do_i2c_init(int argc, char **argv)
{
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    if (argc >= 3) {
        s_sda = atoi(argv[1]);
        s_scl = atoi(argv[2]);
    }
    if (argc >= 4) {
        s_speed = atoi(argv[3]);
    }

    /* Reset pins to default state before handing to I2C driver.
     * gpio_config() (used by the gpio shell commands) modifies the IO_MUX
     * routing and can leave it in a state that i2c_new_master_bus() doesn't
     * fully undo, causing the I2C peripheral to lose contact with the pad.
     * gpio_reset_pin() clears all peripheral routing and restores defaults. */
    gpio_reset_pin((gpio_num_t)s_sda);
    gpio_reset_pin((gpio_num_t)s_scl);

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)s_sda,
        .scl_io_num = (gpio_num_t)s_scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        printf("I2C init failed: %s\n", esp_err_to_name(err));
        s_bus = NULL;
        return 1;
    }
    printf("I2C ready: SDA=GPIO%d  SCL=GPIO%d  %dkHz\n", s_sda, s_scl, s_speed / 1000);
    return 0;
}

static int do_i2c_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_bus == NULL) {
        printf("I2C not initialized. Run: i2c init [sda_gpio] [scl_gpio]\n");
        return 1;
    }

    printf("Scanning I2C (SDA=GPIO%d SCL=GPIO%d)...\n", s_sda, s_scl);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    int found = 0;
    for (int addr = 0; addr < 128; addr++) {
        if (addr % 16 == 0) printf("%02x: ", addr);
        if (i2c_master_probe(s_bus, (uint16_t)addr, 20) == ESP_OK) {
            printf("%02x ", addr);
            found++;
        } else {
            printf("-- ");
        }
        if (addr % 16 == 15) printf("\n");
    }
    printf("\n%d device(s) found\n", found);
    return 0;
}

static int do_i2c_read(int argc, char **argv)
{
    if (s_bus == NULL) { printf("I2C not initialized.\n"); return 1; }
    if (argc < 4) {
        printf("Usage: i2c read <addr_hex> <reg_hex> <nbytes>\n");
        return 1;
    }

    uint16_t addr  = (uint16_t)strtol(argv[1], NULL, 16);
    uint8_t  reg   = (uint8_t)strtol(argv[2], NULL, 16);
    int      nbytes = atoi(argv[3]);

    if (nbytes < 1 || nbytes > 32) { printf("nbytes must be 1-32\n"); return 1; }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = (uint32_t)s_speed,
    };
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        printf("Add device 0x%02x failed: %s\n", addr, esp_err_to_name(err));
        return 1;
    }

    uint8_t buf[32];
    err = i2c_master_transmit_receive(dev, &reg, 1, buf, (size_t)nbytes, 200);
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) {
        printf("Read failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("0x%02x [reg 0x%02x, %d byte%s]: ", addr, reg, nbytes, nbytes > 1 ? "s" : "");
    for (int i = 0; i < nbytes; i++) printf("%02x ", buf[i]);
    printf("\n");
    return 0;
}

static int do_i2c_write(int argc, char **argv)
{
    if (s_bus == NULL) { printf("I2C not initialized.\n"); return 1; }
    if (argc < 4) {
        printf("Usage: i2c write <addr_hex> <reg_hex> <byte_hex> [byte_hex...]\n");
        return 1;
    }

    uint16_t addr  = (uint16_t)strtol(argv[1], NULL, 16);
    uint8_t  reg   = (uint8_t)strtol(argv[2], NULL, 16);
    int      ndata = argc - 3;
    if (ndata > 31) { printf("Too many bytes (max 31)\n"); return 1; }

    uint8_t buf[32];
    buf[0] = reg;
    for (int i = 0; i < ndata; i++) {
        buf[i + 1] = (uint8_t)strtol(argv[3 + i], NULL, 16);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = (uint32_t)s_speed,
    };
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        printf("Add device 0x%02x failed: %s\n", addr, esp_err_to_name(err));
        return 1;
    }

    err = i2c_master_transmit(dev, buf, (size_t)(ndata + 1), 200);
    i2c_master_bus_rm_device(dev);

    if (err != ESP_OK) {
        printf("Write failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("0x%02x [reg 0x%02x] <- ", addr, reg);
    for (int i = 0; i < ndata; i++) printf("%02x ", buf[i + 1]);
    printf("OK\n");
    return 0;
}

static int do_i2c(int argc, char **argv)
{
    if (argc < 2) {
        printf("I2C master commands:\n");
        printf("  i2c init [sda] [scl] [hz]         init bus (default GPIO15/16 400000Hz)\n");
        printf("  i2c scan                          scan 0x00-0x7f\n");
        printf("  i2c read  <addr> <reg> <n>        read N bytes from register\n");
        printf("  i2c write <addr> <reg> <b0> ...   write bytes to register\n");
        printf("  addr/reg/bytes in hex, e.g.: i2c read 40 00 2\n");
        return 1;
    }
    if (strcmp(argv[1], "init") == 0)  return do_i2c_init(argc - 1, argv + 1);
    if (strcmp(argv[1], "scan") == 0)  return do_i2c_scan(argc - 1, argv + 1);
    if (strcmp(argv[1], "read") == 0)  return do_i2c_read(argc - 1, argv + 1);
    if (strcmp(argv[1], "write") == 0) return do_i2c_write(argc - 1, argv + 1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_i2c_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "i2c",
        .help = "I2C master: i2c <init|scan|read|write>",
        .hint = NULL,
        .func = &do_i2c,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
