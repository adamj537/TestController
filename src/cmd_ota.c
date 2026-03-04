#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "cmd_ota.h"

static const char *TAG = "ota";

static int do_ota_update(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: ota update <url>\n");
        printf("  Supports http:// and https:// URLs\n");
        printf("  Example (local): ota update http://192.168.1.100:8080/firmware.bin\n");
        return 1;
    }

    const char *url = argv[1];
    printf("OTA: fetching %s\n", url);

    esp_http_client_config_t http_cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* Mozilla CA bundle for HTTPS */
        .timeout_ms        = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    ESP_LOGI(TAG, "Starting OTA from: %s", url);
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err != ESP_OK) {
        printf("OTA failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("OTA complete — rebooting in 1 second\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return 0;   /* unreachable */
}

static int do_ota_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t  *desc    = esp_app_get_description();

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);

    printf("Partition : %s\n", running->label);
    printf("Version   : %s\n", desc->version);
    printf("Build     : %s %s\n", desc->date, desc->time);
    printf("IDF ver   : %s\n", desc->idf_ver);

    const char *state_str = "unknown";
    switch (state) {
        case ESP_OTA_IMG_NEW:             state_str = "new";              break;
        case ESP_OTA_IMG_PENDING_VERIFY:  state_str = "pending_verify";  break;
        case ESP_OTA_IMG_VALID:           state_str = "valid";           break;
        case ESP_OTA_IMG_INVALID:         state_str = "invalid";         break;
        case ESP_OTA_IMG_ABORTED:         state_str = "aborted";         break;
        case ESP_OTA_IMG_UNDEFINED:       state_str = "undefined";       break;
    }
    printf("OTA state : %s\n", state_str);
    return 0;
}

static int do_ota(int argc, char **argv)
{
    if (argc < 2) {
        printf("OTA commands:\n");
        printf("  ota update <url>   download firmware from URL and flash\n");
        printf("  ota status         show running partition and version info\n");
        return 1;
    }
    if (strcmp(argv[1], "update") == 0) return do_ota_update(argc-1, argv+1);
    if (strcmp(argv[1], "status") == 0) return do_ota_status(argc-1, argv+1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_ota_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ota",
        .help    = "OTA update: ota <update|status>",
        .hint    = NULL,
        .func    = &do_ota,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
