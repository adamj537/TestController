#pragma once

/* cmd_flash.h — Raw flash read CLI command.
 *
 * Provides `flash read <offset_hex> [size]` for inspecting partition
 * contents without esptool (which causes hard resets via RTS). */

void register_flash_commands(void);
