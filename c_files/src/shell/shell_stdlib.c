/* shell_stdlib.c - RandomOS Standard Library (gui.ros, math.ros, io.ros) */

#include "shell/shell_stdlib.h"
#include "vfs.h"
#include "string.h"
#include "log.h"

static const char LIB_GUI_ROS[] =
    "// /lib/ros/gui.ros -- RandomOS GUI Library v2\n"
    "// Usage: import gui\n"
    "fn COL_BLACK()   -> i32 { return 0x000000 }\n"
    "fn COL_WHITE()   -> i32 { return 0xFFFFFF }\n"
    "fn COL_RED()     -> i32 { return 0xFF4040 }\n"
    "fn COL_GREEN()   -> i32 { return 0x40CC55 }\n"
    "fn COL_BLUE()    -> i32 { return 0x4488FF }\n"
    "fn COL_YELLOW()  -> i32 { return 0xFFDD00 }\n"
    "fn COL_ORANGE()  -> i32 { return 0xFF8800 }\n"
    "fn COL_CYAN()    -> i32 { return 0x00CCDD }\n"
    "fn COL_PURPLE()  -> i32 { return 0x9956E8 }\n"
    "fn COL_GRAY()    -> i32 { return 0x888888 }\n"
    "fn COL_LGRAY()   -> i32 { return 0xCCCCCC }\n"
    "fn COL_DGRAY()   -> i32 { return 0x444444 }\n"
    "fn COL_BG()      -> i32 { return 0x0D1117 }\n"
    "fn COL_SURFACE() -> i32 { return 0x161B22 }\n"
    "fn COL_CARD()    -> i32 { return 0x21262D }\n"
    "fn COL_ACCENT()  -> i32 { return 0x4F8EF7 }\n"
    "fn COL_HOVER()   -> i32 { return 0x6BA5FF }\n"
    "fn COL_PRESS()   -> i32 { return 0x2E6ADF }\n"
    "fn COL_BORDER()  -> i32 { return 0x30363D }\n"
    "fn COL_TEXT()    -> i32 { return 0xE6EDF3 }\n"
    "fn COL_SUBTEXT() -> i32 { return 0x8B949E }\n"
    "fn COL_DANGER()  -> i32 { return 0xFF4D5A }\n"
    "fn COL_SUCCESS() -> i32 { return 0x3FB950 }\n"
    "fn COL_WARN()    -> i32 { return 0xD29922 }\n"
    "fn EV_CLOSE()    -> i32 { return -1 }\n"
    "fn ui_min(a: i32, b: i32) -> i32 { if a < b { return a } return b }\n"
    "fn ui_max(a: i32, b: i32) -> i32 { if a > b { return a } return b }\n"
    "fn ui_clamp(v: i32, lo: i32, hi: i32) -> i32 { if v < lo { return lo } if v > hi { return hi } return v }\n"
    "fn ui_mx(ms: i32) -> i32 { return ms & 0xFFF }\n"
    "fn ui_my(ms: i32) -> i32 { return (ms >> 12) & 0xFFF }\n"
    "fn ui_mb(ms: i32) -> i32 { return (ms >> 24) & 3 }\n"
    "fn ui_lbtn(ms: i32) -> i32 { return (ms >> 24) & 1 }\n"
    "fn ui_rbtn(ms: i32) -> i32 { return (ms >> 25) & 1 }\n"
    "fn ui_inside(px: i32, py: i32, rx: i32, ry: i32, rw: i32, rh: i32) -> i32 {\n"
    "    if px < rx { return 0 } if py < ry { return 0 }\n"
    "    if px >= rx + rw { return 0 } if py >= ry + rh { return 0 } return 1 }\n"
    "fn ui_hover(ms: i32, rx: i32, ry: i32, rw: i32, rh: i32) -> i32 { return ui_inside(ui_mx(ms), ui_my(ms), rx, ry, rw, rh) }\n"
    "fn ui_click(ms: i32, rx: i32, ry: i32, rw: i32, rh: i32) -> i32 { if ui_hover(ms, rx, ry, rw, rh) == 0 { return 0 } return ui_lbtn(ms) }\n"
    "fn gui_rrect(win: i32, x: i32, y: i32, w: i32, h: i32, r: i32, color: i32) { let xy: i32 = x | (y << 16) let wh: i32 = w | (h << 16) gui_fill_round(win, xy, wh, r, color) }\n"
    "fn gcell_w(avail: i32, cols: i32, gap: i32) -> i32 { return (avail - gap * (cols + 1)) / cols }\n"
    "fn gcell_h(avail: i32, rows: i32, gap: i32) -> i32 { return (avail - gap * (rows + 1)) / rows }\n"
    "fn gcell_x(ox: i32, avail: i32, cols: i32, gap: i32, c: i32) -> i32 { let cw: i32 = gcell_w(avail, cols, gap) return ox + gap + c * (cw + gap) }\n"
    "fn gcell_y(oy: i32, avail: i32, rows: i32, gap: i32, r: i32) -> i32 { let ch: i32 = gcell_h(avail, rows, gap) return oy + gap + r * (ch + gap) }\n"
    "fn flex_w(avail: i32, n: i32, gap: i32) -> i32 { return (avail - gap * (n + 1)) / n }\n"
    "fn flex_x(ox: i32, avail: i32, n: i32, gap: i32, i: i32) -> i32 { let iw: i32 = flex_w(avail, n, gap) return ox + gap + i * (iw + gap) }\n"
    "fn gui_panel(win: i32, x: i32, y: i32, w: i32, h: i32, color: i32) { gui_rrect(win, x, y, w, h, 8, color) }\n"
    "fn gui_header(win: i32, x: i32, y: i32, w: i32, text: str) { gui_rrect(win, x, y, w, 22, 5, COL_ACCENT()) gui_text(win, x + 8, y + 7, text, COL_WHITE()) }\n"
    "fn gui_label(win: i32, x: i32, y: i32, text: str, fg: i32) { gui_text(win, x, y, text, fg) }\n"
    "fn gui_hsep(win: i32, x: i32, y: i32, w: i32) { gui_pen(win, COL_BORDER()) gui_line(win, x, y, x + w, y) }\n"
    "fn gui_vsep(win: i32, x: i32, y: i32, h: i32) { gui_pen(win, COL_BORDER()) gui_line(win, x, y, x, y + h) }\n"
    "fn gui_btn(win: i32, x: i32, y: i32, w: i32, h: i32, label: str, ms: i32) -> i32 { let hov: i32 = ui_hover(ms, x, y, w, h) let clk: i32 = ui_click(ms, x, y, w, h) let mut bg: i32 = COL_ACCENT() if hov == 1 { bg = COL_HOVER() } if clk == 1 { bg = COL_PRESS() } gui_rrect(win, x, y, w, h, 5, bg) gui_text(win, x + 8, y + (h - 8) / 2, label, COL_WHITE()) return clk }\n"
    "fn gui_btn_outline(win: i32, x: i32, y: i32, w: i32, h: i32, label: str, ms: i32) -> i32 { let hov: i32 = ui_hover(ms, x, y, w, h) let clk: i32 = ui_click(ms, x, y, w, h) if hov == 1 { gui_rrect(win, x, y, w, h, 5, COL_CARD()) } gui_pen(win, COL_ACCENT()) gui_rect(win, x, y, w, h) gui_text(win, x + 8, y + (h - 8) / 2, label, COL_ACCENT()) return clk }\n"
    "fn gui_btn_danger(win: i32, x: i32, y: i32, w: i32, h: i32, label: str, ms: i32) -> i32 { let hov: i32 = ui_hover(ms, x, y, w, h) let clk: i32 = ui_click(ms, x, y, w, h) let mut bg: i32 = COL_DANGER() if hov == 1 { bg = 0xFF7080 } gui_rrect(win, x, y, w, h, 5, bg) gui_text(win, x + 8, y + (h - 8) / 2, label, COL_WHITE()) return clk }\n"
    "fn gui_btn_success(win: i32, x: i32, y: i32, w: i32, h: i32, label: str, ms: i32) -> i32 { let hov: i32 = ui_hover(ms, x, y, w, h) let clk: i32 = ui_click(ms, x, y, w, h) let mut bg: i32 = COL_SUCCESS() if hov == 1 { bg = 0x56D76A } gui_rrect(win, x, y, w, h, 5, bg) gui_text(win, x + 8, y + (h - 8) / 2, label, COL_WHITE()) return clk }\n"
    "fn gui_checkbox(win: i32, x: i32, y: i32, label: str, checked: i32, ms: i32) -> i32 { let hov: i32 = ui_hover(ms, x, y, 16, 16) let clk: i32 = ui_click(ms, x, y, 16, 16) let mut bg: i32 = COL_CARD() if checked == 1 { bg = COL_ACCENT() } if hov == 1 { bg = COL_HOVER() } gui_rrect(win, x, y, 16, 16, 3, bg) gui_pen(win, COL_BORDER()) gui_rect(win, x, y, 16, 16) if checked == 1 { gui_text(win, x + 4, y + 4, \"*\", COL_WHITE()) } gui_text(win, x + 22, y + 4, label, COL_TEXT()) return clk }\n"
    "fn gui_radio(win: i32, x: i32, y: i32, label: str, selected: i32, ms: i32) -> i32 { let clk: i32 = ui_click(ms, x, y, 16, 16) let mut bg: i32 = COL_CARD() if selected == 1 { bg = COL_ACCENT() } gui_fill_circle(win, x + 8, y + 8, 7, bg) gui_pen(win, COL_BORDER()) gui_circle(win, x + 8, y + 8, 7, COL_BORDER()) if selected == 1 { gui_fill_circle(win, x + 8, y + 8, 3, COL_WHITE()) } gui_text(win, x + 22, y + 4, label, COL_TEXT()) return clk }\n"
    "fn gui_progress(win: i32, x: i32, y: i32, w: i32, h: i32, val: i32, maxv: i32, color: i32) { gui_rrect(win, x, y, w, h, 3, COL_CARD()) let fw: i32 = w * val / maxv if fw > 0 { gui_rrect(win, x, y, fw, h, 3, color) } gui_pen(win, COL_BORDER()) gui_rect(win, x, y, w, h) }\n"
    "fn gui_slider(win: i32, x: i32, y: i32, w: i32, val: i32, minv: i32, maxv: i32, ms: i32) -> i32 { let range: i32 = maxv - minv let frac: i32 = (val - minv) * (w - 12) / range gui_rrect(win, x, y + 5, w, 6, 3, COL_CARD()) if frac > 0 { gui_rrect(win, x, y + 5, frac + 6, 6, 3, COL_ACCENT()) } let tx: i32 = x + frac let mut thcol: i32 = COL_ACCENT() if ui_hover(ms, tx, y, 14, 16) == 1 { thcol = COL_HOVER() } gui_fill_circle(win, tx + 6, y + 8, 6, thcol) gui_pen(win, COL_BORDER()) gui_circle(win, tx + 6, y + 8, 6, COL_BORDER()) if ui_click(ms, x, y, w, 16) == 1 { let drag: i32 = ui_mx(ms) - x let mut nv: i32 = minv + drag * range / w if nv < minv { nv = minv } if nv > maxv { nv = maxv } return nv } return val }\n"
    "fn gui_toggle(win: i32, x: i32, y: i32, label: str, on: i32, ms: i32) -> i32 { let clk: i32 = ui_click(ms, x, y, 34, 18) let mut bg: i32 = COL_DGRAY() if on == 1 { bg = COL_ACCENT() } gui_rrect(win, x, y, 34, 18, 9, bg) let mut tx: i32 = x + 3 if on == 1 { tx = x + 17 } gui_fill_circle(win, tx + 6, y + 9, 6, COL_WHITE()) gui_text(win, x + 40, y + 5, label, COL_TEXT()) return clk }\n"
    "fn gui_textbox(win: i32, x: i32, y: i32, w: i32, h: i32, text: str, focused: i32) { let mut border: i32 = COL_BORDER() if focused == 1 { border = COL_ACCENT() } gui_rrect(win, x, y, w, h, 4, COL_CARD()) gui_pen(win, border) gui_rect(win, x, y, w, h) gui_text(win, x + 6, y + (h - 8) / 2, text, COL_TEXT()) }\n"
    "fn gui_scrollbar(win: i32, x: i32, y: i32, h: i32, pos: i32, maxp: i32) { gui_rrect(win, x, y, 6, h, 3, COL_CARD()) if maxp > 0 { let th: i32 = h * 8 / (maxp + 8) let ty: i32 = y + pos * (h - th) / maxp gui_rrect(win, x, ty, 6, th, 3, COL_SUBTEXT()) } }\n"
    "fn gui_badge(win: i32, cx: i32, cy: i32, r: i32, color: i32) { gui_fill_circle(win, cx, cy, r, color) gui_pen(win, COL_BORDER()) gui_circle(win, cx, cy, r, COL_BORDER()) }\n"
    "fn gui_tag(win: i32, x: i32, y: i32, text: str, bg: i32, fg: i32) { gui_rrect(win, x, y, 52, 16, 8, bg) gui_text(win, x + 6, y + 4, text, fg) }\n"
    "fn gui_icon(win: i32, x: i32, y: i32, size: i32, color: i32) { gui_rrect(win, x, y, size, size, 4, color) }\n"
    "fn gui_tooltip(win: i32, x: i32, y: i32, text: str) { gui_rrect(win, x, y - 22, 90, 18, 4, 0x2D333B) gui_pen(win, COL_BORDER()) gui_rect(win, x, y - 22, 90, 18) gui_text(win, x + 5, y - 17, text, COL_SUBTEXT()) }\n"
    "fn gui_notify(win: i32, x: i32, y: i32, w: i32, text: str, kind: i32) { let mut bg: i32 = COL_ACCENT() if kind == 1 { bg = COL_WARN() } if kind == 2 { bg = COL_DANGER() } if kind == 3 { bg = COL_SUCCESS() } gui_rrect(win, x, y, w, 24, 4, bg) gui_text(win, x + 10, y + 8, text, COL_WHITE()) }\n"
    "fn gui_stat_card(win: i32, x: i32, y: i32, w: i32, h: i32, value: str, label: str, color: i32) { gui_rrect(win, x, y, w, h, 6, COL_CARD()) gui_pen(win, color) gui_fill_rect(win, x, y, w, 3) gui_text(win, x + 8, y + 12, value, color) gui_text(win, x + 8, y + 26, label, COL_SUBTEXT()) }\n"
    "fn gui_table_row(win: i32, x: i32, y: i32, w: i32, h: i32, even: i32) { let mut bg: i32 = COL_SURFACE() if even == 1 { bg = COL_CARD() } gui_pen(win, bg) gui_fill_rect(win, x, y, w, h) }\n"
    "fn gui_divider(win: i32, x: i32, y: i32, w: i32, text: str) { gui_pen(win, COL_BORDER()) gui_line(win, x, y + 4, x + 20, y + 4) gui_text(win, x + 24, y, text, COL_SUBTEXT()) }\n";

static const char LIB_MATH_ROS[] =
    "// /lib/ros/math.ros  --  RandomOS Standard Math Library\n"
    "// Usage:  import math\n"
    "fn abs(x: i32) -> i32 { if x < 0 { return 0 - x } return x }\n"
    "fn min(a: i32, b: i32) -> i32 { if a < b { return a } return b }\n"
    "fn max(a: i32, b: i32) -> i32 { if a > b { return a } return b }\n"
    "fn clamp(v: i32, lo: i32, hi: i32) -> i32 { if v < lo { return lo } if v > hi { return hi } return v }\n"
    "fn isqrt(n: i32) -> i32 { if n <= 0 { return 0 } let mut x: i32 = n let mut y: i32 = (x + 1) / 2 while y < x { x = y y = (x + n / x) / 2 } return x }\n"
    "fn ipow(base: i32, exp: i32) -> i32 { let mut r: i32 = 1 let mut e: i32 = exp let mut b: i32 = base while e > 0 { if (e & 1) == 1 { r = r * b } b = b * b e = e >> 1 } return r }\n"
    "fn lerp(a: i32, b: i32, t: i32, scale: i32) -> i32 { return a + (b - a) * t / scale }\n"
    "fn map_range(v: i32, in_min: i32, in_max: i32, out_min: i32, out_max: i32) -> i32 { return out_min + (v - in_min) * (out_max - out_min) / (in_max - in_min) }\n";

static const char LIB_IO_ROS[] =
    "// /lib/ros/io.ros  --  RandomOS I/O Library\n"
    "// Usage:  import io\n"
    "fn io_open(path: str) -> i32 { return tbuf_open(path) }\n"
    "fn io_close(h: i32) { tbuf_close(h) }\n"
    "fn io_save(h: i32) { tbuf_save(h) }\n"
    "fn io_getline(h: i32, n: i32) -> i32 { return tbuf_getline(h, n) }\n"
    "fn io_key(h: i32, k: i32) { tbuf_input(h, k) }\n"
    "fn io_lines(h: i32) -> i32 { return tbuf_linecount(h) }\n"
    "fn io_cursor(h: i32) -> i32 { return tbuf_cursor(h) }\n"
    "fn io_cline(h: i32) -> i32 { return tbuf_cursor(h) & 0xFFFF }\n"
    "fn io_ccol(h: i32) -> i32 { return (tbuf_cursor(h) >> 16) & 0xFFFF }\n"
    "fn io_itoa(h: i32, n: i32) -> i32 { return tbuf_numstr(h, n) }\n"
    "fn print_sep() { println(\"----------------------------------------\") }\n"
    "fn print_header(title: str) { print_sep() println(title) print_sep() }\n";

void shell_init_stdlib(void)
{
    int fd;
    vfs_mkdir("/lib");
    vfs_mkdir("/lib/ros");

    fd = vfs_open("/lib/ros/gui.ros",  VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_GUI_ROS,  strlen(LIB_GUI_ROS));  vfs_close(fd); }

    fd = vfs_open("/lib/ros/math.ros", VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_MATH_ROS, strlen(LIB_MATH_ROS)); vfs_close(fd); }

    fd = vfs_open("/lib/ros/io.ros",   VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) { vfs_write(fd, LIB_IO_ROS,   strlen(LIB_IO_ROS));   vfs_close(fd); }

    log_info("[shell] stdlib written to /lib/ros/");
}
