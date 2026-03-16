/* Shim: hal_system.h has moved to tc-firmware-common (common/hal/hal_system.h).
 * This file re-exports it so that existing relative includes ('../hal/hal_system.h')
 * continue to work during the migration. Prefer including 'hal_system.h' directly
 * via the -Icommon/hal build flag in new code. */
#include "../common/hal/hal_system.h"
