/* Shim: hal_adc.h has moved to tc-firmware-common (common/hal/hal_adc.h).
 * This file re-exports it so that existing relative includes ('../hal/hal_adc.h')
 * continue to work during the migration. Prefer including 'hal_adc.h' directly
 * via the -Icommon/hal build flag in new code. */
#include "../common/hal/hal_adc.h"
