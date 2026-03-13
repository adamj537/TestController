/**
 * @file recipe_manager.h
 * @brief Recipe management for test sequence selection and execution
 *
 * Provides operators with ability to:
 * - List available recipes
 * - Select current recipe
 * - Load/save/update recipes from storage
 * - Coordinate recipes across multiple test controllers
 * - Load last recipe on startup
 */

#ifndef RECIPE_MANAGER_H
#define RECIPE_MANAGER_H

#include "recipe.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define RECIPE_LIST_MAX 20  /* Maximum recipes that can be listed */

/* ==================== Initialization ==================== */

/**
 * @brief Initialize recipe manager
 *
 * Loads recipe list from storage and restores last-used recipe as current.
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_Init(void);

/**
 * @brief Deinitialize recipe manager
 *
 * Saves current recipe state to persistent storage.
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_Deinit(void);

/* ==================== Recipe Discovery ==================== */

/**
 * @brief Get list of available recipes
 *
 * Retrieves metadata for all recipes matching filter criteria.
 *
 * @param product_filter Product name to filter (NULL for all)
 * @param variant_filter Variant to filter (NULL for all)
 * @param recipes Array to receive recipe metadata
 * @param max_recipes Maximum recipes to return
 *
 * @return Number of recipes found, -1 on error
 */
int32_t RecipeManager_ListRecipes(const char* product_filter,
                                  const char* variant_filter,
                                  RecipeMetadata_t* recipes,
                                  uint32_t max_recipes);

/**
 * @brief Get count of available recipes
 *
 * @param product_filter Product name to filter (NULL for all)
 * @param variant_filter Variant to filter (NULL for all)
 *
 * @return Number of matching recipes, -1 on error
 */
int32_t RecipeManager_GetRecipeCount(const char* product_filter,
                                     const char* variant_filter);

/* ==================== Recipe Selection ==================== */

/**
 * @brief Load recipe by name
 *
 * Loads recipe into memory as current recipe.
 * Can only change recipe when no test is running.
 *
 * @param recipe_name Recipe name to load
 *
 * @return 0 on success, -1 if not found or locked
 */
int RecipeManager_LoadRecipe(const char* recipe_name);

/**
 * @brief Get current recipe
 *
 * Returns pointer to currently loaded recipe.
 *
 * @return Pointer to Recipe_t, or NULL if none loaded
 */
const Recipe_t* RecipeManager_GetCurrent(void);

/**
 * @brief Get current recipe name
 *
 * Useful for display/status.
 *
 * @return Recipe name, or empty string if none loaded
 */
const char* RecipeManager_GetCurrentName(void);

/**
 * @brief Get current recipe metadata
 *
 * Returns metadata without loading full recipe.
 *
 * @param metadata Pointer to metadata structure
 *
 * @return 0 on success, -1 if none loaded
 */
int RecipeManager_GetCurrentMetadata(RecipeMetadata_t* metadata);

/* ==================== Recipe Persistence ==================== */

/**
 * @brief Save recipe to persistent storage
 *
 * Writes recipe as JSON to storage with optional SD backup.
 * Can update existing recipe or create new one.
 *
 * @param recipe Recipe to save
 * @param backup_to_sd If true, also backup to SD card
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_SaveRecipe(const Recipe_t* recipe, bool backup_to_sd);

/**
 * @brief Update recipe in storage
 *
 * Updates existing recipe revision and timestamp.
 *
 * @param recipe Updated recipe
 *
 * @return 0 on success, -1 if recipe not found
 */
int RecipeManager_UpdateRecipe(const Recipe_t* recipe);

/**
 * @brief Delete recipe from storage
 *
 * Cannot delete currently loaded recipe.
 *
 * @param recipe_name Recipe to delete
 *
 * @return 0 on success, -1 on error or locked
 */
int RecipeManager_DeleteRecipe(const char* recipe_name);

/* ==================== Multi-Channel Coordination ==================== */

/**
 * @brief Load recipe on all test controllers
 *
 * Publishes recipe via MQTT to all TCs, ensuring all channels
 * run the same recipe (operator selects once, all use it).
 *
 * @param recipe_name Recipe to load on all TCs
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_LoadRecipeMultiChannel(const char* recipe_name);

/**
 * @brief Sync current recipe across channels
 *
 * Ensures all TCs know about the current recipe.
 * Called during startup for consistency.
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_SyncCurrentRecipe(void);

/* ==================== Locking (Test Runtime) ==================== */

/**
 * @brief Lock recipe while test is running
 *
 * Prevents operator from changing recipe during test execution.
 *
 * @return 0 on success, -1 if already locked
 */
int RecipeManager_Lock(void);

/**
 * @brief Unlock recipe when test completes
 *
 * Allows operator to select new recipe.
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_Unlock(void);

/**
 * @brief Check if recipe is locked
 *
 * @return 1 if locked, 0 if unlocked, -1 on error
 */
int RecipeManager_IsLocked(void);

/* ==================== Startup/Shutdown ==================== */

/**
 * @brief Load last-used recipe on startup
 *
 * Called during initialization to restore previous recipe context.
 *
 * @return 0 on success (recipe loaded), 1 if none previously used, -1 on error
 */
int RecipeManager_LoadLastUsed(void);

/**
 * @brief Save current recipe as last-used
 *
 * Called during shutdown to restore on next startup.
 *
 * @return 0 on success, -1 on error
 */
int RecipeManager_SaveAsLastUsed(void);

#ifdef __cplusplus
}
#endif

#endif /* RECIPE_MANAGER_H */
