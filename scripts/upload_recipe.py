#!/usr/bin/env python3
"""Upload a recipe JSON file to the TC via the TCP console.

Usage:
    python3 scripts/upload_recipe.py recipes/g3-mb-v1.json [recipe-id]
    python3 scripts/upload_recipe.py recipes/g3-mb-v1.json g3-mb-v1 --set-active

The script minifies the JSON, base64-encodes it, and sends it via the
'recipe set-b64 <id> <base64>' console command. Requires firmware with
max_cmdline_length >= 4096 (set in main.cpp).

After upload the TC can run the recipe with:
    recipe run <id>
Or set it as active (so state-machine picks it up on next sm start):
    nvs_set recipes active str <id>    (if nvs console available)
Use --set-active to do this automatically via DCMD if TC is MQTT-connected.
"""

import base64
import json
import os
import sys
import socket
import time
import argparse

TCC_IP   = "10.0.0.244"
TCC_PORT = 4242


def connect(ip: str = TCC_IP, port: int = TCC_PORT) -> socket.socket:
    s = socket.socket()
    s.settimeout(15)
    s.connect((ip, port))
    time.sleep(0.5)
    try:
        s.recv(4096)  # drain banner
    except OSError:
        pass
    return s


def send_cmd(sock: socket.socket, cmd: str, wait: float = 5.0) -> str:
    sock.sendall((cmd + "\n").encode())
    out = []
    deadline = time.time() + wait
    sock.settimeout(1)
    while time.time() < deadline:
        try:
            chunk = sock.recv(8192).decode(errors="replace")
            if chunk:
                out.append(chunk)
                if "g3-tc|" in chunk and ">" in chunk:
                    break
        except OSError:
            continue
    return "".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Upload recipe JSON to TC")
    parser.add_argument("recipe_file", help="Path to recipe JSON file")
    parser.add_argument("recipe_id",   nargs="?",
                        help="Recipe ID (default: recipeId field from JSON)")
    parser.add_argument("--set-active", action="store_true",
                        help="Set this recipe as active after upload (requires NVS console cmd)")
    parser.add_argument("--ip",  default=TCC_IP,  help=f"TC IP (default {TCC_IP})")
    parser.add_argument("--port",default=TCC_PORT, type=int, help=f"TC port (default {TCC_PORT})")
    args = parser.parse_args()

    if not os.path.isfile(args.recipe_file):
        print(f"ERROR: file not found: {args.recipe_file}")
        return 1

    with open(args.recipe_file, encoding="utf-8") as f:
        recipe_data = json.load(f)

    recipe_id = args.recipe_id or recipe_data.get("recipeId", "default")
    minified   = json.dumps(recipe_data, separators=(",", ":"))
    b64        = base64.b64encode(minified.encode()).decode()

    print(f"Recipe:  {recipe_id}")
    print(f"File:    {args.recipe_file}")
    print(f"Steps:   {len(recipe_data.get('steps', []))}")
    print(f"JSON:    {len(minified)} bytes  →  base64: {len(b64)} bytes")

    if len(b64) > 3800:
        print(f"WARNING: base64 payload is {len(b64)} bytes — "
              "ensure max_cmdline_length >= 4096 in firmware")

    print(f"Connecting to {args.ip}:{args.port} ...")
    try:
        sock = connect(args.ip, args.port)
    except OSError as e:
        print(f"ERROR: cannot connect — {e}")
        return 1

    cmd = f"recipe set-b64 {recipe_id} {b64}"
    print(f"Sending recipe set-b64 ({len(cmd)} chars) ...")
    resp = send_cmd(sock, cmd, wait=10.0)
    print(f"Response: {resp.strip()}")

    success = "stored" in resp.lower()
    if not success:
        sock.close()
        return 1

    if args.set_active:
        # Use NVS console command to set active recipe (requires nvs_set console cmd)
        # This may not be available — print manual fallback if it fails
        print(f"Setting active recipe to '{recipe_id}' ...")
        resp2 = send_cmd(sock, f"nvs_set recipes active str {recipe_id}", wait=3.0)
        if "ok" in resp2.lower() or "set" in resp2.lower():
            print(f"Active recipe set to '{recipe_id}'")
        else:
            print(f"Could not set active via console. Set manually:")
            print(f"  nvs_set recipes active str {recipe_id}")

    sock.close()
    print(f"\nDone. Recipe '{recipe_id}' stored on TC.")
    print(f"Run with: python3 scripts/tc_cmd.py 'recipe run {recipe_id}' 60")
    return 0


if __name__ == "__main__":
    sys.exit(main())
