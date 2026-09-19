#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "x_client.h"

static console_x_cycle_cb_t s_on_x_cycle;

static int set_value(const char *key, int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: %s <value>\n", argv[0]);
        return 1;
    }
    esp_err_t err = x_store_set(key, argv[1]);
    printf("%s: %s\n", key, err == ESP_OK ? "stored" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_set_client_id(int argc, char **argv) { return set_value(X_KEY_CLIENT_ID, argc, argv); }
static int cmd_set_secret(int argc, char **argv)    { return set_value(X_KEY_CLIENT_SECRET, argc, argv); }
static int cmd_set_refresh(int argc, char **argv)   { return set_value(X_KEY_REFRESH_TOKEN, argc, argv); }

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    x_print_status();
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t err = x_store_clear();
    printf("x_auth: %s\n", err == ESP_OK ? "cleared" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_cycle(int argc, char **argv)
{
    int offset = argc > 1 ? atoi(argv[1]) : 0;
    printf("queued: refresh + post to X for today%+d day(s); watch the log\n", offset);
    s_on_x_cycle(offset);
    return 0;
}

void console_start(console_x_cycle_cb_t on_x_cycle)
{
    s_on_x_cycle = on_x_cycle;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "panch> ";
    repl_cfg.max_cmdline_length = 512;
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw, &repl_cfg, &repl));

    const esp_console_cmd_t cmds[] = {
        { .command = "x_set_client_id", .help = "Store the X OAuth2 client id", .func = cmd_set_client_id },
        { .command = "x_set_secret",    .help = "Store the X OAuth2 client secret", .func = cmd_set_secret },
        { .command = "x_set_refresh",   .help = "Store the X refresh token (seed / re-seed)", .func = cmd_set_refresh },
        { .command = "x_status",        .help = "Show which X credentials are set (never their values)", .func = cmd_status },
        { .command = "x_clear",         .help = "Erase all stored X credentials", .func = cmd_clear },
        { .command = "x_cycle",         .help = "Run one refresh+post cycle now: x_cycle [day_offset]", .func = cmd_cycle },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
