#include "stdio.h"
#include "string.h"


void kmain()
{
    fb_clear();
    cursor_move_home();
    
    char buf[128];
    sprintf(buf, "OS loaded. Version: %d. Subsystem: %s. Code: %c", 1, "String", 'A');
    puts(buf);
}
