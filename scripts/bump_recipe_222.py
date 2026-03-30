#!/usr/bin/env python3
"""v2.2.2: remove ltc2498_snapshot + ltc2498_compare_snapshot pairs; bump version."""
import json
import pathlib

RECIPE_PATH = pathlib.Path(__file__).parent.parent / "recipes" / "g3-mb-v2.json"
REMOVE_PRIMITIVES = {"ltc2498_snapshot", "ltc2498_compare_snapshot"}

with open(RECIPE_PATH) as f:
    recipe = json.load(f)

before = len(recipe["steps"])
recipe["steps"] = [s for s in recipe["steps"] if s["primitive"] not in REMOVE_PRIMITIVES]
after = len(recipe["steps"])

recipe["recipeVersion"] = "2.2.2"

with open(RECIPE_PATH, "w") as f:
    json.dump(recipe, f, separators=(",", ":"))

print(f"Steps: {before} → {after} (removed {before - after})")
print(f"Version: 2.2.2")
print(f"Written: {RECIPE_PATH}")
