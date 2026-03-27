"""tc_config.py — Central config for all TC scripts.

Edit scripts/local_config.py (not committed) to override any value.
Copy scripts/local_config.py.example to get started.
"""

try:
    from local_config import TC_IP, TC_SERIAL, BROKER, WIFI_SSID, WIFI_PASS
except ImportError:
    TC_IP      = "192.168.50.30"
    TC_SERIAL  = "G3-MB-Tester-000"
    BROKER     = "192.168.50.1"
    WIFI_SSID  = "FixtureOpsGateway"
    WIFI_PASS  = "gateway123"

# Aliases used by TCP-console scripts
HOST = TC_IP
PORT = 4242
