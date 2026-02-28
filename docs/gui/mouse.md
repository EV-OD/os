# PS/2 Mouse Driver

## Hardware Background

The PS/2 mouse uses the **auxiliary channel** of the 8042 keyboard controller.
Data arrives on IRQ12 in 3-byte packets:

```
Byte 0:  Y overflow | X overflow | Y sign | X sign | 1 | Middle | Right | Left
Byte 1:  X movement (two's complement, sign in byte 0 bit 4)
Byte 2:  Y movement (two's complement, sign in byte 0 bit 5)
         Note: Y is inverted — positive = up on screen, we flip it.
```

---

## Initialisation Sequence

```
1. Disable keyboard          out(0x64, 0xAD)
2. Enable aux port           out(0x64, 0xA8)
3. Enable aux IRQ            out(0x64, 0x20); read byte; SET bit 1; write back
4. Reset mouse               mouse_write(0xFF); read 0xAA, 0x00
5. Set defaults              mouse_write(0xF6)
6. Enable data reporting     mouse_write(0xF4)
7. Re-enable keyboard        out(0x64, 0xAE)
8. Register IRQ12 handler    isr_register_handler(IRQ12, mouse_irq_handler)
```

---

## IRQ12 Handler

```c
static int    packet_byte = 0;
static uint8_t packet[3];

static void mouse_irq_handler(void) {
    packet[packet_byte++] = inb(0x60);
    if (packet_byte == 3) {
        packet_byte = 0;
        mouse_process_packet(packet);
    }
    pic_send_eoi(12);
}
```

---

## Packet Processing

```c
static void mouse_process_packet(uint8_t *p) {
    int dx =  (int)(int8_t)p[1];
    int dy = -(int)(int8_t)p[2];   /* Y is inverted in PS/2 */

    s_mouse.x = clamp(s_mouse.x + dx, 0, (int)fb_width()  - 1);
    s_mouse.y = clamp(s_mouse.y + dy, 0, (int)fb_height() - 1);
    s_mouse.buttons = p[0] & 0x07;
}
```

---

## Cursor Sprite

The hardware cursor is a 12×19 transparent-background bitmap drawn at the
current mouse position on every frame.  The previous cursor position is
repainted from the desktop compositor before the new cursor is drawn.

---

## Ports Used

| Port | Direction | Purpose |
|------|-----------|---------|
| 0x60 | R/W | Data port (read mouse data, write mouse commands) |
| 0x64 | R | Status port |
| 0x64 | W | Command port |
