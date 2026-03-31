#!/usr/bin/env python3
"""Patch tc_mqtt.c to add DcmdAck DDATA support (baton ec8fb523).

DcmdAck rules (from PRD R2.7.3–R2.7.7):
  - Config-only cmds (nvs_set): always ACK accepted + rejected
  - State-change cmds (start, abort, selftest, ...): ACK ONLY on rejection
  - Unknown cmd / malformed / missing fields: always ACK rejected
  - DcmdAck is published BEFORE any reboot/rebirth for accepted config cmds
"""

import re
import sys

TARGET = "common/src/tc_mqtt.c"

with open(TARGET, encoding="utf-8") as fh:
    src = fh.read()

orig = src  # keep for diff check

# ── 1. Add publish_dcmd_ack() helper after publish_alert_ddata ──────────────
HELPER = '''
/* ── DcmdAck DDATA — application-level ACK for DCMD commands (R2.7.3–R2.7.7) ─
 * Published on ALL rejections and on accepted config-only commands (nvs_set).
 * State-change command acceptances do NOT publish — the resulting state DDATA
 * is the implicit ACK (sending both would cause spurious duplicate events).    */
static void publish_dcmd_ack(const char *cmd, bool accepted, const char *error)
{
    char ts[32];
    get_iso8601(ts, sizeof(ts));

    char payload[256];
    int n = snprintf(payload, sizeof(payload),
        "{"
        "\\"type\\":\\"dcmd_ack\\","
        "\\"seq\\":%u,"
        "\\"timestamp\\":\\"%s\\","
        "\\"fixture_serial\\":\\"%s\\","
        "\\"channel\\":%u,"
        "\\"DcmdAck/Cmd\\":\\"%s\\","
        "\\"DcmdAck/Outcome\\":\\"%s\\","
        "\\"DcmdAck/Error\\":\\"%s\\""
        "}",
        (unsigned)s_seq, ts, s_serial, (unsigned)s_channel,
        cmd ? cmd : "",
        accepted ? "accepted" : "rejected",
        error ? error : "");

    if (n > 0 && n < (int)sizeof(payload)) {
        lbb_write("DDATA", payload, n);
        if (!s_connected || !s_client) return;
        char topic[TOPIC_LEN];
        make_topic(topic, sizeof(topic), "DDATA");
        int rc = esp_mqtt_client_publish(s_client, topic, payload, n, 1, false);
        if (rc >= 0) s_seq++;
    }
}

'''

src = src.replace(
    "/* ── DCMD dispatch ────────────────────────────────────────────────────────── */",
    "/* ── DCMD dispatch ────────────────────────────────────────────────────────── */" + HELPER,
    1
)

# ── 2. Missing 'cmd' field ──────────────────────────────────────────────────
src = src.replace(
    '    if (!json_get_str(buf, "cmd", cmd, sizeof(cmd))) {\n'
    '        ESP_LOGW(TAG, "DCMD: no \'cmd\' field — ignoring: %s", buf);\n'
    '        return;\n'
    '    }',
    '    if (!json_get_str(buf, "cmd", cmd, sizeof(cmd))) {\n'
    '        ESP_LOGW(TAG, "DCMD: no \'cmd\' field — ignoring: %s", buf);\n'
    '        publish_dcmd_ack("", false, "missing field: cmd");\n'
    '        return;\n'
    '    }',
    1
)

# ── 3. start: check state, publish DcmdAck on rejection ────────────────────
src = src.replace(
    '        mqtt_log("RX", "DCMD start fw_url=%s recipe=%s op=%s",\n'
    '                 firmware_url[0] ? firmware_url : "none",\n'
    '                 recipe_id[0] ? recipe_id : "(active)",\n'
    '                 operation_id[0] ? operation_id : "none");\n'
    '        ESP_LOGI(TAG, "DCMD: start  firmware_url=%s  recipe_id=%s  operation_id=%s",\n'
    '                 firmware_url[0] ? firmware_url : "(none)",\n'
    '                 recipe_id[0] ? recipe_id : "(from NVS)",\n'
    '                 operation_id[0] ? operation_id : "(none)");\n'
    '        tc_sm_cmd_start();\n'
    '        return;\n'
    '    }',
    '        mqtt_log("RX", "DCMD start fw_url=%s recipe=%s op=%s",\n'
    '                 firmware_url[0] ? firmware_url : "none",\n'
    '                 recipe_id[0] ? recipe_id : "(active)",\n'
    '                 operation_id[0] ? operation_id : "none");\n'
    '        ESP_LOGI(TAG, "DCMD: start  firmware_url=%s  recipe_id=%s  operation_id=%s",\n'
    '                 firmware_url[0] ? firmware_url : "(none)",\n'
    '                 recipe_id[0] ? recipe_id : "(from NVS)",\n'
    '                 operation_id[0] ? operation_id : "(none)");\n'
    '        /* DcmdAck on rejection only — tc_sm_cmd_start() state-change DDATA is the\n'
    '         * implicit ACK on acceptance (R2.7.4). */\n'
    '        tc_sm_state_t cur_state = tc_sm_state();\n'
    '        if (cur_state != TC_SM_IDLE && cur_state != TC_SM_ABORTED &&\n'
    '            cur_state != TC_SM_FLASH_FAIL && cur_state != TC_SM_SELFTEST_FAIL) {\n'
    '            char err[48];\n'
    '            snprintf(err, sizeof(err), "wrong state: %s", tc_sm_state_str());\n'
    '            publish_dcmd_ack("start", false, err);\n'
    '            return;\n'
    '        }\n'
    '        tc_sm_cmd_start();\n'
    '        return;\n'
    '    }',
    1
)

# ── 4. diagnostic missing 'test' field ─────────────────────────────────────
src = src.replace(
    '        if (!json_get_str(buf, "test", test, sizeof(test))) {\n'
    '            ESP_LOGW(TAG, "DCMD: diagnostic — missing \'test\' field");\n'
    '            return;\n'
    '        }',
    '        if (!json_get_str(buf, "test", test, sizeof(test))) {\n'
    '            ESP_LOGW(TAG, "DCMD: diagnostic — missing \'test\' field");\n'
    '            publish_dcmd_ack("diagnostic", false, "missing field: test");\n'
    '            return;\n'
    '        }',
    1
)

# ── 5. ota: not idle ────────────────────────────────────────────────────────
src = src.replace(
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota — rejected: not idle (state=%s)", tc_sm_state_str());\n'
    '            tc_mqtt_publish_ota_progress("tc", "aborted", -1,\n'
    '                "PRE_GATE_REJECTED", "Fixture not idle", false);\n'
    '            return;\n'
    '        }',
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota — rejected: not idle (state=%s)", tc_sm_state_str());\n'
    '            tc_mqtt_publish_ota_progress("tc", "aborted", -1,\n'
    '                "PRE_GATE_REJECTED", "Fixture not idle", false);\n'
    '            char ota_err[48];\n'
    '            snprintf(ota_err, sizeof(ota_err), "wrong state: %s", tc_sm_state_str());\n'
    '            publish_dcmd_ack("ota", false, ota_err);\n'
    '            return;\n'
    '        }',
    1
)

# ── 6. ota: missing url ─────────────────────────────────────────────────────
src = src.replace(
    '        if (!json_get_str(buf, "url", url, sizeof(url))) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota — missing \'url\' field");\n'
    '            return;\n'
    '        }',
    '        if (!json_get_str(buf, "url", url, sizeof(url))) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota — missing \'url\' field");\n'
    '            publish_dcmd_ack("ota", false, "missing field: url");\n'
    '            return;\n'
    '        }',
    1
)

# ── 7. ota_rollback: not idle ───────────────────────────────────────────────
src = src.replace(
    '    if (strcmp(cmd, "ota_rollback") == 0) {\n'
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota_rollback — rejected: not idle (state=%s)",\n'
    '                     tc_sm_state_str());\n'
    '            tc_mqtt_publish_ota_progress("tc", "aborted", -1,\n'
    '                "PRE_GATE_REJECTED", "Fixture not idle", false);\n'
    '            return;\n'
    '        }',
    '    if (strcmp(cmd, "ota_rollback") == 0) {\n'
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: ota_rollback — rejected: not idle (state=%s)",\n'
    '                     tc_sm_state_str());\n'
    '            tc_mqtt_publish_ota_progress("tc", "aborted", -1,\n'
    '                "PRE_GATE_REJECTED", "Fixture not idle", false);\n'
    '            char rb_err[48];\n'
    '            snprintf(rb_err, sizeof(rb_err), "wrong state: %s", tc_sm_state_str());\n'
    '            publish_dcmd_ack("ota_rollback", false, rb_err);\n'
    '            return;\n'
    '        }',
    1
)

# ── 8. set_time: invalid epoch ──────────────────────────────────────────────
src = src.replace(
    '        if (epoch_s < 1577836800LL) {\n'
    '            ESP_LOGW(TAG, "DCMD set_time: invalid epoch %lld — rejected", epoch_s);\n'
    '            publish_alert_ddata("warning", "set_time: epoch_s invalid or missing");\n'
    '            return;\n'
    '        }',
    '        if (epoch_s < 1577836800LL) {\n'
    '            ESP_LOGW(TAG, "DCMD set_time: invalid epoch %lld — rejected", epoch_s);\n'
    '            publish_alert_ddata("warning", "set_time: epoch_s invalid or missing");\n'
    '            publish_dcmd_ack("set_time", false, "epoch_s invalid or missing");\n'
    '            return;\n'
    '        }',
    1
)

# ── 9. nvs_set: not idle ────────────────────────────────────────────────────
src = src.replace(
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: nvs_set — rejected: not Idle (state=%s)", tc_sm_state_str());\n'
    '            publish_alert_ddata("warning", "nvs_set: rejected — fixture not Idle");\n'
    '            return;\n'
    '        }',
    '        if (tc_sm_state() != TC_SM_IDLE) {\n'
    '            ESP_LOGW(TAG, "DCMD: nvs_set — rejected: not Idle (state=%s)", tc_sm_state_str());\n'
    '            publish_alert_ddata("warning", "nvs_set: rejected — fixture not Idle");\n'
    '            char nvs_err[48];\n'
    '            snprintf(nvs_err, sizeof(nvs_err), "wrong state: %s", tc_sm_state_str());\n'
    '            publish_dcmd_ack("nvs_set", false, nvs_err);\n'
    '            return;\n'
    '        }',
    1
)

# ── 10. nvs_set: missing 'key' field ───────────────────────────────────────
src = src.replace(
    '        if (!key[0]) {\n'
    '            publish_alert_ddata("warning", "nvs_set: missing \'key\' field");\n'
    '            return;\n'
    '        }',
    '        if (!key[0]) {\n'
    '            publish_alert_ddata("warning", "nvs_set: missing \'key\' field");\n'
    '            publish_dcmd_ack("nvs_set", false, "missing field: key");\n'
    '            return;\n'
    '        }',
    1
)

# ── 11. nvs_set: unknown key ────────────────────────────────────────────────
src = src.replace(
    '        if (!is_identity && !is_ota_ca) {\n'
    '            char alert[64];\n'
    '            snprintf(alert, sizeof(alert), "nvs_set: unknown key \'%s\'", key);\n'
    '            publish_alert_ddata("warning", alert);\n'
    '            return;\n'
    '        }',
    '        if (!is_identity && !is_ota_ca) {\n'
    '            char alert[64];\n'
    '            snprintf(alert, sizeof(alert), "nvs_set: unknown key \'%s\'", key);\n'
    '            publish_alert_ddata("warning", alert);\n'
    '            char nvs_uk_err[64];\n'
    '            snprintf(nvs_uk_err, sizeof(nvs_uk_err), "unknown key: %s", key);\n'
    '            publish_dcmd_ack("nvs_set", false, nvs_uk_err);\n'
    '            return;\n'
    '        }',
    1
)

# ── 12. nvs_set ota_root_ca: success (before return, no rebirth) ────────────
src = src.replace(
    '            ESP_LOGI(TAG, "DCMD nvs_set: ota_root_ca written (%d bytes)", (int)ca_len);\n'
    '            mqtt_log("RX", "DCMD nvs_set ota_root_ca (%d bytes)", (int)ca_len);\n'
    '            return;\n'
    '        }',
    '            ESP_LOGI(TAG, "DCMD nvs_set: ota_root_ca written (%d bytes)", (int)ca_len);\n'
    '            mqtt_log("RX", "DCMD nvs_set ota_root_ca (%d bytes)", (int)ca_len);\n'
    '            publish_dcmd_ack("nvs_set", true, "");\n'
    '            return;\n'
    '        }',
    1
)

# ── 13. nvs_set: missing 'value' field ─────────────────────────────────────
src = src.replace(
    '        if (!json_get_str(buf, "value", value, sizeof(value))) {\n'
    '            publish_alert_ddata("warning", "nvs_set: missing \'value\' field");\n'
    '            return;\n'
    '        }',
    '        if (!json_get_str(buf, "value", value, sizeof(value))) {\n'
    '            publish_alert_ddata("warning", "nvs_set: missing \'value\' field");\n'
    '            publish_dcmd_ack("nvs_set", false, "missing field: value");\n'
    '            return;\n'
    '        }',
    1
)

# ── 14. nvs_set: accepted identity key — publish ACK before rebirth ─────────
src = src.replace(
    '        goto do_rebirth;  /* R5.2: auto-REBIRTH after identity key write */\n'
    '    }',
    '        /* DcmdAck accepted before rebirth — issuer must see ACK before TC reboots */\n'
    '        publish_dcmd_ack("nvs_set", true, "");\n'
    '        goto do_rebirth;  /* R5.2: auto-REBIRTH after identity key write */\n'
    '    }',
    1
)

# ── 15. Unknown cmd fallthrough ─────────────────────────────────────────────
src = src.replace(
    '    ESP_LOGW(TAG, "DCMD: unknown cmd \'%s\'", cmd);\n'
    '    {\n'
    '        char alert_msg[64];\n'
    '        snprintf(alert_msg, sizeof(alert_msg), "DCMD: unknown cmd \'%s\'", cmd);\n'
    '        publish_alert_ddata("warning", alert_msg);\n'
    '    }\n'
    '    return;',
    '    ESP_LOGW(TAG, "DCMD: unknown cmd \'%s\'", cmd);\n'
    '    {\n'
    '        char err_msg[64];\n'
    '        snprintf(err_msg, sizeof(err_msg), "unknown cmd: %s", cmd);\n'
    '        publish_alert_ddata("warning", err_msg);\n'
    '        publish_dcmd_ack(cmd, false, err_msg);\n'
    '    }\n'
    '    return;',
    1
)

# ── Verify all replacements were applied ────────────────────────────────────
checks = [
    ("publish_dcmd_ack helper added",   'static void publish_dcmd_ack(' in src),
    ("missing cmd field",               '"missing field: cmd"' in src),
    ("start wrong state",               '"wrong state: %s", tc_sm_state_str' in src),
    ("diagnostic missing test",         '"missing field: test"' in src),
    ("ota not idle",                     '"wrong state: %s", tc_sm_state_str' in src),
    ("ota missing url",                  '"missing field: url"' in src),
    ("ota_rollback not idle",            '"ota_rollback", false' in src),
    ("set_time invalid epoch",           '"set_time", false' in src),
    ("nvs_set not idle",                 'wrong state: %s", tc_sm_state_str' in src),
    ("nvs_set missing key",              '"missing field: key"' in src),
    ("nvs_set unknown key",              '"unknown key: %s"' in src),
    ("nvs_set ota_root_ca accepted",     '"nvs_set", true' in src),
    ("nvs_set missing value",            '"missing field: value"' in src),
    ("nvs_set identity accepted",        'DcmdAck accepted before rebirth' in src),
    ("unknown cmd fallthrough",          '"unknown cmd: %s"' in src),
]

all_ok = True
for name, ok in checks:
    status = "OK" if ok else "FAIL"
    print(f"  [{status}] {name}")
    if not ok:
        all_ok = False

if not all_ok:
    print("\nERROR: some patches failed to apply — aborting write")
    sys.exit(1)

if src == orig:
    print("\nERROR: no changes were made")
    sys.exit(1)

with open(TARGET, "w", encoding="utf-8") as fh:
    fh.write(src)

print(f"\nPatched {TARGET} successfully.")
