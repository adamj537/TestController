/* Shim: hal_dac.h has moved to tc-firmware-common (common/hal/hal_dac.h).
 * This file re-exports it so that existing relative includes ('../hal/hal_dac.h')
 * continue to work during the migration. Prefer including 'hal_dac.h' directly
 * via the -Icommon/hal build flag in new code. */
#include "../common/hal/hal_dac.h"
