/**
 * @file test_ota_chunk.c
 * @brief Native tests for Path C chunked OTA receiver logic in cmd_ota.c.
 *
 * Exercises:
 *   - hex_to_bin (SHA-256 hex string to binary conversion)
 *   - base64 field extraction from a mock DCMD JSON payload
 *   - chunk sequence tracking (seq mismatch detection)
 *   - full single-chunk happy path (begin → write → validate → end → set_boot)
 *   - SHA-256 mismatch abort
 *   - missing "data" field abort
 *
 * All ESP-IDF OTA and mbedTLS functions are mocked in the stub headers in
 * this test directory.  Pattern follows test_recipe_steps.c.
 */

#ifndef NATIVE_BUILD
#define NATIVE_BUILD
#endif

/* ── ESP-IDF stubs — these shadow real headers via -I flag ordering ── */
/* NOTE: order matters; esp_console.h defines esp_err_t used by others. */

/* ── Unit under test ── */
#include "../../common/src/cmd_ota.c"

/* ── Unity ── */
#include "unity.h"

/* ── Mock state — defined here; declared extern in stubs ── */
int  g_mock_ota_begin_rc       = 0;
int  g_mock_ota_write_rc       = 0;
int  g_mock_ota_end_rc         = 0;
int  g_mock_ota_set_boot_rc    = 0;
int  g_mock_ota_abort_called   = 0;
int  g_mock_ota_end_called     = 0;
int  g_mock_ota_set_boot_called= 0;
int  g_mock_ota_write_count    = 0;
int  g_mock_no_partition       = 0;
int  g_mock_esp_restart_called = 0;
int  g_mock_ota_progress_calls = 0;
const char *g_mock_ota_progress_status = NULL;
int  g_mock_ndeath_calls       = 0;

/* ── tc_mqtt stub implementations (non-static so linker finds them) ── */
void tc_mqtt_publish_ota_progress(const char *target, const char *status,
                                   int pct, const char *code,
                                   const char *msg, bool rollback)
{
    (void)target; (void)pct; (void)code; (void)msg; (void)rollback;
    g_mock_ota_progress_calls++;
    g_mock_ota_progress_status = status;
}

void tc_mqtt_publish_ndeath(void)
{
    g_mock_ndeath_calls++;
}

bool tc_mqtt_lbb_enabled(void) { return false; }

static void reset_mocks(void)
{
    g_mock_ota_begin_rc        = 0;
    g_mock_ota_write_rc        = 0;
    g_mock_ota_end_rc          = 0;
    g_mock_ota_set_boot_rc     = 0;
    g_mock_ota_abort_called    = 0;
    g_mock_ota_end_called      = 0;
    g_mock_ota_set_boot_called = 0;
    g_mock_ota_write_count     = 0;
    g_mock_no_partition        = 0;
    g_mock_esp_restart_called  = 0;
    g_mock_ota_progress_calls  = 0;
    g_mock_ota_progress_status = NULL;
    g_mock_ndeath_calls        = 0;

    /* Reset static session state via the abort helper */
    chunk_session_abort();
}

/* ───────────────────────────────────────────────────────────────────────────
 * Helper: build a minimal ota_chunk DCMD JSON payload
 *
 * The function builds:
 *   {"cmd":"ota_chunk","seq":<seq>,"offset":<offset>,"data":"<b64>","total":<total>[,"sha256":"<hex>"]}
 * into buf (caller must provide enough space).
 * ─────────────────────────────────────────────────────────────────────────── */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Real base64 encode for test helpers — minimal RFC 4648 implementation */
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max)
{
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i+1 < in_len) v |= (uint32_t)in[i+1] << 8;
        if (i+2 < in_len) v |= (uint32_t)in[i+2];
        if (out_pos + 4 >= out_max) break;
        out[out_pos++] = B64_CHARS[(v >> 18) & 0x3F];
        out[out_pos++] = B64_CHARS[(v >> 12) & 0x3F];
        out[out_pos++] = (i+1 < in_len) ? B64_CHARS[(v >> 6) & 0x3F] : '=';
        out[out_pos++] = (i+2 < in_len) ? B64_CHARS[v & 0x3F]        : '=';
    }
    out[out_pos] = '\0';
    return out_pos;
}

/* Compute SHA-256 hex string using our stub implementation */
static void sha256_hex(const uint8_t *data, size_t len, char *hex_out)
{
    mbedtls_sha256_context ctx;
    uint8_t digest[32];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, digest);
    for (int i = 0; i < 32; i++)
        sprintf(hex_out + i*2, "%02x", (unsigned)digest[i]);
    hex_out[64] = '\0';
}

static int build_chunk_payload(char *buf, size_t buf_sz,
                                uint32_t seq, uint32_t total,
                                const uint8_t *data, size_t data_len,
                                const char *sha256_hex_str)  /* NULL if not last chunk */
{
    /* base64-encode the chunk data */
    size_t b64_max = (data_len * 4 / 3) + 8;
    char  *b64     = (char *)malloc(b64_max);
    if (!b64) return -1;
    b64_encode(data, data_len, b64, b64_max);

    int n;
    if (sha256_hex_str) {
        n = snprintf(buf, buf_sz,
            "{\"cmd\":\"ota_chunk\",\"seq\":%u,\"offset\":%u,"
            "\"data\":\"%s\",\"total\":%u,\"sha256\":\"%s\"}",
            (unsigned)seq, (unsigned)(seq * data_len),
            b64, (unsigned)total, sha256_hex_str);
    } else {
        n = snprintf(buf, buf_sz,
            "{\"cmd\":\"ota_chunk\",\"seq\":%u,\"offset\":%u,"
            "\"data\":\"%s\",\"total\":%u}",
            (unsigned)seq, (unsigned)(seq * data_len),
            b64, (unsigned)total);
    }
    free(b64);
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Tests: hex_to_bin
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_hex_to_bin_valid_lowercase(void)
{
    /* Known SHA-256 of empty string */
    const char *hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    uint8_t bin[32];
    TEST_ASSERT_EQUAL_INT(0, hex_to_bin(hex, bin, sizeof(bin)));
    TEST_ASSERT_EQUAL_HEX8(0xe3, bin[0]);
    TEST_ASSERT_EQUAL_HEX8(0xb0, bin[1]);
    TEST_ASSERT_EQUAL_HEX8(0x55, bin[31]);
}

void test_hex_to_bin_valid_uppercase(void)
{
    const char *hex = "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855";
    uint8_t bin[32];
    TEST_ASSERT_EQUAL_INT(0, hex_to_bin(hex, bin, sizeof(bin)));
    TEST_ASSERT_EQUAL_HEX8(0xe3, bin[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, bin[31]);
}

void test_hex_to_bin_invalid_char(void)
{
    /* 'g' is not a valid hex digit */
    const char *hex = "g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    uint8_t bin[32];
    TEST_ASSERT_NOT_EQUAL(0, hex_to_bin(hex, bin, sizeof(bin)));
}

void test_hex_to_bin_buffer_too_small(void)
{
    const char *hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    uint8_t bin[16];  /* only 16 bytes — too small for 32 */
    TEST_ASSERT_NOT_EQUAL(0, hex_to_bin(hex, bin, sizeof(bin)));
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Tests: base64 field extraction (via ota_chunk_dcmd parse path)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_base64_extraction_missing_data_field(void)
{
    reset_mocks();
    /* Payload with no "data" field — should fail gracefully */
    const char *payload = "{\"cmd\":\"ota_chunk\",\"seq\":0,\"offset\":0,\"total\":4}";
    ota_chunk_dcmd(payload, (int)strlen(payload));

    TEST_ASSERT_GREATER_THAN(0, g_mock_ota_progress_calls);
    /* error_code should be CHUNK_PARSE_FAILED */
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
}

void test_base64_extraction_empty_data_field(void)
{
    reset_mocks();
    const char *payload = "{\"cmd\":\"ota_chunk\",\"seq\":0,\"offset\":0,\"total\":4,\"data\":\"\"}";
    ota_chunk_dcmd(payload, (int)strlen(payload));

    TEST_ASSERT_GREATER_THAN(0, g_mock_ota_progress_calls);
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
}

void test_base64_extraction_valid_data_decoded(void)
{
    reset_mocks();
    /* Single-byte payload: 0xAB encoded is "qw==" */
    /* Build a 1-byte firmware blob */
    uint8_t fw[1] = { 0xAB };
    char sha_hex[65];
    sha256_hex(fw, sizeof(fw), sha_hex);

    char buf[512];
    int  n = build_chunk_payload(buf, sizeof(buf), 0, 1, fw, 1, sha_hex);
    TEST_ASSERT_GREATER_THAN(0, n);

    ota_chunk_dcmd(buf, n);

    /* Should have called esp_ota_write once, then completed */
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_write_count);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_end_called);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_set_boot_called);
    TEST_ASSERT_EQUAL_STRING("complete", g_mock_ota_progress_status);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ndeath_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_restart_called);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Tests: sequence tracking
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_seq_mismatch_on_second_chunk(void)
{
    reset_mocks();

    /* Send chunk 0 OK (multi-chunk session: total=6, first chunk has 3 bytes) */
    uint8_t data0[3] = { 0x01, 0x02, 0x03 };
    char buf[512];
    int  n = build_chunk_payload(buf, sizeof(buf), 0, 6, data0, 3, NULL);
    ota_chunk_dcmd(buf, n);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_write_count);  /* wrote chunk 0 */

    /* Now send seq=2 instead of seq=1 — should abort */
    uint8_t data2[3] = { 0x07, 0x08, 0x09 };
    n = build_chunk_payload(buf, sizeof(buf), 2, 6, data2, 3, NULL);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_abort_called);
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
    /* Session must be reset: a new seq=0 should start a fresh session */
    TEST_ASSERT_FALSE(s_chunk_active);
}

void test_seq_first_chunk_must_be_zero(void)
{
    reset_mocks();
    /* Send seq=1 as first chunk — should fail without beginning OTA */
    uint8_t data[4] = { 1, 2, 3, 4 };
    char buf[512];
    int  n = build_chunk_payload(buf, sizeof(buf), 1, 4, data, 4, NULL);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(0, g_mock_ota_write_count);
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
}

void test_seq_no_partition_fails_gracefully(void)
{
    reset_mocks();
    g_mock_no_partition = 1;

    uint8_t data[4] = { 1, 2, 3, 4 };
    char buf[512];
    int  n = build_chunk_payload(buf, sizeof(buf), 0, 4, data, 4, NULL);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(0, g_mock_ota_write_count);
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Tests: SHA-256 validation
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_sha256_mismatch_aborts_session(void)
{
    reset_mocks();

    uint8_t fw[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    /* Provide a deliberately wrong SHA-256 */
    const char *wrong_sha = "0000000000000000000000000000000000000000000000000000000000000000";

    char buf[512];
    int  n = build_chunk_payload(buf, sizeof(buf), 0, 8, fw, 8, wrong_sha);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(0, g_mock_ota_end_called);
    TEST_ASSERT_EQUAL_INT(0, g_mock_ota_set_boot_called);
    TEST_ASSERT_EQUAL_STRING("error", g_mock_ota_progress_status);
    TEST_ASSERT_EQUAL_INT(0, g_mock_esp_restart_called);
}

void test_sha256_correct_completes_flash(void)
{
    reset_mocks();

    uint8_t fw[16];
    for (int i = 0; i < 16; i++) fw[i] = (uint8_t)i;
    char sha_hex[65];
    sha256_hex(fw, sizeof(fw), sha_hex);

    char buf[1024];
    int  n = build_chunk_payload(buf, sizeof(buf), 0, 16, fw, 16, sha_hex);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_end_called);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_set_boot_called);
    TEST_ASSERT_EQUAL_STRING("complete", g_mock_ota_progress_status);
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_restart_called);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Tests: multi-chunk session
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_two_chunk_session_complete(void)
{
    reset_mocks();

    /* 6-byte firmware split into two 3-byte chunks */
    uint8_t fw[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    char sha_hex[65];
    sha256_hex(fw, sizeof(fw), sha_hex);

    char buf[512];
    int  n;

    /* Chunk 0 */
    n = build_chunk_payload(buf, sizeof(buf), 0, 6, fw, 3, NULL);
    ota_chunk_dcmd(buf, n);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_write_count);
    TEST_ASSERT_FALSE(!s_chunk_active);  /* session still open */

    /* Chunk 1 (last) */
    n = build_chunk_payload(buf, sizeof(buf), 1, 6, fw + 3, 3, sha_hex);
    ota_chunk_dcmd(buf, n);

    TEST_ASSERT_EQUAL_INT(2, g_mock_ota_write_count);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_end_called);
    TEST_ASSERT_EQUAL_INT(1, g_mock_ota_set_boot_called);
    TEST_ASSERT_EQUAL_STRING("complete", g_mock_ota_progress_status);
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_restart_called);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Unity runner
 * ═══════════════════════════════════════════════════════════════════════════ */

void setUp(void)    { reset_mocks(); }
void tearDown(void) { /* nothing */ }

int main(void)
{
    UNITY_BEGIN();

    /* hex_to_bin */
    RUN_TEST(test_hex_to_bin_valid_lowercase);
    RUN_TEST(test_hex_to_bin_valid_uppercase);
    RUN_TEST(test_hex_to_bin_invalid_char);
    RUN_TEST(test_hex_to_bin_buffer_too_small);

    /* base64 extraction */
    RUN_TEST(test_base64_extraction_missing_data_field);
    RUN_TEST(test_base64_extraction_empty_data_field);
    RUN_TEST(test_base64_extraction_valid_data_decoded);

    /* sequence tracking */
    RUN_TEST(test_seq_mismatch_on_second_chunk);
    RUN_TEST(test_seq_first_chunk_must_be_zero);
    RUN_TEST(test_seq_no_partition_fails_gracefully);

    /* SHA-256 validation */
    RUN_TEST(test_sha256_mismatch_aborts_session);
    RUN_TEST(test_sha256_correct_completes_flash);

    /* multi-chunk */
    RUN_TEST(test_two_chunk_session_complete);

    return UNITY_END();
}
