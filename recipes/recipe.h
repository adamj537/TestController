/**
 * @file recipe.h
 * @brief Recipe data structures for test sequences
 *
 * Defines JSON-compatible recipe format for operator-selectable tests.
 * Supports multi-product, multi-variant recipes with shared configuration.
 */

#ifndef RECIPE_H
#define RECIPE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RECIPE_NAME_MAX         64
#define RECIPE_DESCRIPTION_MAX  256
#define RECIPE_PRODUCT_MAX      32
#define RECIPE_VARIANT_MAX      32
#define RECIPE_VERSION_MAX      16
#define RECIPE_STEPS_MAX        50  /* Maximum test steps per recipe */

/* ==================== Recipe Step Types ==================== */

typedef enum {
    RECIPE_STEP_SET_VOLTAGE,      /* Set DUT supply voltage */
    RECIPE_STEP_MEASURE_VOLTAGE,  /* Measure DUT output voltage */
    RECIPE_STEP_MEASURE_CURRENT,  /* Measure DUT supply current */
    RECIPE_STEP_SET_STIMULUS,     /* Set test signal (DAC) */
    RECIPE_STEP_MEASURE_RESPONSE, /* Measure DUT response to signal */
    RECIPE_STEP_SELECT_BRANCH,    /* Select branch (1-4) */
    RECIPE_STEP_POWER_RAIL,       /* Enable/disable power rail */
    RECIPE_STEP_DUT_COMMAND,      /* Send command to DUT via UART */
    RECIPE_STEP_DELAY,            /* Wait/pause */
    RECIPE_STEP_CHECK_VALUE       /* Validate measured value against range */
} RecipeStepType_t;

typedef enum {
    RECIPE_POWER_RAIL_VIN_1,
    RECIPE_POWER_RAIL_VIN_2,
    RECIPE_POWER_RAIL_3V,
    RECIPE_POWER_RAIL_5V
} RecipePowerRail_t;

typedef enum {
    RECIPE_POWER_ENABLE,
    RECIPE_POWER_DISABLE
} RecipePowerAction_t;

/* ==================== Recipe Step Definition ==================== */

typedef struct {
    RecipeStepType_t type;
    uint32_t step_id;           /* Unique step ID for cross-reference */
    const char* description;     /* Human-readable step description */

    /* Parameters (type-dependent) */
    union {
        struct {
            uint16_t voltage_mv;  /* Voltage in millivolts */
        } set_voltage;

        struct {
            uint16_t min_mv;
            uint16_t max_mv;
        } measure_voltage;

        struct {
            uint16_t min_ma;
            uint16_t max_ma;
        } measure_current;

        struct {
            uint16_t signal_mv;
        } set_stimulus;

        struct {
            uint16_t min_mv;
            uint16_t max_mv;
        } measure_response;

        struct {
            uint8_t branch_num;   /* 1-4 */
        } select_branch;

        struct {
            RecipePowerRail_t rail;
            RecipePowerAction_t action;
        } power_rail;

        struct {
            const char* command;  /* DUT UART command */
            uint16_t timeout_ms;
        } dut_command;

        struct {
            uint32_t delay_ms;
        } delay;

        struct {
            uint32_t prev_step_id;  /* Reference to previous measurement */
            uint16_t min_value;
            uint16_t max_value;
        } check_value;
    } params;

} RecipeStep_t;

/* ==================== Recipe Definition ==================== */

typedef struct {
    /* Metadata */
    char name[RECIPE_NAME_MAX];
    char description[RECIPE_DESCRIPTION_MAX];
    char version[RECIPE_VERSION_MAX];
    char product[RECIPE_PRODUCT_MAX];     /* Product being tested */
    char variant[RECIPE_VARIANT_MAX];     /* Product variant/SKU */

    /* Recipe properties */
    uint32_t duration_ms;   /* Expected runtime in milliseconds */
    bool requires_dut_uart; /* If true, DUT firmware interaction needed */
    uint8_t step_count;
    RecipeStep_t steps[RECIPE_STEPS_MAX];

    /* Tracking */
    uint64_t created_timestamp;
    uint64_t last_modified_timestamp;
    char created_by[32];
    uint32_t revision;

} Recipe_t;

/* ==================== Recipe Metadata ==================== */

typedef struct {
    char name[RECIPE_NAME_MAX];
    char product[RECIPE_PRODUCT_MAX];
    char variant[RECIPE_VARIANT_MAX];
    uint32_t revision;
    uint64_t last_modified;
} RecipeMetadata_t;

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_H */
