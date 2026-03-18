#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*primitive_fn_t)(void);

typedef struct {
    const char   *id;
    primitive_fn_t fn;
} primitive_entry_t;

/* Look up a primitive by string ID. Returns NULL if not found. */
primitive_fn_t recipe_primitives_lookup(const char *id);

/* Get the full dispatch table (for iteration/listing). */
const primitive_entry_t *recipe_primitives_table(int *count_out);

#ifdef __cplusplus
}
#endif
