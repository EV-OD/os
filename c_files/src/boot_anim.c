/* =========================================================================
 * boot_anim.c – Text-based boot loading animation for RabinOS
 *
 * Displays an ASCII-art logo and sequenced loading messages with a final
 * progress bar.  Timing is based on PIT ticks (10 ms each).
 *
 * The animation is purely cosmetic – subsystems are already initialised
 * by the time this runs.
 * ========================================================================= */

#include "boot_anim.h"
#include "stdio.h"
#include "string.h"
#include "pit.h"

/* -------------------------------------------------------------------------
 * Busy-wait delay (ms). Works before the scheduler is running.
 * ------------------------------------------------------------------------- */
static void delay_ms(unsigned int ms)
{
    unsigned int ticks_needed = ms / PIT_TICK_MS;
    if (ticks_needed == 0) ticks_needed = 1;
    unsigned int start = pit_get_ticks();
    while ((pit_get_ticks() - start) < ticks_needed) {
        /* spin */
    }
}

/* =========================================================================
 * boot_animation
 * ========================================================================= */
void boot_animation(void)
{
    fb_clear();
    cursor_move_home();

    /* ---- ASCII art logo ---- */
    puts("\n\n\n");
    puts_color("   ____       _     _        ___  ____  \n", COLOR_LIGHT_CYAN);
    puts_color("  |  _ \\ __ _| |__ (_)_ __  / _ \\/ ___| \n", COLOR_LIGHT_CYAN);
    puts_color("  | |_) / _` | '_ \\| | '_ \\| | | \\___ \\ \n", COLOR_LIGHT_CYAN);
    puts_color("  |  _ < (_| | |_) | | | | | |_| |___) |\n", COLOR_LIGHT_CYAN);
    puts_color("  |_| \\_\\__,_|_.__/|_|_| |_|\\___/|____/ \n", COLOR_LIGHT_CYAN);
    puts("\n");
    puts_color("           v0.1.0 - i386 kernel\n\n", COLOR_DARK_GREY);

    /* ---- Loading steps with animated dots ---- */
    static const char *steps[] = {
        "Initialising memory",
        "Mounting filesystem",
        "Creating directories",
        "Loading shell",
        "Starting services",
        (void *)0
    };

    for (int s = 0; steps[s] != (void *)0; s++) {
        puts_color("  [", COLOR_DARK_GREY);
        puts_color(" ", COLOR_DARK_GREY);
        puts_color("]  ", COLOR_DARK_GREY);
        puts_color(steps[s], COLOR_LIGHT_GREY);

        /* Animate dots */
        for (int dot = 0; dot < 3; dot++) {
            delay_ms(200);
            putchar_color('.', COLOR_LIGHT_GREY);
        }
        delay_ms(150);

        /* Go back and replace the space with a green checkmark */
        /* We can't easily go back, so we print "done" at the end */
        puts_color(" done\n", COLOR_LIGHT_GREEN);
    }

    /* ---- Progress bar ---- */
    puts("\n  ");
    putchar('[');
    for (unsigned int i = 0; i < 40; i++) {
        delay_ms(20);
        putchar_color('#', COLOR_LIGHT_GREEN);
    }
    putchar(']');
    puts_color("  100%\n", COLOR_WHITE);

    puts("\n");
    puts_color("  [", COLOR_DARK_GREY);
    puts_color("OK", COLOR_LIGHT_GREEN);
    puts_color("]  ", COLOR_DARK_GREY);
    puts_color("System ready.\n\n", COLOR_WHITE);

    delay_ms(800);

    /* Clear screen before showing shell */
    fb_clear();
    cursor_move_home();
}
