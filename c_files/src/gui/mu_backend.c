/* =========================================================================
 * gui/mu_backend.c – microui rendering backend for RandomOS
 *
 * Bridges microui to the OS window-manager:
 *   - mu_Context is heap-allocated (~280 KB — far too large for the 8 KB
 *     kernel stack).
 *   - Keyboard input arrives through the WM on_key callback → ring buffer,
 *     so we never steal events from the compositor.
 *   - Mouse position is read via mouse_get() (non-consuming), with button
 *     edges tracked in the main loop.
 *   - Rendering goes to the window's offscreen canvas; the compositor's
 *     regular paint cycle blits it to the framebuffer.
 *   - On exit the process sets PROC_DEAD and halts, so process_wait()
 *     in the shell unblocks correctly.
 * ========================================================================= */

#include "gui/mu_backend.h"
#include "microui.h"
#include "gui/wm.h"
#include "gui/canvas.h"
#include "gui/fb.h"
#include "gui/font.h"
#include "gui/color.h"
#include "gui/mouse.h"
#include "keyboard.h"
#include "string.h"
#include "log.h"
#include "pit.h"
#include "process.h"
#include "sched.h"
#include "kheap.h"

/* -------------------------------------------------------------------------
 * Text-measurement callbacks (8×8 bitmap font)
 * ------------------------------------------------------------------------- */

static int mu_text_width_cb(mu_Font font, const char *str, int len)
{
    (void)font;
    if (len < 0) len = (int)strlen(str);
    return len * CNV_FONT_W;
}

static int mu_text_height_cb(mu_Font font)
{
    (void)font;
    return CNV_FONT_H;
}

/* -------------------------------------------------------------------------
 * Keyboard ring buffer — filled by the WM on_key callback
 *   (runs in the compositor process context)
 * ------------------------------------------------------------------------- */

#define KEY_BUF_SZ  32
static volatile char s_keys[KEY_BUF_SZ];
static volatile int  s_khead, s_ktail;

static void on_key_cb(wm_window_t *win, char c)
{
    (void)win;
    int next = (s_khead + 1) % KEY_BUF_SZ;
    if (next != s_ktail) {
        s_keys[s_khead] = c;
        s_khead = next;
    }
}

static int key_pop(void)
{
    if (s_ktail == s_khead) return 0;
    char c = s_keys[s_ktail];
    s_ktail = (s_ktail + 1) % KEY_BUF_SZ;
    return (unsigned char)c;
}

/* -------------------------------------------------------------------------
 * Render microui commands → canvas
 * ------------------------------------------------------------------------- */

static void mu_render(mu_Context *ctx, wm_window_t *win)
{
    unsigned int *buf = win->canvas;
    int cw = win->w;
    int ch = win->h - TITLE_BAR_H;
    mu_Command *cmd = (void *)0;

    while (mu_next_command(ctx, &cmd)) {
        switch (cmd->type) {

        case MU_COMMAND_RECT: {
            mu_Color c = cmd->rect.color;
            cnv_fill_rect(buf, cw, ch,
                          cmd->rect.rect.x, cmd->rect.rect.y,
                          cmd->rect.rect.w, cmd->rect.rect.h,
                          COLOR_RGB(c.r, c.g, c.b));
            break;
        }

        case MU_COMMAND_TEXT: {
            mu_Color c = cmd->text.color;
            cnv_draw_str(buf, cw, ch,
                         cmd->text.pos.x, cmd->text.pos.y,
                         cmd->text.str,
                         COLOR_RGB(c.r, c.g, c.b), COLOR_TRANSPARENT);
            break;
        }

        case MU_COMMAND_ICON: {
            mu_Rect r  = cmd->icon.rect;
            mu_Color c = cmd->icon.color;
            int cx = r.x + (r.w - CNV_FONT_W) / 2;
            int cy = r.y + (r.h - CNV_FONT_H) / 2;
            const char *g = "?";
            if (cmd->icon.id == MU_ICON_CLOSE)     g = "x";
            if (cmd->icon.id == MU_ICON_CHECK)     g = "*";
            if (cmd->icon.id == MU_ICON_COLLAPSED) g = ">";
            if (cmd->icon.id == MU_ICON_EXPANDED)  g = "v";
            cnv_draw_str(buf, cw, ch, cx, cy, g,
                         COLOR_RGB(c.r, c.g, c.b), COLOR_TRANSPARENT);
            break;
        }

        case MU_COMMAND_CLIP:
            /* Canvas clips to bounds automatically via cnv_* functions */
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * Input: mouse via mouse_get(), keyboard via WM ring buffer
 * ------------------------------------------------------------------------- */

static unsigned char s_prev_btns;

static void mu_feed_input(mu_Context *ctx, wm_window_t *win)
{
    /* --- Mouse (non-consuming, global state) --- */
    mouse_state_t ms = mouse_get();
    int rx = ms.x - win->x;
    int ry = ms.y - (win->y + TITLE_BAR_H);
    int cw = win->w;
    int ch = win->h - TITLE_BAR_H;

    int inside = (rx >= 0 && rx < cw && ry >= 0 && ry < ch);
    int active = inside || (ms.buttons & 0x03);

    if (active) {
        mu_input_mousemove(ctx, rx, ry);

        if ((ms.buttons & 1) && !(s_prev_btns & 1))
            mu_input_mousedown(ctx, rx, ry, MU_MOUSE_LEFT);
        if (!(ms.buttons & 1) && (s_prev_btns & 1))
            mu_input_mouseup(ctx, rx, ry, MU_MOUSE_LEFT);

        if ((ms.buttons & 2) && !(s_prev_btns & 2))
            mu_input_mousedown(ctx, rx, ry, MU_MOUSE_RIGHT);
        if (!(ms.buttons & 2) && (s_prev_btns & 2))
            mu_input_mouseup(ctx, rx, ry, MU_MOUSE_RIGHT);
    }
    s_prev_btns = ms.buttons;

    /* --- Keyboard (from WM callback ring buffer) --- */
    int c;
    while ((c = key_pop()) != 0) {
        if (c == '\b')
            mu_input_keydown(ctx, MU_KEY_BACKSPACE);
        else if (c == '\n' || c == '\r')
            mu_input_keydown(ctx, MU_KEY_RETURN);
        else {
            char txt[2];
            txt[0] = (char)c;
            txt[1] = '\0';
            mu_input_text(ctx, txt);
        }
    }
}

/* -------------------------------------------------------------------------
 * Demo UI definition
 * ------------------------------------------------------------------------- */

static int     s_counter;
static int     s_checkbox;
static mu_Real s_slider;
static char    s_textbuf[64];
static char    s_logbuf[256];
static int     s_loglen;

/* Layout width tables (must outlive the mu_layout_row call) */
static int w_full[]  = { -1 };
static int w_btn3[]  = { 90, 90, -1 };
static int w_pair[]  = { 100, -1 };
static int w_inp[]   = { 80, -1 };

static void log_msg(const char *msg)
{
    int ml = (int)strlen(msg);
    if (s_loglen + ml + 1 > (int)sizeof(s_logbuf) - 1) {
        s_loglen = 0;
        s_logbuf[0] = '\0';
    }
    memcpy(s_logbuf + s_loglen, msg, ml);
    s_loglen += ml;
    s_logbuf[s_loglen++] = '\n';
    s_logbuf[s_loglen] = '\0';
}

static void demo_ui(mu_Context *ctx, int cw, int ch)
{
    if (mu_begin_window_ex(ctx, "Demo",
            mu_rect(5, 5, cw - 10, ch - 10),
            MU_OPT_NOCLOSE | MU_OPT_NOTITLE | MU_OPT_NORESIZE))
    {
        /* --- Header --- */
        mu_layout_row(ctx, 1, w_full, 0);
        mu_label(ctx, "microui on RandomOS");

        /* --- Buttons row --- */
        mu_layout_row(ctx, 3, w_btn3, 0);

        if (mu_button(ctx, "Click me")) {
            s_counter++;
            char msg[32];
            sprintf(msg, "Clicked %d times", s_counter);
            log_msg(msg);
        }

        if (mu_button(ctx, "Reset")) {
            s_counter  = 0;
            s_slider   = 50.0f;
            s_checkbox = 0;
            log_msg("Reset!");
        }

        {
            char buf[32];
            sprintf(buf, "Count: %d", s_counter);
            mu_label(ctx, buf);
        }

        /* --- Checkbox --- */
        mu_layout_row(ctx, 1, w_full, 0);
        mu_checkbox(ctx, "Enable feature", &s_checkbox);

        /* --- Slider --- */
        mu_layout_row(ctx, 2, w_pair, 0);
        mu_label(ctx, "Progress:");
        mu_slider(ctx, &s_slider, 0.0f, 100.0f);

        /* --- Text input --- */
        mu_layout_row(ctx, 2, w_inp, 0);
        mu_label(ctx, "Input:");
        mu_textbox(ctx, s_textbuf, sizeof(s_textbuf));

        /* --- Log panel --- */
        mu_layout_row(ctx, 1, w_full, 80);
        mu_begin_panel(ctx, "Log");
        mu_layout_row(ctx, 1, w_full, 0);
        mu_text(ctx, s_logbuf);
        mu_end_panel(ctx);

        mu_end_window(ctx);
    }
}

/* -------------------------------------------------------------------------
 * mu_demo_run – kernel process entry point
 *
 * Launched via:  process_create("mu_demo", mu_demo_run, 0)
 * The shell blocks in process_wait() until we set PROC_DEAD.
 * ------------------------------------------------------------------------- */

void mu_demo_run(void)
{
    process_t *self = sched_current();

    /* --- Heap-allocate mu_Context (~280 KB, kernel stack is only 8 KB) --- */
    mu_Context *ctx = (mu_Context *)kmalloc(sizeof(mu_Context));
    if (!ctx) {
        log_error("[mu_demo] kmalloc(%d) failed", (int)sizeof(mu_Context));
        goto die;
    }

    /* --- Create WM window --- */
    wm_window_t *win = wm_create(80, 50, 420, 340, "microui Demo");
    if (!win) {
        log_error("[mu_demo] wm_create failed");
        kfree(ctx);
        goto die;
    }
    wm_alloc_canvas(win);
    if (!win->canvas) {
        log_error("[mu_demo] canvas alloc failed");
        wm_destroy(win);
        kfree(ctx);
        goto die;
    }
    win->owner_pid = self ? (int)self->pid : 0;

    int cw = win->w;
    int ch = win->h - TITLE_BAR_H;

    /* --- Register WM key callback (compositor will deliver events) --- */
    win->on_key = on_key_cb;

    /* --- Reset all state (may be re-used across runs) --- */
    s_khead = s_ktail = 0;
    s_prev_btns = 0;
    s_counter   = 0;
    s_checkbox  = 0;
    s_slider    = 50.0f;
    s_loglen    = 0;
    s_logbuf[0] = '\0';
    memcpy(s_textbuf, "Hello OS!", 10);

    /* --- Init microui --- */
    mu_init(ctx);
    ctx->text_width  = mu_text_width_cb;
    ctx->text_height = mu_text_height_cb;

    log_msg("microui started");
    log_msg("Click buttons!");
    log_info("[mu_demo] running (%dx%d client)", cw, ch);

    /* --- Main loop (~30 fps) --- */
    unsigned int last_tick = pit_get_ticks();

    for (;;) {
        if (self && self->killed) break;

        /* Throttle: sleep until next frame */
        unsigned int now = pit_get_ticks();
        if ((now - last_tick) < 3u) {
            __asm__ volatile("sti; hlt");
            continue;
        }
        last_tick = now;

        /* Input → microui */
        mu_feed_input(ctx, win);

        /* microui frame */
        mu_begin(ctx);
        demo_ui(ctx, cw, ch);
        mu_end(ctx);

        /* Render to canvas */
        cnv_clear(win->canvas, cw, ch, COLOR_RGB(0x2E, 0x2E, 0x2E));
        mu_render(ctx, win);

        /* Mark dirty; the compositor paints every frame via
         * wm_invalidate_all → wm_paint_all → fb_flush. */
        win->dirty = 1;
    }

    /* --- Cleanup --- */
    wm_destroy(win);
    wm_invalidate_all();
    kfree(ctx);
    log_info("[mu_demo] exited");

die:
    /* Kernel processes have no valid return address on the stack.
     * Mark ourselves dead so process_wait() in the shell unblocks,
     * then halt forever until the scheduler reaps us. */
    if (self) {
        self->exit_status = 0;
        self->state       = PROC_DEAD;
    }
    for (;;) __asm__ volatile("hlt");
}
