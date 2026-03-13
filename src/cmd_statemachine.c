#include "cmd_statemachine.h"
#include "tc_statemachine.h"

#include "esp_console.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cmd_sm";

static int do_sm(int argc, char **argv)
{
    if (argc < 2) {
        printf("state: %s\n", tc_sm_state_str());
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0) {
        printf("state: %s\n", tc_sm_state_str());
        return 0;
    }

    if (strcmp(sub, "start") == 0) {
        ESP_LOGI(TAG, "manual start");
        tc_sm_cmd_start();
        return 0;
    }

    if (strcmp(sub, "abort") == 0) {
        ESP_LOGI(TAG, "manual abort");
        tc_sm_cmd_abort();
        return 0;
    }

    printf("usage: sm <status|start|abort>\n");
    return 1;
}

void register_statemachine_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "sm",
        .help    = "State machine: sm <status|start|abort>",
        .hint    = NULL,
        .func    = &do_sm,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
