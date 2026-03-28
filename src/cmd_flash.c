/* cmd_flash.c — Raw flash read CLI command.
 *
 * `flash read <offset_hex> [size]` — hex-dump raw flash bytes.
 * Max 256 bytes per call to keep console output manageable.
 * `flash partitions` — list partition table.
 */

#include "cmd_flash.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_console.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_READ_MAX  256

static void hex_dump(const uint8_t *buf, size_t len, uint32_t base_addr)
{
    for (size_t i = 0; i < len; i += 16) {
        printf("  %08lx: ", (unsigned long)(base_addr + i));
        size_t row = (len - i < 16) ? (len - i) : 16;
        for (size_t j = 0; j < row; j++)
            printf("%02x ", buf[i + j]);
        for (size_t j = row; j < 16; j++)
            printf("   ");
        printf(" ");
        for (size_t j = 0; j < row; j++) {
            uint8_t c = buf[i + j];
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("\n");
    }
}

static int do_flash_read(uint32_t offset, size_t size)
{
    if (size > FLASH_READ_MAX) {
        printf("Max read size is %u bytes\n", FLASH_READ_MAX);
        return 1;
    }

    uint8_t buf[FLASH_READ_MAX];
    esp_err_t err = esp_flash_read(NULL, buf, offset, size);
    if (err != ESP_OK) {
        printf("flash read failed at 0x%08lx: %s\n",
               (unsigned long)offset, esp_err_to_name(err));
        return 1;
    }

    printf("flash read 0x%08lx  %u bytes:\n", (unsigned long)offset, (unsigned)size);
    hex_dump(buf, size, offset);
    return 0;
}

static int do_flash_partitions(void)
{
    printf("%-12s  %-8s  %-10s  %-10s\n", "Label", "Type", "Offset", "Size");
    printf("%-12s  %-8s  %-10s  %-10s\n", "------------", "--------",
           "----------", "----------");

    esp_partition_iterator_t it = esp_partition_find(
            ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        const char *type_str;
        switch (p->type) {
            case ESP_PARTITION_TYPE_APP:  type_str = "app";  break;
            case ESP_PARTITION_TYPE_DATA: type_str = "data"; break;
            default:                     type_str = "?";     break;
        }
        printf("%-12s  %-8s  0x%08lx  0x%08lx  (%lu KB)\n",
               p->label, type_str,
               (unsigned long)p->address,
               (unsigned long)p->size,
               (unsigned long)(p->size / 1024));
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    return 0;
}

static int do_flash(int argc, char **argv)
{
    if (argc < 2) goto usage;

    if (strcmp(argv[1], "partitions") == 0) {
        return do_flash_partitions();
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) goto usage;
        uint32_t offset = (uint32_t)strtoul(argv[2], NULL, 0);
        size_t size = (argc >= 4) ? (size_t)strtoul(argv[3], NULL, 0) : 32;
        return do_flash_read(offset, size);
    }

usage:
    printf("Usage:\n"
           "  flash read <offset_hex> [size]  hex-dump flash (max %u bytes)\n"
           "  flash partitions                list partition table\n",
           FLASH_READ_MAX);
    return 1;
}

void register_flash_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "flash",
        .help    = "flash read <offset> [size] | flash partitions",
        .hint    = NULL,
        .func    = &do_flash,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
