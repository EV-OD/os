/* kmain.c */

void kmain()
{
    /* The video memory address. */
    volatile unsigned char *video = (unsigned char *) 0xB8000;
    
    /* Clear the screen (optional, but good for visibility) */
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }

    /* Print 'Hello' at the top left */
    const char *str = "Hello from kmain!";
    for (int i = 0; str[i] != '\0'; i++) {
        video[i * 2] = str[i];
        video[i * 2 + 1] = 0x07;
    }
}
