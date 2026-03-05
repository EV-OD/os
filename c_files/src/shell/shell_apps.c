/* shell_apps.c - Embedded applications (rxt, term, paint) */

#include "shell/shell_apps.h"
#include "shell/shell_core.h"
#include "vfs.h"
#include "string.h"
#include "rosc.h"
#include "stdio.h"

static const char RXT_ROS[] =
    "// rxt -- RandomOS Text Editor\n"
    "// Usage:  rxt <filename>   (or launch from desktop with no arg)\n"
    "//  Ctrl+S = Save / Save As    Ctrl+Q = Quit\n"
    "//\n"
    "// Install with:  setup\n"
    "// Then run:      rxt myfile.txt\n"
    "\n"
    "import gui\n"
    "import io\n"
    "\n"
    "fn main() {\n"
    "    let fname: i32 = getarg(1)\n"
    "    let mut has_name: i32 = 0\n"
    "    if fname != 0 {\n"
    "        has_name = 1\n"
    "    }\n"
    "    let tb: i32 = io_open(fname)\n"
    "    let win: i32 = gui_window(50, 30, 640, 420, \"rxt editor\")\n"
    "    let C_BG: i32 = 0x1E1E1E\n"
    "    let C_FG: i32 = 0xD4D4D4\n"
    "    let C_GUT: i32 = 0x252526\n"
    "    let C_HL: i32 = 0x2A4555\n"
    "    let C_NUM: i32 = 0x666666\n"
    "    let C_STATBG: i32 = 0x007ACC\n"
    "    let C_WHITE: i32 = 0xFFFFFF\n"
    "    let VISIBLE: i32 = 34\n"
    "    let STAT_Y: i32 = 340\n"
    "    let mut scroll: i32 = 0\n"
    "    let mut running: i32 = 1\n"
    "    while running == 1 {\n"
    "        gui_fill(win, C_BG)\n"
    "        gui_pen(win, C_GUT)\n"
    "        gui_fill_rect(win, 0, 0, 40, STAT_Y)\n"
    "        let cpos: i32 = io_cursor(tb)\n"
    "        let cline: i32 = cpos & 0xFFFF\n"
    "        let ccol: i32 = (cpos >> 16) & 0xFFFF\n"
    "        let mut i: i32 = 0\n"
    "        while i < VISIBLE {\n"
    "            let ln: i32 = scroll + i\n"
    "            let y: i32 = i * 10\n"
    "            if ln == cline {\n"
    "                gui_pen(win, C_HL)\n"
    "                gui_fill_rect(win, 0, y, 640, 10)\n"
    "            }\n"
    "            gui_text(win, 2, y, io_itoa(tb, ln + 1), C_NUM)\n"
    "            gui_text(win, 42, y, io_getline(tb, ln), C_FG)\n"
    "            i = i + 1\n"
    "        }\n"
    "        let cx: i32 = 42 + ccol * 8\n"
    "        let cy: i32 = (cline - scroll) * 10\n"
    "        gui_pen(win, C_WHITE)\n"
    "        gui_fill_rect(win, cx, cy, 2, 8)\n"
    "        gui_pen(win, C_STATBG)\n"
    "        gui_fill_rect(win, 0, STAT_Y, 640, 20)\n"
    "        gui_text(win, 4, STAT_Y + 4, \"^S Save  ^Q Quit  Ln:\", C_WHITE)\n"
    "        gui_text(win, 172, STAT_Y + 4, io_itoa(tb, cline + 1), C_WHITE)\n"
    "        gui_text(win, 196, STAT_Y + 4, \"Col:\", C_WHITE)\n"
    "        gui_text(win, 228, STAT_Y + 4, io_itoa(tb, ccol + 1), C_WHITE)\n"
    "        gui_flush(win)\n"
    "        let k: i32 = gui_wait(win)\n"
    "        if k == -1 {\n"
    "            running = 0\n"
    "        } else if k == 17 {\n"
    "            running = 0\n"
    "        } else if k == 19 {\n"
    "            if has_name == 0 {\n"
    "                io_saveas(tb, win)\n"
    "                has_name = 1\n"
    "            } else {\n"
    "                io_save(tb)\n"
    "            }\n"
    "        } else {\n"
    "            io_key(tb, k)\n"
    "            mut more: i32 = gui_poll(win)\n"
    "            while more > 0 {\n"
    "                if more == 17 {\n"
    "                    running = 0\n"
    "                } else if more == 19 {\n"
    "                    if has_name == 0 {\n"
    "                        io_saveas(tb, win)\n"
    "                        has_name = 1\n"
    "                    } else {\n"
    "                        io_save(tb)\n"
    "                    }\n"
    "                } else {\n"
    "                    io_key(tb, more)\n"
    "                }\n"
    "                more = gui_poll(win)\n"
    "            }\n"
    "            let nc: i32 = io_cursor(tb) & 0xFFFF\n"
    "            if nc < scroll {\n"
    "                scroll = nc\n"
    "            }\n"
    "            if nc >= scroll + VISIBLE {\n"
    "                scroll = nc - VISIBLE + 1\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    io_close(tb)\n"
    "    gui_close(win)\n"
    "}\n";

/* =========================================================================
 * OS directory structure creation
 * ========================================================================= */

static const char TERM_ROS[] =
    "// term -- spawn a new GUI terminal window\n"
    "// Install with: setup\n"
    "// Run from desktop or shell: term\n"
    "import gui\n"
    "fn main() {\n"
    "    spawn_term()\n"
    "}\n";

/* =========================================================================
 * paint app source (embedded, installed by 'setup' command)
 *
 * Controls:
 *   Left-click + drag = draw with pencil or eraser
 *   P               = pencil tool
 *   E               = eraser tool
 *   S               = cycle brush size (2,4,8,12)
 *   C               = clear canvas
 *   1-8             = pick colour from palette
 *   Q / close       = quit
 * ========================================================================= */
static const char PAINT_ROS[] =
    "// paint -- ROX Paint Application\n"
    "// Install with: setup   then run: paint\n"
    "// Controls: P=pencil E=eraser S=size C=clear 1-8=color Q=quit\n"
    "import gui\n"
    "\n"
    "// ── tiny helper: draw a filled circle (thick dot) ─────────────────\n"
    "fn dot(win: i32, x: i32, y: i32, r: i32, col: i32) {\n"
    "    gui_fill_circle(win, x, y, r, col)\n"
    "}\n"
    "\n"
    "// ── draw a thick line from (x0,y0) to (x1,y1) using circles ───────\n"
    "fn thick_line(win: i32, x0: i32, y0: i32, x1: i32, y1: i32, r: i32, col: i32) {\n"
    "    let mut dx: i32 = x1 - x0\n"
    "    let mut dy: i32 = y1 - y0\n"
    "    if dx < 0 { dx = 0 - dx }\n"
    "    if dy < 0 { dy = 0 - dy }\n"
    "    let steps: i32 = dx + dy\n"
    "    if steps == 0 {\n"
    "        dot(win, x0, y0, r, col)\n"
    "        return\n"
    "    }\n"
    "    let mut i: i32 = 0\n"
    "    while i <= steps {\n"
    "        let px: i32 = x0 + (x1 - x0) * i / steps\n"
    "        let py: i32 = y0 + (y1 - y0) * i / steps\n"
    "        dot(win, px, py, r, col)\n"
    "        i = i + 1\n"
    "    }\n"
    "}\n"
    "\n"
    "// ── colour swatch helper ──────────────────────────────────────────\n"
    "fn swatch(win: i32, idx: i32, col: i32, sel: i32) {\n"
    "    let sx: i32 = 6 + idx * 26\n"
    "    let sy: i32 = 6\n"
    "    gui_pen(win, col)\n"
    "    gui_fill_rect(win, sx, sy, 22, 22)\n"
    "    if sel == 1 {\n"
    "        gui_pen(win, 0xFFFFFF)\n"
    "        gui_rect(win, sx - 1, sy - 1, 24, 24)\n"
    "    } else {\n"
    "        gui_pen(win, 0x000000)\n"
    "        gui_rect(win, sx, sy, 22, 22)\n"
    "    }\n"
    "}\n"
    "\n"
    "// ── draw the toolbar ─────────────────────────────────────────────\n"
    "fn draw_toolbar(win: i32, tool: i32, brush: i32, cur_col: i32) {\n"
    "    gui_pen(win, 0x2B2B2B)\n"
    "    gui_fill_rect(win, 0, 0, 640, 36)\n"
    "    gui_pen(win, 0x111111)\n"
    "    gui_line(win, 0, 36, 640, 36)\n"
    "    // Swatches: 8 colours\n"
    "    swatch(win, 0, 0x000000, cur_col == 0)\n"
    "    swatch(win, 1, 0xFFFFFF, cur_col == 1)\n"
    "    swatch(win, 2, 0xFF3B3B, cur_col == 2)\n"
    "    swatch(win, 3, 0x3BFF5A, cur_col == 3)\n"
    "    swatch(win, 4, 0x3B8FFF, cur_col == 4)\n"
    "    swatch(win, 5, 0xFFD83B, cur_col == 5)\n"
    "    swatch(win, 6, 0xFF8C3B, cur_col == 6)\n"
    "    swatch(win, 7, 0xC23BFF, cur_col == 7)\n"
    "    // Tool labels\n"
    "    let tl_x: i32 = 224\n"
    "    if tool == 0 {\n"
    "        gui_pen(win, 0x4F8EF7)\n"
    "        gui_fill_rect(win, tl_x, 4, 52, 28)\n"
    "    }\n"
    "    gui_text(win, tl_x + 6, 12, \"P:Pencil\", 0xFFFFFF)\n"
    "    if tool == 1 {\n"
    "        gui_pen(win, 0x4F8EF7)\n"
    "        gui_fill_rect(win, tl_x + 58, 4, 52, 28)\n"
    "    }\n"
    "    gui_text(win, tl_x + 64, 12, \"E:Eraser\", 0xFFFFFF)\n"
    "    // Brush size indicator\n"
    "    gui_text(win, tl_x + 124, 8, \"S:Size\", 0xCCCCCC)\n"
    "    gui_fill_circle(win, tl_x + 170 + brush * 2, 18, brush + 1, 0xFFFFFF)\n"
    "    // Clear button\n"
    "    gui_pen(win, 0xFF4444)\n"
    "    gui_fill_rect(win, 560, 4, 72, 28)\n"
    "    gui_text(win, 568, 12, \"C:Clear\", 0xFFFFFF)\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    let WIN_W: i32 = 640\n"
    "    let WIN_H: i32 = 480\n"
    "    let TOOLBAR_H: i32 = 38\n"
    "    let CANVAS_BG: i32 = 0xF5F5F5\n"
    "    let win: i32 = gui_window(40, 30, WIN_W, WIN_H, \"ROX Paint\")\n"
    "\n"
    "    // Palette\n"
    "    let pal0: i32 = 0x000000\n"
    "    let pal1: i32 = 0xFFFFFF\n"
    "    let pal2: i32 = 0xFF3B3B\n"
    "    let pal3: i32 = 0x3BFF5A\n"
    "    let pal4: i32 = 0x3B8FFF\n"
    "    let pal5: i32 = 0xFFD83B\n"
    "    let pal6: i32 = 0xFF8C3B\n"
    "    let pal7: i32 = 0xC23BFF\n"
    "\n"
    "    // Brush sizes\n"
    "    let bsz0: i32 = 2\n"
    "    let bsz1: i32 = 4\n"
    "    let bsz2: i32 = 8\n"
    "    let bsz3: i32 = 12\n"
    "\n"
    "    mut tool: i32 = 0\n"
    "    mut brush_idx: i32 = 0\n"
    "    mut col_idx: i32 = 0\n"
    "    mut draw_col: i32 = 0x000000\n"
    "    mut brush_r: i32 = 2\n"
    "    mut running: i32 = 1\n"
    "    mut prev_x: i32 = -1\n"
    "    mut prev_y: i32 = -1\n"
    "    mut was_down: i32 = 0\n"
    "\n"
    "    // Initial canvas fill\n"
    "    gui_fill(win, CANVAS_BG)\n"
    "    draw_toolbar(win, tool, brush_r, col_idx)\n"
    "    gui_flush(win)\n"
    "\n"
    "    while running == 1 {\n"
    "        // ── poll keyboard (non-blocking) ────────────────────────────\n"
    "        let k: i32 = gui_poll(win)\n"
    "        if k == -1 {\n"
    "            running = 0\n"
    "        }\n"
    "        if k == 113 { running = 0 }\n"
    "        if k == 112 { tool = 0 }\n"
    "        if k == 101 { tool = 1 }\n"
    "        if k == 115 {\n"
    "            brush_idx = brush_idx + 1\n"
    "            if brush_idx > 3 { brush_idx = 0 }\n"
    "            if brush_idx == 0 { brush_r = bsz0 }\n"
    "            if brush_idx == 1 { brush_r = bsz1 }\n"
    "            if brush_idx == 2 { brush_r = bsz2 }\n"
    "            if brush_idx == 3 { brush_r = bsz3 }\n"
    "        }\n"
    "        if k == 99 {\n"
    "            gui_fill(win, CANVAS_BG)\n"
    "            draw_toolbar(win, tool, brush_r, col_idx)\n"
    "            gui_flush(win)\n"
    "        }\n"
    "        // Colour select: keys 1-8\n"
    "        if k == 49 { col_idx = 0  draw_col = pal0 }\n"
    "        if k == 50 { col_idx = 1  draw_col = pal1 }\n"
    "        if k == 51 { col_idx = 2  draw_col = pal2 }\n"
    "        if k == 52 { col_idx = 3  draw_col = pal3 }\n"
    "        if k == 53 { col_idx = 4  draw_col = pal4 }\n"
    "        if k == 54 { col_idx = 5  draw_col = pal5 }\n"
    "        if k == 55 { col_idx = 6  draw_col = pal6 }\n"
    "        if k == 56 { col_idx = 7  draw_col = pal7 }\n"
    "\n"
    "        // ── sample mouse state ──────────────────────────────────────\n"
    "        let ms: i32 = gui_mouse(win)\n"
    "        let mx: i32 = ms & 0xFFF\n"
    "        let my: i32 = (ms >> 12) & 0xFFF\n"
    "        let lbtn: i32 = (ms >> 24) & 1\n"
    "\n"
    "        // ── toolbar swatch click ────────────────────────────────────\n"
    "        if lbtn == 1 {\n"
    "            if my >= 6 {\n"
    "                if my < 28 {\n"
    "                    if mx >= 6  { if mx < 28  { col_idx = 0  draw_col = pal0 } }\n"
    "                    if mx >= 32 { if mx < 54  { col_idx = 1  draw_col = pal1 } }\n"
    "                    if mx >= 58 { if mx < 80  { col_idx = 2  draw_col = pal2 } }\n"
    "                    if mx >= 84 { if mx < 106 { col_idx = 3  draw_col = pal3 } }\n"
    "                    if mx >= 110 { if mx < 132 { col_idx = 4  draw_col = pal4 } }\n"
    "                    if mx >= 136 { if mx < 158 { col_idx = 5  draw_col = pal5 } }\n"
    "                    if mx >= 162 { if mx < 184 { col_idx = 6  draw_col = pal6 } }\n"
    "                    if mx >= 188 { if mx < 210 { col_idx = 7  draw_col = pal7 } }\n"
    "                    // P button => pencil\n"
    "                    if mx >= 224 { if mx < 276 { tool = 0 } }\n"
    "                    // E button => eraser\n"
    "                    if mx >= 282 { if mx < 334 { tool = 1 } }\n"
    "                    // C button => clear\n"
    "                    if mx >= 560 {\n"
    "                        gui_fill(win, CANVAS_BG)\n"
    "                        draw_toolbar(win, tool, brush_r, col_idx)\n"
    "                        gui_flush(win)\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "\n"
    "        // ── drawing in canvas area ──────────────────────────────────\n"
    "        if my > TOOLBAR_H {\n"
    "            if lbtn == 1 {\n"
    "                let mut ink: i32 = draw_col\n"
    "                if tool == 1 { ink = CANVAS_BG }\n"
    "                if was_down == 1 {\n"
    "                    thick_line(win, prev_x, prev_y, mx, my, brush_r, ink)\n"
    "                } else {\n"
    "                    dot(win, mx, my, brush_r, ink)\n"
    "                }\n"
    "                draw_toolbar(win, tool, brush_r, col_idx)\n"
    "                gui_flush(win)\n"
    "                prev_x = mx\n"
    "                prev_y = my\n"
    "                was_down = 1\n"
    "            } else {\n"
    "                prev_x = -1\n"
    "                prev_y = -1\n"
    "                was_down = 0\n"
    "            }\n"
    "        } else {\n"
    "            if lbtn == 0 { was_down = 0 }\n"
    "        }\n"
    "    }\n"
    "    gui_close(win)\n"
    "}\n";

int cmd_setup(int argc, char **argv)
{
    (void)argc; (void)argv;
    int fd, ret;

    puts_color("[setup] Writing rxt source...\n", COLOR_LIGHT_CYAN);

    fd = vfs_open("/tmp/rxt.ros", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("[setup] error: cannot write /tmp/rxt.ros\n", COLOR_LIGHT_RED);
        return -1;
    }
    vfs_write(fd, RXT_ROS, strlen(RXT_ROS));
    vfs_close(fd);

    puts_color("[setup] Compiling rxt.ros -> /bin/rxt.rox ...\n", COLOR_LIGHT_CYAN);
    ret = rosc_compile("/tmp/rxt.ros", "/bin/rxt.rox", 1);
    if (ret < 0) {
        puts_color("[setup] error: compilation failed\n", COLOR_LIGHT_RED);
        return -1;
    }

    vfs_unlink("/tmp/rxt.ros");
    puts_color("[setup] rxt installed at /bin/rxt.rox\n", COLOR_LIGHT_GREEN);
    puts_color("  Usage: rxt <filename>\n", COLOR_WHITE);

    /* ---- Install term.rox ---- */
    puts_color("[setup] Writing term source...\n", COLOR_LIGHT_CYAN);
    fd = vfs_open("/tmp/term.ros", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("[setup] warning: cannot write /tmp/term.ros\n", COLOR_LIGHT_RED);
        return 0; /* rxt succeeded; non-fatal */
    }
    vfs_write(fd, TERM_ROS, strlen(TERM_ROS));
    vfs_close(fd);

    puts_color("[setup] Compiling term.ros -> /bin/term.rox ...\n", COLOR_LIGHT_CYAN);
    ret = rosc_compile("/tmp/term.ros", "/bin/term.rox", 1);
    if (ret < 0) {
        puts_color("[setup] warning: term compilation failed\n", COLOR_LIGHT_RED);
        return 0;
    }
    vfs_unlink("/tmp/term.ros");
    puts_color("[setup] term installed at /bin/term.rox\n", COLOR_LIGHT_GREEN);
    puts_color("  Usage: term   (opens a new terminal window)\n", COLOR_WHITE);

    /* ---- Install paint.rox ---- */
    puts_color("[setup] Writing paint source...\n", COLOR_LIGHT_CYAN);
    fd = vfs_open("/tmp/paint.ros", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("[setup] warning: cannot write /tmp/paint.ros\n", COLOR_LIGHT_RED);
        return 0; /* rxt+term succeeded; non-fatal */
    }
    vfs_write(fd, PAINT_ROS, strlen(PAINT_ROS));
    vfs_close(fd);

    puts_color("[setup] Compiling paint.ros -> /bin/paint.rox ...\n", COLOR_LIGHT_CYAN);
    ret = rosc_compile("/tmp/paint.ros", "/bin/paint.rox", 1);
    if (ret < 0) {
        puts_color("[setup] warning: paint compilation failed\n", COLOR_LIGHT_RED);
        return 0;
    }
    vfs_unlink("/tmp/paint.ros");
    puts_color("[setup] paint installed at /bin/paint.rox\n", COLOR_LIGHT_GREEN);
    puts_color("  Usage: paint\n", COLOR_WHITE);
    puts_color("  Controls: P=pencil  E=eraser  S=size  C=clear  1-8=colour  Q=quit\n",
               COLOR_LIGHT_GREY);
    return 0;
}
