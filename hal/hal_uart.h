/* Shim: hal_uart.h has moved to tc-firmware-common (common/hal/hal_uart.h).
 * This file re-exports it so that existing relative includes ('../hal/hal_uart.h')
 * continue to work during the migration. Prefer including 'hal_uart.h' directly
 * via the -Icommon/hal build flag in new code. */
#include "../common/hal/hal_uart.h"
