#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "driver/gpio.h"
#include "cmd_gpio.h"

static int do_gpio_set(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: gpio set <gpio_num> <0|1>\n");
        return 1;
    }
    int pin = atoi(argv[1]);
    int level = atoi(argv[2]);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        printf("gpio_config failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    gpio_set_level(pin, level ? 1 : 0);
    printf("GPIO%d -> %d\n", pin, level ? 1 : 0);
    return 0;
}

static int do_gpio_get(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: gpio get <gpio_num>\n");
        return 1;
    }
    int pin = atoi(argv[1]);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        printf("gpio_config failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("GPIO%d = %d\n", pin, gpio_get_level(pin));
    return 0;
}

static int do_gpio_mode(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: gpio mode <gpio_num> <in|out|od>\n");
        return 1;
    }
    int pin = atoi(argv[1]);
    gpio_mode_t mode;
    if (strcmp(argv[2], "in") == 0)       mode = GPIO_MODE_INPUT;
    else if (strcmp(argv[2], "out") == 0) mode = GPIO_MODE_OUTPUT;
    else if (strcmp(argv[2], "od") == 0)  mode = GPIO_MODE_OUTPUT_OD;
    else {
        printf("Unknown mode '%s'. Use: in, out, od\n", argv[2]);
        return 1;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        printf("gpio_config failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("GPIO%d mode -> %s\n", pin, argv[2]);
    return 0;
}

static int do_gpio(int argc, char **argv)
{
    if (argc < 2) {
        printf("GPIO control commands:\n");
        printf("  gpio set <num> <0|1>        drive output high/low\n");
        printf("  gpio get <num>              read input level\n");
        printf("  gpio mode <num> <in|out|od> set direction\n");
        return 1;
    }
    if (strcmp(argv[1], "set") == 0)  return do_gpio_set(argc - 1, argv + 1);
    if (strcmp(argv[1], "get") == 0)  return do_gpio_get(argc - 1, argv + 1);
    if (strcmp(argv[1], "mode") == 0) return do_gpio_mode(argc - 1, argv + 1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_gpio_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "gpio",
        .help = "GPIO control: gpio <set|get|mode> ...",
        .hint = NULL,
        .func = &do_gpio,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
