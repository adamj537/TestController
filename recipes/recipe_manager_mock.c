/**
 * @file recipe_manager_mock.c
 * @brief Mock recipe manager for unit testing
 *
 * Implements recipe management using mock storage for off-board testing.
 */

#include "recipe_manager.h"
#include "../storage/storage.h"
#include <string.h>
#include <stdlib.h>

/* ==================== Manager State ==================== */

static struct {
    bool initialized;
    Recipe_t current_recipe;
    bool recipe_loaded;
    bool locked;
    char last_recipe_name[RECIPE_NAME_MAX];
} recipe_manager = {false, {}, false, false, ""};

/* ==================== Initialization ==================== */

int RecipeManager_Init(void) {
    if (recipe_manager.initialized) return 0;

    if (Storage_Init() != 0) return -1;

    recipe_manager.initialized = true;
    recipe_manager.recipe_loaded = false;
    recipe_manager.locked = false;

    /* Load last-used recipe */
    RecipeManager_LoadLastUsed();

    return 0;
}

int RecipeManager_Deinit(void) {
    if (!recipe_manager.initialized) return -1;

    /* Save current recipe state */
    if (recipe_manager.recipe_loaded) {
        RecipeManager_SaveAsLastUsed();
    }

    Storage_Deinit();
    recipe_manager.initialized = false;

    return 0;
}

/* ==================== Recipe Discovery ==================== */

int32_t RecipeManager_ListRecipes(const char* product_filter,
                                  const char* variant_filter,
                                  RecipeMetadata_t* recipes,
                                  uint32_t max_recipes) {
    if (!recipe_manager.initialized || !recipes) return -1;

    const char* keys[RECIPE_LIST_MAX];
    int32_t count = Storage_ListKeys(STORAGE_DOMAIN_RECIPES, keys, RECIPE_LIST_MAX);
    if (count < 0) return -1;

    uint32_t result_count = 0;
    for (int32_t i = 0; i < count && result_count < max_recipes; i++) {
        const char* key = keys[i];

        /* Read recipe metadata from storage */
        uint8_t data[512];
        int32_t size = Storage_Read(STORAGE_DOMAIN_RECIPES, key, data, sizeof(data));

        if (size > 0) {
            /* Simple parsing: assume format "name:product:variant:revision" */
            /* In real implementation, would parse JSON */
            RecipeMetadata_t* meta = &recipes[result_count];
            strncpy(meta->name, key, RECIPE_NAME_MAX - 1);
            meta->name[RECIPE_NAME_MAX - 1] = '\0';

            result_count++;
        }
    }

    return result_count;
}

int32_t RecipeManager_GetRecipeCount(const char* product_filter,
                                     const char* variant_filter) {
    if (!recipe_manager.initialized) return -1;

    const char* keys[RECIPE_LIST_MAX];
    return Storage_ListKeys(STORAGE_DOMAIN_RECIPES, keys, RECIPE_LIST_MAX);
}

/* ==================== Recipe Selection ==================== */

int RecipeManager_LoadRecipe(const char* recipe_name) {
    if (!recipe_manager.initialized || !recipe_name) return -1;
    if (recipe_manager.locked) return -1;  /* Cannot change while test running */

    /* Read recipe from storage */
    uint8_t data[4096];
    int32_t size = Storage_Read(STORAGE_DOMAIN_RECIPES, recipe_name, data, sizeof(data));

    if (size <= 0) return -1;  /* Recipe not found */

    /* Simple mock: copy name and mark as loaded */
    memset(&recipe_manager.current_recipe, 0, sizeof(Recipe_t));
    strncpy(recipe_manager.current_recipe.name, recipe_name, RECIPE_NAME_MAX - 1);
    recipe_manager.recipe_loaded = true;

    return 0;
}

const Recipe_t* RecipeManager_GetCurrent(void) {
    if (!recipe_manager.initialized || !recipe_manager.recipe_loaded) {
        return NULL;
    }
    return &recipe_manager.current_recipe;
}

const char* RecipeManager_GetCurrentName(void) {
    if (!recipe_manager.initialized || !recipe_manager.recipe_loaded) {
        return "";
    }
    return recipe_manager.current_recipe.name;
}

int RecipeManager_GetCurrentMetadata(RecipeMetadata_t* metadata) {
    if (!recipe_manager.initialized || !recipe_manager.recipe_loaded || !metadata) {
        return -1;
    }

    strncpy(metadata->name, recipe_manager.current_recipe.name, RECIPE_NAME_MAX - 1);
    strncpy(metadata->product, recipe_manager.current_recipe.product, RECIPE_PRODUCT_MAX - 1);
    strncpy(metadata->variant, recipe_manager.current_recipe.variant, RECIPE_VARIANT_MAX - 1);
    metadata->revision = recipe_manager.current_recipe.revision;
    metadata->last_modified = recipe_manager.current_recipe.last_modified_timestamp;

    return 0;
}

/* ==================== Recipe Persistence ==================== */

int RecipeManager_SaveRecipe(const Recipe_t* recipe, bool backup_to_sd) {
    if (!recipe_manager.initialized || !recipe) return -1;

    /* In real implementation, would serialize to JSON */
    /* For mock, just store the recipe name */
    return Storage_WriteString(STORAGE_DOMAIN_RECIPES, recipe->name,
                              recipe->name, backup_to_sd);
}

int RecipeManager_UpdateRecipe(const Recipe_t* recipe) {
    if (!recipe_manager.initialized || !recipe) return -1;

    /* Check if recipe exists */
    if (!Storage_Exists(STORAGE_DOMAIN_RECIPES, recipe->name)) {
        return -1;
    }

    return RecipeManager_SaveRecipe(recipe, false);
}

int RecipeManager_DeleteRecipe(const char* recipe_name) {
    if (!recipe_manager.initialized || !recipe_name) return -1;
    if (recipe_manager.locked) return -1;

    /* Cannot delete current recipe */
    if (recipe_manager.recipe_loaded &&
        strcmp(recipe_manager.current_recipe.name, recipe_name) == 0) {
        return -1;
    }

    return Storage_Delete(STORAGE_DOMAIN_RECIPES, recipe_name);
}

/* ==================== Multi-Channel Coordination ==================== */

int RecipeManager_LoadRecipeMultiChannel(const char* recipe_name) {
    if (!recipe_manager.initialized || !recipe_name) return -1;

    /* In real implementation, would publish via MQTT to all TCs */
    /* For mock, just load locally */
    return RecipeManager_LoadRecipe(recipe_name);
}

int RecipeManager_SyncCurrentRecipe(void) {
    if (!recipe_manager.initialized) return -1;

    /* In real implementation, would sync via MQTT */
    return 0;
}

/* ==================== Locking ==================== */

int RecipeManager_Lock(void) {
    if (!recipe_manager.initialized) return -1;
    if (recipe_manager.locked) return -1;

    recipe_manager.locked = true;
    return 0;
}

int RecipeManager_Unlock(void) {
    if (!recipe_manager.initialized) return -1;

    recipe_manager.locked = false;
    return 0;
}

int RecipeManager_IsLocked(void) {
    if (!recipe_manager.initialized) return -1;
    return recipe_manager.locked ? 1 : 0;
}

/* ==================== Startup/Shutdown ==================== */

int RecipeManager_LoadLastUsed(void) {
    if (!recipe_manager.initialized) return -1;

    char last_recipe[RECIPE_NAME_MAX];
    int32_t size = Storage_ReadString(STORAGE_DOMAIN_CONFIGURATION,
                                      "_last_recipe", last_recipe,
                                      RECIPE_NAME_MAX);

    if (size <= 0) {
        return 1;  /* No previous recipe */
    }

    if (RecipeManager_LoadRecipe(last_recipe) == 0) {
        return 0;  /* Successfully loaded */
    }

    return 1;  /* Failed to load, but not an error */
}

int RecipeManager_SaveAsLastUsed(void) {
    if (!recipe_manager.initialized || !recipe_manager.recipe_loaded) {
        return -1;
    }

    return Storage_WriteString(STORAGE_DOMAIN_CONFIGURATION,
                              "_last_recipe",
                              recipe_manager.current_recipe.name, false);
}

/* ==================== Mock Test Helpers ==================== */

/**
 * @brief Create a test recipe for unit testing
 */
Recipe_t* RecipeManager_Mock_CreateTestRecipe(const char* name,
                                              const char* product) {
    Recipe_t* recipe = malloc(sizeof(Recipe_t));
    if (!recipe) return NULL;

    memset(recipe, 0, sizeof(Recipe_t));
    strncpy(recipe->name, name, RECIPE_NAME_MAX - 1);
    strncpy(recipe->product, product, RECIPE_PRODUCT_MAX - 1);
    recipe->revision = 1;

    return recipe;
}

/**
 * @brief Reset recipe manager state (for test isolation)
 */
void RecipeManager_Mock_Reset(void) {
    memset(&recipe_manager, 0, sizeof(recipe_manager));
}

/**
 * @brief Get manager state for verification
 */
bool RecipeManager_Mock_IsRecipeLoaded(void) {
    return recipe_manager.recipe_loaded;
}

/**
 * @brief Get current recipe name for assertion
 */
const char* RecipeManager_Mock_GetCurrentName(void) {
    if (!recipe_manager.recipe_loaded) return "";
    return recipe_manager.current_recipe.name;
}
