/* Shim: hal_gpio.h has moved to tc-firmware-common (common/hal/hal_gpio.h).
 * This file re-exports it so that existing relative includes ('../hal/hal_gpio.h')
 * continue to work during the migration. Prefer including 'hal_gpio.h' directly
 * via the -Icommon/hal build flag in new code. */
#include "../common/hal/hal_gpio.h"
