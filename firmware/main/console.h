/* Serial console (UART0, i.e. the board's USB-serial bridge) for seeding X credentials and testing.
   The console task never touches Swiss Ephemeris; x_cycle only signals the
   calc task through the callback. */
#pragma once

/* Called from the console task: run one X refresh-then-post cycle for the date
   `day_offset` days from today, in the calc task. */
typedef void (*console_x_cycle_cb_t)(int day_offset);

void console_start(console_x_cycle_cb_t on_x_cycle);
