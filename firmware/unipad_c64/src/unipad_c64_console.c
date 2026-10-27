// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ricardo Quesada
//
// Non-blocking interactive USB CDC / UART0 serial console for Unipad C64.

#include "unipad_c64_console.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <btstack_run_loop.h>
#include <pico/stdlib.h>
#include <uni.h>

#include "uni_system.h"
#include "unipad_c64_pio.h"
#include "unipad_c64_platform.h"

#define CONSOLE_POLL_INTERVAL_MS    20u
#define CONSOLE_LINE_MAX_LEN        96u
#define CONSOLE_MAX_ARGS            6u

static btstack_timer_source_t s_console_timer;
static char s_line_buf[CONSOLE_LINE_MAX_LEN];
static size_t s_line_len = 0u;

static void print_help(void) {
    logi(
        "\nUnipad C64 Serial Console Commands:\n"
        "  help                                         Show this command reference\n"
        "  status | version                             Show hardware, PIO, SID clock & controller status\n"
        "  swap_ports                                   Swap Control Port #1 and Control Port #2\n"
        "  gamepad_mode [normal|mouse|twinstick]        Get or set gamepad emulation mode\n"
        "  c64_pot_mode [3buttons|5buttons|rumble|paddle] Get or set persistent C64 POT mode\n"
        "  autofire_cps [1..62]                         Get or set persistent autofire CPS (7/13/29/62)\n"
        "  mouse_scale [0.1..5.0]                       Get or set persistent 1351 mouse sensitivity\n"
        "  c64_tv [auto|pal|ntsc]                       Get or set C64 SID Phi2 clock standard\n"
        "  bt_scan [on|off]                             Enable or disable Bluetooth discovery\n"
        "  list_devices                                 Dump connected Bluetooth HID devices\n"
        "  list_keys                                    List stored Bluetooth pairing keys\n"
        "  del_keys                                     Erase all stored Bluetooth pairing keys\n"
        "  properties                                   Dump all persistent Flash TLV properties\n"
        "  reboot                                       Reboot Unipad C64\n\n");
}

static size_t tokenize_line(char* line, char* argv[], size_t max_args) {
    size_t argc = 0u;
    char* p = line;

    while (*p != '\0' && argc < max_args) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

static void handle_cmd_gamepad_mode(size_t argc, char* argv[]) {
    if (argc < 2u) {
        logi("gamepad_mode = %s\n", unipad_c64_gamepad_mode_to_str(unipad_c64_platform_get_gamepad_mode()));
        return;
    }

    if (strcmp(argv[1], "normal") == 0) {
        unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_NORMAL);
    } else if (strcmp(argv[1], "mouse") == 0) {
        unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_MOUSE);
    } else if (strcmp(argv[1], "twinstick") == 0) {
        unipad_c64_platform_set_gamepad_mode(UNIPAD_GAMEPAD_MODE_TWINSTICK);
    } else {
        loge("Invalid gamepad_mode '%s'. Expected: normal | mouse | twinstick\n", argv[1]);
    }
}

static void handle_cmd_pot_mode(size_t argc, char* argv[]) {
    if (argc < 2u) {
        logi("c64_pot_mode = %s\n", unipad_c64_pot_mode_to_str(unipad_c64_platform_get_pot_mode()));
        return;
    }

    if (strcmp(argv[1], "3buttons") == 0) {
        unipad_c64_platform_set_pot_mode(UNIPAD_C64_POT_MODE_3BUTTONS);
    } else if (strcmp(argv[1], "5buttons") == 0) {
        unipad_c64_platform_set_pot_mode(UNIPAD_C64_POT_MODE_5BUTTONS);
    } else if (strcmp(argv[1], "rumble") == 0) {
        unipad_c64_platform_set_pot_mode(UNIPAD_C64_POT_MODE_RUMBLE);
    } else if (strcmp(argv[1], "paddle") == 0) {
        unipad_c64_platform_set_pot_mode(UNIPAD_C64_POT_MODE_PADDLE);
    } else {
        loge("Invalid c64_pot_mode '%s'. Expected: 3buttons | 5buttons | rumble | paddle\n", argv[1]);
    }
}

static void handle_cmd_autofire_cps(size_t argc, char* argv[]) {
    if (argc < 2u) {
        logi("autofire_cps = %u\n", (unsigned)unipad_c64_platform_get_autofire_cps());
        return;
    }

    char* endptr = NULL;
    const long val = strtol(argv[1], &endptr, 10);
    if (endptr == argv[1] || *endptr != '\0' || val < 1L || val > 62L) {
        loge("Invalid autofire_cps '%s'. Must be an integer between 1 and 62.\n", argv[1]);
        return;
    }
    unipad_c64_platform_set_autofire_cps((uint8_t)val);
}

static void handle_cmd_mouse_scale(size_t argc, char* argv[]) {
    if (argc < 2u) {
        logi("mouse_scale = %.2f\n", (double)unipad_c64_platform_get_mouse_scale());
        return;
    }

    char* endptr = NULL;
    const float val = strtof(argv[1], &endptr);
    if (endptr == argv[1] || *endptr != '\0' || !unipad_c64_platform_set_mouse_scale(val)) {
        loge("Invalid mouse_scale '%s'. Must be between 0.1 and 5.0.\n", argv[1]);
    }
}

static void handle_cmd_c64_tv(size_t argc, char* argv[]) {
    if (argc < 2u) {
        unipad_pio_status_t st;
        unipad_c64_pio_get_status(&st);
        const char* names[] = {"auto", "pal", "ntsc"};
        logi("c64_tv = %s (active=%u Hz %s)\n",
             names[st.tv_setting], (unsigned)st.active_phi2_hz,
             (st.active_phi2_hz == 985248u) ? "PAL" : "NTSC");
        return;
    }

    if (strcmp(argv[1], "auto") == 0) {
        unipad_c64_pio_set_tv_standard(UNIPAD_TV_AUTO);
        logi("c64_tv -> auto\n");
    } else if (strcmp(argv[1], "pal") == 0) {
        unipad_c64_pio_set_tv_standard(UNIPAD_TV_PAL);
        logi("c64_tv -> pal (985,248 Hz)\n");
    } else if (strcmp(argv[1], "ntsc") == 0) {
        unipad_c64_pio_set_tv_standard(UNIPAD_TV_NTSC);
        logi("c64_tv -> ntsc (1,022,727 Hz)\n");
    } else {
        loge("Invalid c64_tv '%s'. Expected: auto | pal | ntsc\n", argv[1]);
    }
}

static void handle_cmd_bt_scan(size_t argc, char* argv[]) {
    if (argc < 2u) {
        logi("bt_scan = %s\n", uni_bt_is_scanning() ? "on" : "off");
        return;
    }

    if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0) {
        unipad_c64_platform_set_bt_scanning(true);
    } else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
        unipad_c64_platform_set_bt_scanning(false);
    } else {
        loge("Invalid bt_scan '%s'. Expected: on | off\n", argv[1]);
    }
}

static void dispatch_command_line(char* line) {
    char* argv[CONSOLE_MAX_ARGS] = {0};
    const size_t argc = tokenize_line(line, argv, CONSOLE_MAX_ARGS);
    if (argc == 0u) {
        return;
    }

    const char* cmd = argv[0];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_help();
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "version") == 0) {
        unipad_c64_platform_print_status();
    } else if (strcmp(cmd, "swap_ports") == 0 || strcmp(cmd, "swap") == 0) {
        unipad_c64_platform_swap_ports();
    } else if (strcmp(cmd, "gamepad_mode") == 0) {
        handle_cmd_gamepad_mode(argc, argv);
    } else if (strcmp(cmd, "c64_pot_mode") == 0) {
        handle_cmd_pot_mode(argc, argv);
    } else if (strcmp(cmd, "autofire_cps") == 0) {
        handle_cmd_autofire_cps(argc, argv);
    } else if (strcmp(cmd, "mouse_scale") == 0) {
        handle_cmd_mouse_scale(argc, argv);
    } else if (strcmp(cmd, "c64_tv") == 0) {
        handle_cmd_c64_tv(argc, argv);
    } else if (strcmp(cmd, "bt_scan") == 0) {
        handle_cmd_bt_scan(argc, argv);
    } else if (strcmp(cmd, "list_devices") == 0) {
        uni_hid_device_dump_all();
    } else if (strcmp(cmd, "list_keys") == 0) {
        uni_bt_list_keys_unsafe();
    } else if (strcmp(cmd, "del_keys") == 0) {
        uni_bt_del_keys_unsafe();
        logi("Erased all stored Bluetooth pairing keys.\n");
    } else if (strcmp(cmd, "properties") == 0) {
        uni_property_dump_all();
    } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
        logi("Rebooting Unipad C64...\n");
        uni_system_reboot();
    } else {
        loge("Unknown command '%s'. Type 'help' for available commands.\n", cmd);
    }
}

static void console_poll_timer_handler(btstack_timer_source_t* ts) {
    // Drain all pending non-blocking characters from USB CDC / UART0 RX ring buffer.
    for (unsigned budget = 0u; budget < 64u; budget++) {
        const int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }

        if (ch == '\r' || ch == '\n') {
            if (s_line_len > 0u) {
                putchar('\n');
                s_line_buf[s_line_len] = '\0';
                dispatch_command_line(s_line_buf);
                s_line_len = 0u;
            }
            continue;
        }

        if (ch == '\b' || ch == 0x7F) {
            if (s_line_len > 0u) {
                s_line_len--;
                printf("\b \b");
            }
            continue;
        }

        if (ch >= 0x20 && ch <= 0x7E && s_line_len < (CONSOLE_LINE_MAX_LEN - 1u)) {
            s_line_buf[s_line_len++] = (char)ch;
            putchar(ch);
        }
    }

    btstack_run_loop_set_timer(ts, CONSOLE_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

void unipad_c64_console_init(void) {
    s_line_len = 0u;
    btstack_run_loop_set_timer_handler(&s_console_timer, console_poll_timer_handler);
    btstack_run_loop_set_timer(&s_console_timer, CONSOLE_POLL_INTERVAL_MS);
    btstack_run_loop_add_timer(&s_console_timer);
}
