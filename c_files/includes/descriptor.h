

//global
struct gdt {
    unsigned int address;
    unsigned short size;
} __attribute__((packed));



//local
struct idt {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));


