

# it compiles the loader.asm file to an object file named loader.o 
# -f elf32 specifies the output format as 32-bit ELF (Executable and Linkable Format)
nasm -f elf32 loader.asm 


# it links the loader.o object file to create an executable named kernel.elf
# -T link.ld specifies the linker script to use, which defines how the sections of the object file should be arranged in memory
# -melf_i386 specifies the target architecture as 32-bit Intel x86
ld -T link.ld -melf_i386 loader.o -o kernel.elf




genisoimage -R \
    -b boot/grub/stage2_eltorito \
    -no-emul-boot \
    -boot-load-size 4 \
    -A os \
    -input-charset utf8 \
    -quiet \
    -boot-info-table \
    -o os.iso \
    iso