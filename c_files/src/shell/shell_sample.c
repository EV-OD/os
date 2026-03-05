/* shell_sample.c - Sample .ros programs and microui demo */

#include "shell/shell_sample.h"
#include "shell/shell_core.h"
#include "vfs.h"
#include "string.h"
#include "stdio.h"

#ifdef GUI_MODE
#include "process.h"
#include "sched.h"
#include "gui/mu_backend.h"
#endif

static const char sample_hello[] =
    "// Hello from RandomOS!\n"
    "//\n"
    "// Compile:  rosc hello.ros\n"
    "// Run:      ./hello.rox\n"
    "\n"
    "print(\"Hello, RandomOS!\\n\")\n"
    "\n"
    "let x: i32 = 42\n"
    "let y: i32 = x * 2 + 8\n"
    "let answer: i32 = (x + y) / 2\n"
    "\n"
    "print(\"The answer is: \")\n"
    "print(answer)\n";

static const char sample_strings[] =
    "// String printing demo\n"
    "//\n"
    "// Compile:  rosc strings.ros\n"
    "// Run:      ./strings.rox\n"
    "\n"
    "print(\"=== RandomOS String Demo ===\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Welcome to RandomOS!\\n\")\n"
    "print(\"This program prints strings.\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Tab stops:\\n\")\n"
    "print(\"\\tFirst\\n\")\n"
    "print(\"\\t\\tSecond\\n\")\n"
    "print(\"\\t\\t\\tThird\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Multi-line:\\n\")\n"
    "print(\"Line 1\\nLine 2\\nLine 3\\n\")\n"
    "print(\"\\n\")\n"
    "print(\"Goodbye!\\n\")\n";

static const char sample_math[] =
    "// Math operations demo\n"
    "//\n"
    "// Compile:  rosc math.ros\n"
    "// Run:      ./math.rox\n"
    "\n"
    "print(\"=== Math Demo ===\\n\")\n"
    "\n"
    "let a: i32 = 100\n"
    "let b: i32 = 37\n"
    "\n"
    "let sum: i32 = a + b\n"
    "let diff: i32 = a - b\n"
    "let prod: i32 = a * b\n"
    "let quot: i32 = a / b\n"
    "\n"
    "print(\"100 + 37 = \")\n"
    "print(sum)\n"
    "print(\"100 - 37 = \")\n"
    "print(diff)\n"
    "print(\"100 * 37 = \")\n"
    "print(prod)\n"
    "print(\"100 / 37 = \")\n"
    "print(quot)\n"
    "\n"
    "let nested: i32 = (a + b) * (a - b) / 10\n"
    "print(\"(100+37)*(100-37)/10 = \")\n"
    "print(nested)\n";

static const char sample_fib[] =
    "// Fibonacci sequence (compile-time)\n"
    "//\n"
    "// Compile:  rosc fib.ros\n"
    "// Run:      ./fib.rox\n"
    "\n"
    "print(\"=== Fibonacci ===\\n\")\n"
    "\n"
    "let f0: i32 = 0\n"
    "let f1: i32 = 1\n"
    "let f2: i32 = f0 + f1\n"
    "let f3: i32 = f1 + f2\n"
    "let f4: i32 = f2 + f3\n"
    "let f5: i32 = f3 + f4\n"
    "let f6: i32 = f4 + f5\n"
    "let f7: i32 = f5 + f6\n"
    "let f8: i32 = f6 + f7\n"
    "let f9: i32 = f7 + f8\n"
    "let f10: i32 = f8 + f9\n"
    "\n"
    "print(f0)\n"
    "print(f1)\n"
    "print(f2)\n"
    "print(f3)\n"
    "print(f4)\n"
    "print(f5)\n"
    "print(f6)\n"
    "print(f7)\n"
    "print(f8)\n"
    "print(f9)\n"
    "print(f10)\n";

static const char sample_loops[] =
    "// Loops and conditionals demo\n"
    "//\n"
    "// Compile:  rosc loops.ros\n"
    "// Run:      ./loops.rox\n"
    "\n"
    "fn main() {\n"
    "    print(\"=== Loops Demo ===\\n\")\n"
    "\n"
    "    // Count 1 to 5 with while\n"
    "    print(\"Counting up:\\n\")\n"
    "    let mut i: i32 = 1\n"
    "    while i <= 5 {\n"
    "        print(i)\n"
    "        i = i + 1\n"
    "    }\n"
    "\n"
    "    // Countdown with while\n"
    "    print(\"Counting down:\\n\")\n"
    "    let mut n: i32 = 5\n"
    "    while n > 0 {\n"
    "        print(n)\n"
    "        n = n - 1\n"
    "    }\n"
    "\n"
    "    // if / else\n"
    "    print(\"Even/odd check:\\n\")\n"
    "    let mut k: i32 = 0\n"
    "    while k < 6 {\n"
    "        if k % 2 == 0 {\n"
    "            print(\"even\\n\")\n"
    "        } else {\n"
    "            print(\"odd\\n\")\n"
    "        }\n"
    "        k = k + 1\n"
    "    }\n"
    "}\n";

static const char sample_funcs[] =
    "// Functions demo\n"
    "//\n"
    "// Compile:  rosc funcs.ros\n"
    "// Run:      ./funcs.rox\n"
    "\n"
    "fn add(a: i32, b: i32) -> i32 {\n"
    "    return a + b\n"
    "}\n"
    "\n"
    "fn factorial(n: i32) -> i32 {\n"
    "    if n <= 1 {\n"
    "        return 1\n"
    "    }\n"
    "    return n * factorial(n - 1)\n"
    "}\n"
    "\n"
    "fn max(a: i32, b: i32) -> i32 {\n"
    "    if a > b {\n"
    "        return a\n"
    "    }\n"
    "    return b\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    print(\"=== Functions Demo ===\\n\")\n"
    "\n"
    "    let s: i32 = add(12, 30)\n"
    "    print(\"12 + 30 = \")\n"
    "    print(s)\n"
    "\n"
    "    print(\"5! = \")\n"
    "    print(factorial(5))\n"
    "\n"
    "    print(\"max(18, 42) = \")\n"
    "    print(max(18, 42))\n"
    "}\n";

static const char sample_gui[] =
    "// GUI Library Demo -- Interactive\n"
    "//\n"
    "// Uses the standard GUI library from /lib/ros/gui.ros.\n"
    "// Widgets use gui_mouse() for live hover/click detection.\n"
    "// gui_wait returns -1 (EV_CLOSE) when the X button is clicked.\n"
    "//\n"
    "// Compile:  rosc gui.ros\n"
    "// Run:      ./gui.rox\n"
    "\n"
    "import gui\n"
    "\n"
    "fn main() {\n"
    "    let win: i32 = gui_window(50, 30, 420, 300, \"GUI Library Demo\")\n"
    "    mut running: i32 = 1\n"
    "    mut counter: i32 = 0\n"
    "    mut checked: i32 = 0\n"
    "    mut toggle_on: i32 = 0\n"
    "    mut slider_v: i32 = 60\n"
    "    mut radio_sel: i32 = 0\n"
    "\n"
    "    while running == 1 {\n"
    "        let ms: i32 = gui_mouse(win)\n"
    "        let key: i32 = gui_poll(win)\n"
    "\n"
    "        // -- Draw ---------------------------------------------------\n"
    "        gui_fill(win, COL_BG())\n"
    "\n"
    "        // Header\n"
    "        gui_header(win, 10, 8, 400, \"RandomOS  GUI  Library\")\n"
    "        gui_hsep(win, 10, 36, 400)\n"
    "\n"
    "        // Stat cards\n"
    "        let sw: i32 = gcell_w(400, 3, 8)\n"
    "        gui_stat_card(win, gcell_x(10, 400, 3, 8, 0), 44, sw, 50, \"42\",   \"Processes\", COL_ACCENT())\n"
    "        gui_stat_card(win, gcell_x(10, 400, 3, 8, 1), 44, sw, 50, \"87%\",  \"CPU\",       COL_WARN())\n"
    "        gui_stat_card(win, gcell_x(10, 400, 3, 8, 2), 44, sw, 50, \"256M\", \"Memory\",    COL_SUCCESS())\n"
    "\n"
    "        // Buttons (hover-aware)\n"
    "        let bw: i32 = flex_w(400, 3, 8)\n"
    "        if gui_btn(win, flex_x(10, 400, 3, 8, 0), 104, bw, 22, \"Confirm\", ms) == 1 {\n"
    "            counter = counter + 1\n"
    "        }\n"
    "        if gui_btn_outline(win, flex_x(10, 400, 3, 8, 1), 104, bw, 22, \"Cancel\", ms) == 1 {\n"
    "            counter = 0\n"
    "        }\n"
    "        if gui_btn_danger(win, flex_x(10, 400, 3, 8, 2), 104, bw, 22, \"Delete\", ms) == 1 {\n"
    "            if counter > 0 { counter = counter - 1 }\n"
    "        }\n"
    "\n"
    "        // Checkbox + toggle\n"
    "        if gui_checkbox(win, 18, 138, \"Enable feature\", checked, ms) == 1 {\n"
    "            if checked == 0 { checked = 1 } else { checked = 0 }\n"
    "        }\n"
    "        if gui_toggle(win, 240, 136, \"Dark mode\", toggle_on, ms) == 1 {\n"
    "            if toggle_on == 0 { toggle_on = 1 } else { toggle_on = 0 }\n"
    "        }\n"
    "\n"
    "        // Radio buttons\n"
    "        if gui_radio(win, 18,  164, \"Option A\", radio_sel == 0, ms) == 1 { radio_sel = 0 }\n"
    "        if gui_radio(win, 120, 164, \"Option B\", radio_sel == 1, ms) == 1 { radio_sel = 1 }\n"
    "        if gui_radio(win, 222, 164, \"Option C\", radio_sel == 2, ms) == 1 { radio_sel = 2 }\n"
    "\n"
    "        // Slider + progress\n"
    "        gui_label(win, 18, 188, \"Volume:\", COL_SUBTEXT())\n"
    "        slider_v = gui_slider(win, 74, 184, 336, slider_v, 0, 100, ms)\n"
    "        gui_progress(win, 18, 204, 392, 10, slider_v, 100, COL_ACCENT())\n"
    "\n"
    "        // Tags\n"
    "        gui_tag(win, 18,  224, \"alpha\", COL_ACCENT(),  COL_WHITE())\n"
    "        gui_tag(win, 80,  224, \"beta\",  COL_WARN(),    COL_WHITE())\n"
    "        gui_tag(win, 142, 224, \"ok\",    COL_SUCCESS(), COL_WHITE())\n"
    "\n"
    "        // Tooltip when hovering Confirm\n"
    "        if ui_hover(ms, flex_x(10, 400, 3, 8, 0), 104, bw, 22) == 1 {\n"
    "            gui_tooltip(win, flex_x(10, 400, 3, 8, 0), 104, \"Increment\")\n"
    "        }\n"
    "\n"
    "        // Footer\n"
    "        gui_panel(win, 18, 250, 392, 40, COL_CARD())\n"
    "        gui_label(win, 26, 258, \"Q=quit  Hover=highlight  Click=interact\", COL_SUBTEXT())\n"
    "\n"
    "        gui_flush(win)\n"
    "\n"
    "        // -- Handle events ------------------------------------------\n"
    "        if key == 113        { running = 0 }\n"
    "        if key == EV_CLOSE() { running = 0 }\n"
    "    }\n"
    "    gui_close(win)\n"
    "}\n";

static const char sample_test[] =
    "// test_all.ros -- ROX Language Feature Test Suite\n"
    "// Tests all non-GUI features: arithmetic, variables, comparisons,\n"
    "// logical, bitwise, if/else, while, for, break, continue,\n"
    "// functions, recursion, nested calls, type cast.\n"
    "//\n"
    "// Compile:  rosc test_all.ros\n"
    "// Run:      ./test_all.rox\n"
    "\n"
    "fn add(a: i32, b: i32) -> i32 { return a + b }\n"
    "fn mul(a: i32, b: i32) -> i32 { return a * b }\n"
    "fn factorial(n: i32) -> i32 {\n"
    "    if n <= 1 { return 1 }\n"
    "    return n * factorial(n - 1)\n"
    "}\n"
    "fn fib(n: i32) -> i32 {\n"
    "    if n <= 1 { return n }\n"
    "    return fib(n - 1) + fib(n - 2)\n"
    "}\n"
    "fn sum_to(n: i32) -> i32 {\n"
    "    let mut s: i32 = 0\n"
    "    let mut i: i32 = 1\n"
    "    while i <= n { s += i    i += 1 }\n"
    "    return s\n"
    "}\n"
    "\n"
    "fn main() {\n"
    "    println(\"=== ROX Test Suite ===\")\n"
    "    let mut pass: i32 = 0\n"
    "    let mut fail: i32 = 0\n"
    "\n"
    "    // 1. Arithmetic\n"
    "    println(\"-- Arithmetic --\")\n"
    "    if 2 + 3 == 5       { pass += 1 } else { fail += 1 }\n"
    "    if 10 - 4 == 6      { pass += 1 } else { fail += 1 }\n"
    "    if 6 * 7 == 42      { pass += 1 } else { fail += 1 }\n"
    "    if 10 / 3 == 3      { pass += 1 } else { fail += 1 }\n"
    "    if 10 % 3 == 1      { pass += 1 } else { fail += 1 }\n"
    "    if 2 + 3 * 4 == 14  { pass += 1 } else { fail += 1 }\n"
    "    if (2 + 3) * 4 == 20 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 2. Variables + compound assignment\n"
    "    println(\"-- Variables --\")\n"
    "    let mut v: i32 = 10\n"
    "    v += 5    if v == 15 { pass += 1 } else { fail += 1 }\n"
    "    v -= 3    if v == 12 { pass += 1 } else { fail += 1 }\n"
    "    v *= 2    if v == 24 { pass += 1 } else { fail += 1 }\n"
    "    v /= 3    if v == 8  { pass += 1 } else { fail += 1 }\n"
    "    v %= 5    if v == 3  { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 3. Comparisons\n"
    "    println(\"-- Comparisons --\")\n"
    "    if 5 < 10   { pass += 1 } else { fail += 1 }\n"
    "    if 10 > 5   { pass += 1 } else { fail += 1 }\n"
    "    if 5 <= 5   { pass += 1 } else { fail += 1 }\n"
    "    if 10 >= 10 { pass += 1 } else { fail += 1 }\n"
    "    if 5 == 5   { pass += 1 } else { fail += 1 }\n"
    "    if 5 != 10  { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 4. Logical\n"
    "    println(\"-- Logical --\")\n"
    "    if 1 && 1        { pass += 1 } else { fail += 1 }\n"
    "    if !(1 && 0)     { pass += 1 } else { fail += 1 }\n"
    "    if 1 || 0        { pass += 1 } else { fail += 1 }\n"
    "    if !(0 || 0)     { pass += 1 } else { fail += 1 }\n"
    "    if !0            { pass += 1 } else { fail += 1 }\n"
    "    if !!1           { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 5. Bitwise\n"
    "    println(\"-- Bitwise --\")\n"
    "    if (0xFF & 0x0F) == 15  { pass += 1 } else { fail += 1 }\n"
    "    if (0xF0 | 0x0F) == 255 { pass += 1 } else { fail += 1 }\n"
    "    if (0xFF ^ 0x0F) == 240 { pass += 1 } else { fail += 1 }\n"
    "    if (1 << 4) == 16       { pass += 1 } else { fail += 1 }\n"
    "    if (256 >> 3) == 32     { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 6. If / else-if / else\n"
    "    println(\"-- If/Else --\")\n"
    "    let mut r: i32 = 0\n"
    "    if 7 > 10 { r = 1 } else if 7 > 5 { r = 2 } else { r = 3 }\n"
    "    if r == 2 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 7. While loop + break + continue\n"
    "    println(\"-- Loops --\")\n"
    "    if sum_to(10) == 55    { pass += 1 } else { fail += 1 }\n"
    "    if sum_to(100) == 5050 { pass += 1 } else { fail += 1 }\n"
    "    let mut cnt: i32 = 0\n"
    "    let mut idx: i32 = 0\n"
    "    while idx < 100 {\n"
    "        if cnt == 5 { break }\n"
    "        cnt += 1    idx += 1\n"
    "    }\n"
    "    if cnt == 5 { pass += 1 } else { fail += 1 }\n"
    "    let mut evens: i32 = 0\n"
    "    let mut k: i32 = 0\n"
    "    while k < 10 {\n"
    "        k += 1\n"
    "        if k % 2 != 0 { continue }\n"
    "        evens += k\n"
    "    }\n"
    "    if evens == 30 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 8. For loop (range)\n"
    "    println(\"-- For --\")\n"
    "    let mut fs: i32 = 0\n"
    "    for fi in 1..11 { fs += fi }\n"
    "    if fs == 55 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 9. Functions\n"
    "    println(\"-- Functions --\")\n"
    "    if add(3, 4) == 7  { pass += 1 } else { fail += 1 }\n"
    "    if mul(6, 7) == 42 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 10. Recursion\n"
    "    println(\"-- Recursion --\")\n"
    "    if factorial(5) == 120 { pass += 1 } else { fail += 1 }\n"
    "    if factorial(10) == 3628800 { pass += 1 } else { fail += 1 }\n"
    "    if fib(7) == 13  { pass += 1 } else { fail += 1 }\n"
    "    if fib(10) == 55 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // 11. Nested calls\n"
    "    println(\"-- Nested calls --\")\n"
    "    if add(fib(5), factorial(3)) == 11 { pass += 1 } else { fail += 1 }\n"
    "\n"
    "    // Summary\n"
    "    println(\"=== Results ===\")\n"
    "    print(\"PASS: \")    println(pass)\n"
    "    print(\"FAIL: \")    println(fail)\n"
    "    if fail == 0 { println(\"ALL TESTS PASSED!\") }\n"
    "    else         { println(\"SOME TESTS FAILED\") }\n"
    "}\n";

struct sample_entry {
    const char *name;
    const char *file;
    const char *src;
    const char *desc;
};

static const char sample_mu[] =
    "// mu_demo.ros - microui Demo using the gui library\n"
    "// import gui gives you: colors, widgets, layout helpers, poll-based events\n"
    "// Close button or Q to quit.\n"
    "import gui\n"
    "\n"
    "fn main() {\n"
    "    let win: i32 = gui_window(40, 30, 460, 316, \"microui Demo\")\n"
    "    mut running: i32 = 1\n"
    "    mut counter: i32 = 0\n"
    "    mut checked: i32 = 0\n"
    "    mut tog_on:  i32 = 0\n"
    "    mut slider_v: i32 = 40\n"
    "    mut radio_sel: i32 = 0\n"
    "\n"
    "    while running == 1 {\n"
    "        let ms:  i32 = gui_mouse(win)\n"
    "        let key: i32 = gui_poll(win)\n"
    "\n"
    "        if key == EV_CLOSE() { running = 0 }\n"
    "        if key == 113        { running = 0 }\n"
    "\n"
    "        // ---- draw frame ----\n"
    "        gui_fill(win, COL_BG())\n"
    "\n"
    "        // Header\n"
    "        gui_header(win, 8, 6, 444, \"microui Demo  --  RandomOS GUI Library\")\n"
    "        gui_hsep(win, 8, 34, 444)\n"
    "\n"
    "        // --- Stat cards (y=42) : counter | volume level | power ---\n"
    "        let sw:  i32 = 137\n"
    "        let sx0: i32 = 8\n"
    "        let sx1: i32 = 153\n"
    "        let sx2: i32 = 298\n"
    "\n"
    "        if counter == 0 { gui_stat_card(win, sx0, 42, sw, 46, \"0\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 1 { gui_stat_card(win, sx0, 42, sw, 46, \"1\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 2 { gui_stat_card(win, sx0, 42, sw, 46, \"2\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 3 { gui_stat_card(win, sx0, 42, sw, 46, \"3\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 4 { gui_stat_card(win, sx0, 42, sw, 46, \"4\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 5 { gui_stat_card(win, sx0, 42, sw, 46, \"5\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter == 6 { gui_stat_card(win, sx0, 42, sw, 46, \"6\",  \"Counter\", COL_ACCENT()) }\n"
    "        if counter >= 7 { gui_stat_card(win, sx0, 42, sw, 46, \"7+\", \"Counter\", COL_ACCENT()) }\n"
    "\n"
    "        if slider_v < 25  { gui_stat_card(win, sx1, 42, sw, 46, \"LOW\",  \"Volume\", COL_WARN()) }\n"
    "        if slider_v >= 25 {\n"
    "            if slider_v < 75 { gui_stat_card(win, sx1, 42, sw, 46, \"MED\",  \"Volume\", COL_BLUE()) }\n"
    "        }\n"
    "        if slider_v >= 75 { gui_stat_card(win, sx1, 42, sw, 46, \"HIGH\", \"Volume\", COL_GREEN()) }\n"
    "\n"
    "        if tog_on == 0 { gui_stat_card(win, sx2, 42, sw, 46, \"OFF\", \"Power\", COL_DGRAY()) }\n"
    "        if tog_on == 1 { gui_stat_card(win, sx2, 42, sw, 46, \"ON\",  \"Power\", COL_SUCCESS()) }\n"
    "\n"
    "        // --- Buttons panel (y=96) : flex-layout 4 buttons ---\n"
    "        gui_panel(win, 8, 96, 444, 40, COL_SURFACE())\n"
    "        let fw:  i32 = flex_w(428, 4, 8)\n"
    "        let bx0: i32 = flex_x(16, 428, 4, 8, 0)\n"
    "        let bx1: i32 = flex_x(16, 428, 4, 8, 1)\n"
    "        let bx2: i32 = flex_x(16, 428, 4, 8, 2)\n"
    "        let bx3: i32 = flex_x(16, 428, 4, 8, 3)\n"
    "        let bh:  i32 = 22\n"
    "\n"
    "        if gui_btn(win, bx0, 107, fw, bh, \"Increment\", ms) == 1 {\n"
    "            counter = counter + 1\n"
    "        }\n"
    "        if gui_btn_outline(win, bx1, 107, fw, bh, \"Reset\", ms) == 1 {\n"
    "            counter = 0\n"
    "            slider_v = 40\n"
    "        }\n"
    "        if gui_btn_success(win, bx2, 107, fw, bh, \"Save\", ms) == 1 {\n"
    "            checked = 1\n"
    "        }\n"
    "        if gui_btn_danger(win, bx3, 107, fw, bh, \"Clear All\", ms) == 1 {\n"
    "            counter = 0\n"
    "            checked = 0\n"
    "            tog_on = 0\n"
    "            slider_v = 40\n"
    "            radio_sel = 0\n"
    "        }\n"
    "\n"
    "        // --- Checkbox + Toggle (y=144) ---\n"
    "        gui_divider(win, 8, 144, 444, \"Controls\")\n"
    "        if gui_checkbox(win, 8, 162, \"Enable notifications\", checked, ms) == 1 {\n"
    "            if checked == 0 { checked = 1 } else { checked = 0 }\n"
    "        }\n"
    "        if gui_toggle(win, 290, 160, \"Power\", tog_on, ms) == 1 {\n"
    "            if tog_on == 0 { tog_on = 1 } else { tog_on = 0 }\n"
    "        }\n"
    "\n"
    "        // --- Slider + progress (y=190) ---\n"
    "        gui_label(win, 8, 193, \"Volume:\", COL_SUBTEXT())\n"
    "        slider_v = gui_slider(win, 72, 190, 270, slider_v, 0, 100, ms)\n"
    "        gui_progress(win, 350, 190, 102, 16, slider_v, 100, COL_ACCENT())\n"
    "\n"
    "        // --- Radio buttons (y=216) ---\n"
    "        gui_divider(win, 8, 216, 444, \"Theme\")\n"
    "        mut r0: i32 = 0\n"
    "        mut r1: i32 = 0\n"
    "        mut r2: i32 = 0\n"
    "        if radio_sel == 0 { r0 = 1 }\n"
    "        if radio_sel == 1 { r1 = 1 }\n"
    "        if radio_sel == 2 { r2 = 1 }\n"
    "        if gui_radio(win, 8,   232, \"Dark\",   r0, ms) == 1 { radio_sel = 0 }\n"
    "        if gui_radio(win, 118, 232, \"Light\",  r1, ms) == 1 { radio_sel = 1 }\n"
    "        if gui_radio(win, 228, 232, \"System\", r2, ms) == 1 { radio_sel = 2 }\n"
    "\n"
    "        // --- Tags + tooltip (y=258) ---\n"
    "        gui_tag(win, 8,   258, \"INFO\",  COL_ACCENT(),  COL_WHITE())\n"
    "        gui_tag(win, 68,  258, \"WARN\",  COL_WARN(),    COL_BLACK())\n"
    "        gui_tag(win, 128, 258, \"ERROR\", COL_DANGER(),  COL_WHITE())\n"
    "        gui_tag(win, 188, 258, \"OK\",    COL_SUCCESS(), COL_WHITE())\n"
    "        if ui_hover(ms, 188, 258, 52, 16) == 1 {\n"
    "            gui_tooltip(win, 188, 258, \"System normal!\")\n"
    "        }\n"
    "\n"
    "        // --- Notify bar (y=282) ---\n"
    "        if checked == 1 {\n"
    "            gui_notify(win, 8, 282, 444, \"Notifications ON  --  all systems nominal\", 3)\n"
    "        } else {\n"
    "            gui_notify(win, 8, 282, 444, \"Notifications OFF  [enable checkbox above]\", 0)\n"
    "        }\n"
    "\n"
    "        gui_flush(win)\n"
    "    }\n"
    "    gui_close(win)\n"
    "}\n";

static const struct sample_entry sample_table[] = {
    { "hello",   "hello.ros",   sample_hello,   "Hello + variables"       },
    { "strings", "strings.ros", sample_strings, "String printing"         },
    { "math",    "math.ros",    sample_math,    "Math operations"         },
    { "fib",     "fib.ros",     sample_fib,     "Fibonacci sequence"      },
    { "loops",   "loops.ros",   sample_loops,   "While loops + if/else"   },
    { "funcs",   "funcs.ros",   sample_funcs,   "Functions + recursion"   },
    { "gui",     "gui.ros",     sample_gui,     "GUI library: import, widgets, grid/flex layout" },
    { "mu",      "mu_demo.ros", sample_mu,      "import gui: stat cards, buttons, checkbox, slider, radio, tags, notify" },
    { "test",    "test_all.ros", sample_test,   "Full language feature test (PASS/FAIL report)"  },
};

#define SAMPLE_COUNT (sizeof(sample_table) / sizeof(sample_table[0]))

int cmd_sample(int argc, char **argv)
{
    /* No arguments or "list" → show available templates */
    if (argc < 2 || strcmp(argv[1], "list") == 0) {
        puts_color("Available sample programs:\n", COLOR_WHITE);
        unsigned int i;
        for (i = 0; i < SAMPLE_COUNT; i++) {
            puts_color("  sample ", COLOR_DARK_GREY);
            puts_color(sample_table[i].name, COLOR_LIGHT_GREEN);
            puts_color("  - ", COLOR_DARK_GREY);
            puts((char *)sample_table[i].desc);
            putchar('\n');
        }
        puts_color("\nUsage: sample <name>\n", COLOR_DARK_GREY);
        return 0;
    }

    /* Find the matching template */
    const struct sample_entry *entry = (const struct sample_entry *)0;
    unsigned int i;
    for (i = 0; i < SAMPLE_COUNT; i++) {
        if (strcmp(argv[1], sample_table[i].name) == 0) {
            entry = &sample_table[i];
            break;
        }
    }

    if (!entry) {
        puts_color("sample: unknown template '", COLOR_LIGHT_RED);
        puts(argv[1]);
        puts("'. Use 'sample list' to see available templates.\n");
        return -1;
    }

    char path[256];
    resolve_path(entry->file, path, sizeof(path));

    /* Check if file already exists */
    vfs_stat_t st;
    if (vfs_stat(path, &st) == 0) {
        puts_color("sample: file already exists: ", COLOR_LIGHT_BROWN);
        puts(path);
        putchar('\n');
        return -1;
    }

    int fd = vfs_open(path, VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        puts_color("sample: cannot create: ", COLOR_LIGHT_RED);
        puts(path);
        putchar('\n');
        return -1;
    }

    vfs_write(fd, entry->src, strlen(entry->src));
    vfs_close(fd);

    puts_color("  created: ", COLOR_LIGHT_GREEN);
    puts(path);
    putchar('\n');
    puts_color("  compile: ", COLOR_DARK_GREY);
    puts("rosc ");
    puts(path);
    putchar('\n');
    return 0;
}

/* -------------------------------------------------------------------------
 * microui – launch the microui demo as a kernel process
 * ------------------------------------------------------------------------- */
int cmd_microui(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef GUI_MODE
    process_t *p = process_create("mu_demo", mu_demo_run, 0);
    if (!p) {
        puts_color("microui: failed to create process\n", COLOR_LIGHT_RED);
        return -1;
    }
    sched_add(p);
    puts("microui demo launched\n");
    int status = process_wait(p->pid);
    process_destroy(p);
    return status;
#else
    puts("microui requires GUI mode\n");
    return -1;
#endif
}
