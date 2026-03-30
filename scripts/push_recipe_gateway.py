#!/usr/bin/env python3
"""Push a recipe JSON to the gateway recipe-server via REST API.

Workflow: POST /recipes/ (draft) → submit → approve → activate
Activate triggers MQTT DCMD recipe_update to all registered fixtures.

Usage:
    python3 scripts/push_recipe_gateway.py recipes/g3-mb-v2.json
    python3 scripts/push_recipe_gateway.py recipes/g3-mb-v2.json --gateway http://192.168.50.1:8001
"""

import argparse
import json
import sys

try:
    import requests
except ImportError:
    print("requests required: pip install requests")
    sys.exit(1)

import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(__file__))

DEFAULT_GATEWAY = "http://192.168.50.1:8001"
ACTOR = "t0-single-dev"


def main() -> int:
    parser = argparse.ArgumentParser(description="Push recipe to gateway REST API")
    parser.add_argument("recipe_file", help="Path to recipe JSON file")
    parser.add_argument("--gateway", default=DEFAULT_GATEWAY,
                        help=f"Gateway API base URL (default {DEFAULT_GATEWAY})")
    args = parser.parse_args()

    with open(args.recipe_file, encoding="utf-8") as f:
        recipe = json.load(f)

    recipe_id = recipe.get("recipeId", "")
    version = recipe.get("recipeVersion", "")
    if not recipe_id or not version:
        print("ERROR: recipe must have recipeId and recipeVersion")
        return 1

    base = args.gateway.rstrip("/")
    print(f"Gateway: {base}")
    print(f"Recipe:  {recipe_id} v{version}  ({len(recipe.get('steps', []))} steps)")

    # 1 — Create draft
    print("\n[1] POST /recipes/ (create draft) ...")
    r = requests.post(f"{base}/recipes/", json=recipe, timeout=10)
    if r.status_code not in (200, 201):
        print(f"  FAIL {r.status_code}: {r.text[:300]}")
        return 1
    data = r.json()
    print(f"  OK: sha={data.get('sha', '?')}")

    # Get version_id from recipe_versions
    print("\n[2] Fetching version ID ...")
    r = requests.get(f"{base}/recipes/versions", params={"recipe_id": recipe_id}, timeout=10)
    if r.status_code != 200:
        print(f"  FAIL {r.status_code}: {r.text[:200]}")
        return 1
    versions = r.json()
    match = [v for v in versions if v.get("version") == version]
    if not match:
        print(f"  ERROR: version {version} not found in versions list")
        return 1
    vid = match[0]["id"]
    state = match[0]["state"]
    print(f"  version_id={vid}  state={state}")

    # Skip already-activated versions
    if state == "active":
        print(f"\nVersion {version} is already active — nothing to do.")
        return 0

    # 3 — Submit
    if state in ("draft", "rejected"):
        print(f"\n[3] Submit ...")
        r = requests.post(f"{base}/recipes/versions/{vid}/submit",
                          json={"actor": ACTOR}, timeout=10)
        if r.status_code != 200:
            print(f"  FAIL {r.status_code}: {r.text[:200]}")
            return 1
        print(f"  OK: state={r.json().get('state')}")

    # 4 — Approve
    print(f"\n[4] Approve ...")
    r = requests.post(f"{base}/recipes/versions/{vid}/approve",
                      json={"actor": ACTOR}, timeout=10)
    if r.status_code != 200:
        print(f"  FAIL {r.status_code}: {r.text[:200]}")
        return 1
    print(f"  OK: state={r.json().get('state')}")

    # 5 — Activate (sends DCMD to all fixtures)
    print(f"\n[5] Activate (pushes DCMD to all fixtures) ...")
    r = requests.post(f"{base}/recipes/versions/{vid}/activate",
                      json={"actor": ACTOR}, timeout=15)
    if r.status_code != 200:
        print(f"  FAIL {r.status_code}: {r.text[:200]}")
        return 1
    result = r.json()
    print(f"  OK: activated={result.get('activated')}  "
          f"fixtures_notified={result.get('fixtures_notified', 0)}")

    print(f"\nDone. {recipe_id}@{version} is now active on gateway.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
