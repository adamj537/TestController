#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "tc_mqtt.h"
#include "cmd_mqtt.h"

/* mqtt status
 * mqtt config <broker_url> <serial> [channel]
 *
 * Examples:
 *   mqtt config mqtt://10.0.0.1:1883 G3-MB-Tester-001
 *   mqtt config mqtt://10.0.0.1:1883 G3-MB-Tester-001 0
 *   mqtt status
 */

static int do_mqtt(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mqtt <status|config>\n");
        printf("  mqtt status\n");
        printf("  mqtt config <broker_url> <serial> [channel]\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        const char *url = tc_mqtt_broker_url();
        printf("broker : %s\n", url[0] ? url : "(not configured)");
        printf("serial : %s\n", tc_mqtt_serial());
        printf("channel: %u\n", (unsigned)tc_mqtt_channel());
        printf("state  : %s\n", tc_mqtt_connected() ? "connected" : "disconnected");
        return 0;
    }

    if (strcmp(argv[1], "config") == 0) {
        if (argc < 4) {
            printf("Usage: mqtt config <broker_url> <serial> [channel]\n");
            printf("  broker_url: e.g. mqtt://10.0.0.1:1883\n");
            printf("  serial:     e.g. G3-MB-Tester-001\n");
            printf("  channel:    0-7, default 0\n");
            return 1;
        }

        uint8_t ch = 0;
        if (argc >= 5) {
            int v = atoi(argv[4]);
            if (v < 0 || v > 7) {
                printf("ERR: channel must be 0–7\n");
                return 1;
            }
            ch = (uint8_t)v;
        }

        tc_mqtt_configure(argv[2], argv[3], ch);
        printf("OK  broker=%s  serial=%s  channel=%u\n",
               argv[2], argv[3], (unsigned)ch);
        printf("Connecting...\n");
        return 0;
    }

    printf("ERR: unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_mqtt_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "mqtt",
        .help    = "MQTT client: mqtt <status|config>",
        .hint    = NULL,
        .func    = &do_mqtt,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
