#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "version.h"
#include "net_console.h"

static const char *TAG = "net_console";

/* Active TCP client fd; -1 = no client connected.
 * Checked by __wrap__write_r to tee stdout to the TCP client. */
volatile int g_net_client_fd = -1;

static volatile bool s_running = false;
static TaskHandle_t  s_task    = NULL;

/* ── Linker-wrap stdout tee ───────────────────────────────────────────────── *
 * GCC --wrap=_write_r renames the ESP-IDF VFS _write_r to __real__write_r   *
 * and routes all callers through __wrap__write_r.  We forward to the real    *
 * implementation (USB JTAG / UART) and additionally copy stdout/stderr to    *
 * the connected TCP client so the full command output is visible remotely.   */

extern ssize_t __real__write_r(struct _reent *r, int fd,
                               const void *buf, size_t len);

ssize_t __wrap__write_r(struct _reent *r, int fd,
                        const void *buf, size_t len)
{
    ssize_t result = __real__write_r(r, fd, buf, len);
    if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && g_net_client_fd >= 0) {
        /* Best-effort: ignore send errors (client may have disconnected) */
        send(g_net_client_fd, buf, (size_t)len, MSG_DONTWAIT);
    }
    return result;
}

/* ── TCP readline (with echo and backspace support) ──────────────────────── */

static int net_readline(int fd, char *buf, size_t maxlen)
{
    size_t pos = 0;
    while (pos < maxlen - 1) {
        char c;
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return -1;          /* client disconnected */
        if (c == '\n') break;
        if (c == '\r') continue;        /* skip bare CR */
        if (c == '\b' || c == 127) {    /* backspace / DEL */
            if (pos > 0) {
                pos--;
                send(fd, "\b \b", 3, MSG_DONTWAIT);
            }
            continue;
        }
        buf[pos++] = c;
        send(fd, &c, 1, MSG_DONTWAIT);  /* echo */
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* ── TCP console server task ─────────────────────────────────────────────── */

static void tcp_console_task(void *arg)
{
    uint16_t port = (uint16_t)(uintptr_t)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }
    listen(server_fd, 1);
    ESP_LOGI(TAG, "TCP console listening on port %u", port);

    while (s_running) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (s_running) ESP_LOGW(TAG, "accept() failed: %d", errno);
            break;
        }

        ESP_LOGI(TAG, "Client connected on fd %d", client_fd);
        g_net_client_fd = client_fd;

        /* Welcome banner + initial prompt */
        dprintf(client_fd, "\r\nG3 TC bringup shell  fw=" FW_VERSION_FULL "\r\n"
                           "Type 'help' for commands.\r\n\r\n");
        dprintf(client_fd, "g3-tc|" FW_VERSION_STRING "> ");

        char line[256];
        while (s_running) {
            int len = net_readline(client_fd, line, sizeof(line));
            if (len < 0) break;             /* disconnected */
            dprintf(client_fd, "\r\n");     /* newline after echoed input */

            if (len == 0) {
                /* empty line — just re-show prompt */
                dprintf(client_fd, "g3-tc|" FW_VERSION_STRING "> ");
                continue;
            }

            int cmd_ret = 0;
            /* Command output goes to stdout, which __wrap__write_r tees to
             * client_fd.  Prompt is sent directly so it only appears on TCP. */
            esp_console_run(line, &cmd_ret);

            dprintf(client_fd, "g3-tc|" FW_VERSION_STRING "> ");
        }

        g_net_client_fd = -1;
        close(client_fd);
        ESP_LOGI(TAG, "Client disconnected");
    }

    close(server_fd);
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void net_console_start(uint16_t port)
{
    if (s_running) return;
    s_running = true;
    xTaskCreate(tcp_console_task, "tcp_console",
                4096, (void *)(uintptr_t)port, 5, &s_task);
}

void net_console_stop(void)
{
    s_running = false;
    /* Closing the server socket will unblock accept() */
    if (s_task) {
        /* Task self-deletes when s_running becomes false */
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
