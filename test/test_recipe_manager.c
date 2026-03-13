/**
 * @file test_recipe_manager.c
 * @brief Unit tests for Recipe Manager and Storage (using mocks)
 *
 * Tests recipe management, persistence, and multi-channel coordination.
 */

#include "unity.h"
#include "../recipes/recipe_manager.h"
#include "../recipes/recipe.h"
#include "../storage/storage.h"
#include "../storage/storage_mock.c"
#include "../recipes/recipe_manager_mock.c"

/* ==================== Setup / Teardown ==================== */

void setUp(void) {
    Storage_Mock_Reset();
    RecipeManager_Mock_Reset();
    Storage_Init();
    RecipeManager_Init();
}

void tearDown(void) {
    RecipeManager_Deinit();
    Storage_Mock_Reset();
    RecipeManager_Mock_Reset();
}

/* ==================== Storage Tests ==================== */

void test_storage_write_and_read(void) {
    const char* test_data = "Hello, Storage!";

    int result = Storage_WriteString(STORAGE_DOMAIN_RECIPES, "test_key", test_data, false);
    TEST_ASSERT_EQUAL(0, result);

    char buffer[256];
    int32_t size = Storage_ReadString(STORAGE_DOMAIN_RECIPES, "test_key", buffer, sizeof(buffer));
    TEST_ASSERT_GREATER_THAN(0, size);
    TEST_ASSERT_EQUAL_STRING(test_data, buffer);
}

void test_storage_read_nonexistent_key(void) {
    char buffer[256];
    int32_t size = Storage_ReadString(STORAGE_DOMAIN_RECIPES, "nonexistent", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(0, size);
}

void test_storage_delete(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "test_key", "data", false);
    int result = Storage_Delete(STORAGE_DOMAIN_RECIPES, "test_key");
    TEST_ASSERT_EQUAL(0, result);

    /* Verify it's gone */
    int exists = Storage_Exists(STORAGE_DOMAIN_RECIPES, "test_key");
    TEST_ASSERT_EQUAL(0, exists);
}

void test_storage_update(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "test_key", "old_data", false);
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "test_key", "new_data", false);

    char buffer[256];
    Storage_ReadString(STORAGE_DOMAIN_RECIPES, "test_key", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("new_data", buffer);
}

void test_storage_multiple_domains(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe1", "recipe_data", false);
    Storage_WriteString(STORAGE_DOMAIN_CONFIGURATION, "config1", "config_data", false);

    char buffer[256];
    Storage_ReadString(STORAGE_DOMAIN_RECIPES, "recipe1", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("recipe_data", buffer);

    Storage_ReadString(STORAGE_DOMAIN_CONFIGURATION, "config1", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("config_data", buffer);
}

void test_storage_list_keys(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe1", "data1", false);
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe2", "data2", false);
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe3", "data3", false);

    const char* keys[10];
    int32_t count = Storage_ListKeys(STORAGE_DOMAIN_RECIPES, keys, 10);
    TEST_ASSERT_EQUAL(3, count);
}

void test_storage_clear_domain(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe1", "data1", false);
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "recipe2", "data2", false);
    Storage_ClearDomain(STORAGE_DOMAIN_RECIPES);

    const char* keys[10];
    int32_t count = Storage_ListKeys(STORAGE_DOMAIN_RECIPES, keys, 10);
    TEST_ASSERT_EQUAL(0, count);
}

void test_storage_size_check(void) {
    Storage_WriteString(STORAGE_DOMAIN_RECIPES, "test_key", "test_data", false);
    int32_t size = Storage_GetSize(STORAGE_DOMAIN_RECIPES, "test_key");
    TEST_ASSERT_EQUAL(10, size);  /* strlen("test_data") + 1 for NUL */
}

/* ==================== Recipe Manager Tests ==================== */

void test_recipe_manager_save_and_load(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "test_recipe", RECIPE_NAME_MAX - 1);
    strncpy(recipe.product, "G3-Board", RECIPE_PRODUCT_MAX - 1);
    recipe.revision = 1;

    int result = RecipeManager_SaveRecipe(&recipe, false);
    TEST_ASSERT_EQUAL(0, result);

    result = RecipeManager_LoadRecipe("test_recipe");
    TEST_ASSERT_EQUAL(0, result);

    const char* current_name = RecipeManager_GetCurrentName();
    TEST_ASSERT_EQUAL_STRING("test_recipe", current_name);
}

void test_recipe_manager_load_nonexistent(void) {
    int result = RecipeManager_LoadRecipe("nonexistent_recipe");
    TEST_ASSERT_EQUAL(-1, result);
}

void test_recipe_manager_locking(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "locked_recipe", RECIPE_NAME_MAX - 1);

    RecipeManager_SaveRecipe(&recipe, false);
    RecipeManager_LoadRecipe("locked_recipe");

    /* Lock the recipe */
    int result = RecipeManager_Lock();
    TEST_ASSERT_EQUAL(0, result);

    int is_locked = RecipeManager_IsLocked();
    TEST_ASSERT_EQUAL(1, is_locked);

    /* Cannot change recipe while locked */
    result = RecipeManager_LoadRecipe("locked_recipe");
    TEST_ASSERT_EQUAL(-1, result);

    /* Unlock and try again */
    result = RecipeManager_Unlock();
    TEST_ASSERT_EQUAL(0, result);

    is_locked = RecipeManager_IsLocked();
    TEST_ASSERT_EQUAL(0, is_locked);
}

void test_recipe_manager_cannot_delete_locked(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "deleteme", RECIPE_NAME_MAX - 1);

    RecipeManager_SaveRecipe(&recipe, false);
    RecipeManager_LoadRecipe("deleteme");
    RecipeManager_Lock();

    /* Cannot delete while locked */
    int result = RecipeManager_DeleteRecipe("deleteme");
    TEST_ASSERT_EQUAL(-1, result);

    RecipeManager_Unlock();

    /* Now can delete */
    result = RecipeManager_DeleteRecipe("deleteme");
    TEST_ASSERT_EQUAL(0, result);
}

void test_recipe_manager_list_recipes(void) {
    Recipe_t recipe1, recipe2;
    memset(&recipe1, 0, sizeof(recipe1));
    memset(&recipe2, 0, sizeof(recipe2));

    strncpy(recipe1.name, "recipe1", RECIPE_NAME_MAX - 1);
    strncpy(recipe2.name, "recipe2", RECIPE_NAME_MAX - 1);

    RecipeManager_SaveRecipe(&recipe1, false);
    RecipeManager_SaveRecipe(&recipe2, false);

    int32_t count = RecipeManager_GetRecipeCount(NULL, NULL);
    TEST_ASSERT_EQUAL(2, count);
}

void test_recipe_manager_save_last_used(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "last_used_recipe", RECIPE_NAME_MAX - 1);

    RecipeManager_SaveRecipe(&recipe, false);
    RecipeManager_LoadRecipe("last_used_recipe");

    /* Save as last used */
    int result = RecipeManager_SaveAsLastUsed();
    TEST_ASSERT_EQUAL(0, result);

    /* Verify it was saved */
    char last_recipe[RECIPE_NAME_MAX];
    int32_t size = Storage_ReadString(STORAGE_DOMAIN_CONFIGURATION, "_last_recipe",
                                      last_recipe, RECIPE_NAME_MAX);
    TEST_ASSERT_GREATER_THAN(0, size);
    TEST_ASSERT_EQUAL_STRING("last_used_recipe", last_recipe);
}

void test_recipe_manager_load_last_used(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "persisted_recipe", RECIPE_NAME_MAX - 1);

    /* Save recipe and mark as last used */
    RecipeManager_SaveRecipe(&recipe, false);
    RecipeManager_LoadRecipe("persisted_recipe");
    RecipeManager_SaveAsLastUsed();

    /* Simulate reboot: reinit should load last recipe */
    RecipeManager_Deinit();
    RecipeManager_Mock_Reset();
    RecipeManager_Init();

    /* Should have loaded the last recipe */
    const char* current = RecipeManager_GetCurrentName();
    TEST_ASSERT_EQUAL_STRING("persisted_recipe", current);
}

void test_recipe_manager_get_current_metadata(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "metadata_recipe", RECIPE_NAME_MAX - 1);
    strncpy(recipe.product, "G3-Main", RECIPE_PRODUCT_MAX - 1);
    strncpy(recipe.variant, "Rev1.1", RECIPE_VARIANT_MAX - 1);
    recipe.revision = 42;

    RecipeManager_SaveRecipe(&recipe, false);
    RecipeManager_LoadRecipe("metadata_recipe");

    RecipeMetadata_t metadata;
    int result = RecipeManager_GetCurrentMetadata(&metadata);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL_STRING("metadata_recipe", metadata.name);
    TEST_ASSERT_EQUAL_STRING("G3-Main", metadata.product);
    TEST_ASSERT_EQUAL_STRING("Rev1.1", metadata.variant);
    TEST_ASSERT_EQUAL(42, metadata.revision);
}

void test_recipe_manager_update_recipe(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "update_recipe", RECIPE_NAME_MAX - 1);
    recipe.revision = 1;

    RecipeManager_SaveRecipe(&recipe, false);

    /* Update revision */
    recipe.revision = 2;
    int result = RecipeManager_UpdateRecipe(&recipe);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify update */
    RecipeManager_LoadRecipe("update_recipe");
    const Recipe_t* current = RecipeManager_GetCurrent();
    TEST_ASSERT_EQUAL(2, current->revision);
}

void test_recipe_manager_multi_channel(void) {
    Recipe_t recipe;
    memset(&recipe, 0, sizeof(recipe));
    strncpy(recipe.name, "shared_recipe", RECIPE_NAME_MAX - 1);

    RecipeManager_SaveRecipe(&recipe, false);

    /* Load on all channels (mock just loads locally) */
    int result = RecipeManager_LoadRecipeMultiChannel("shared_recipe");
    TEST_ASSERT_EQUAL(0, result);

    /* Verify it's loaded */
    const char* current = RecipeManager_GetCurrentName();
    TEST_ASSERT_EQUAL_STRING("shared_recipe", current);
}

/* ==================== Integration Tests ==================== */

void test_storage_and_recipe_integration(void) {
    /* Save multiple recipes */
    for (int i = 0; i < 5; i++) {
        Recipe_t recipe;
        memset(&recipe, 0, sizeof(recipe));
        snprintf(recipe.name, RECIPE_NAME_MAX, "recipe_%d", i);
        snprintf(recipe.product, RECIPE_PRODUCT_MAX, "Product%d", i);
        RecipeManager_SaveRecipe(&recipe, false);
    }

    /* List recipes */
    int32_t count = RecipeManager_GetRecipeCount(NULL, NULL);
    TEST_ASSERT_EQUAL(5, count);

    /* Load one */
    int result = RecipeManager_LoadRecipe("recipe_2");
    TEST_ASSERT_EQUAL(0, result);

    /* Delete another (not currently loaded) */
    result = RecipeManager_DeleteRecipe("recipe_0");
    TEST_ASSERT_EQUAL(0, result);

    /* Verify count decreased */
    count = RecipeManager_GetRecipeCount(NULL, NULL);
    TEST_ASSERT_EQUAL(4, count);
}

/* ==================== Test Runner ==================== */

int main(void) {
    UNITY_BEGIN();

    /* Storage tests */
    RUN_TEST(test_storage_write_and_read);
    RUN_TEST(test_storage_read_nonexistent_key);
    RUN_TEST(test_storage_delete);
    RUN_TEST(test_storage_update);
    RUN_TEST(test_storage_multiple_domains);
    RUN_TEST(test_storage_list_keys);
    RUN_TEST(test_storage_clear_domain);
    RUN_TEST(test_storage_size_check);

    /* Recipe manager tests */
    RUN_TEST(test_recipe_manager_save_and_load);
    RUN_TEST(test_recipe_manager_load_nonexistent);
    RUN_TEST(test_recipe_manager_locking);
    RUN_TEST(test_recipe_manager_cannot_delete_locked);
    RUN_TEST(test_recipe_manager_list_recipes);
    RUN_TEST(test_recipe_manager_save_last_used);
    RUN_TEST(test_recipe_manager_load_last_used);
    RUN_TEST(test_recipe_manager_get_current_metadata);
    RUN_TEST(test_recipe_manager_update_recipe);
    RUN_TEST(test_recipe_manager_multi_channel);

    /* Integration tests */
    RUN_TEST(test_storage_and_recipe_integration);

    return UNITY_END();
}
