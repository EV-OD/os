

# it compiles the loader.asm file to an object file named loader.o 
# -f elf32 specifies the output format as 32-bit ELF (Executable and Linkable Format)
nasm -f elf32 loader.asm 


# it links the loader.o object file to create an executable named kernel.elf
# -T link.ld specifies the linker script to use, which defines how the sections of the object file should be arranged in memory
# -melf_i386 specifies the target architecture as 32-bit Intel x86
ld -T link.ld -melf_i386 loader.o -o kernel.elf



# genisoimage is a tool used to create ISO 9660 filesystem images, which are commonly used for CD-ROMs and bootable media. This command  creates an ISO image named os.iso with the following options:
# -b boot/grub/stage2_eltorito: Specifies the boot image to be used for creating a bootable ISO. In this case, it points to the GRUB stage2_eltorito file, which is necessary for making the ISO bootable.
# -no-emul-boot: Indicates that the boot image is not an emulated floppy disk, which is a common option for modern bootable ISOs.
# -boot-load-size 4: Specifies the number of sectors to load from the boot image. In this case, it tells the system to load 4 sectors (2048 bytes) from the boot image when booting
# -A os: Sets the application identifier for the ISO image to "os". This is a label that can be used to identify the contents of the ISO.
# -input-charset utf8: Specifies that the input character set for file names is UTF-8, which allows for a wider range of characters in file names.
# -quiet: Suppresses the output of the genisoimage command, making it run quietly without displaying progress messages.
# -boot-info-table: Adds a boot information table to the ISO image, which can be used by the bootloader to determine the layout of the ISO and where to find the boot image.
# -o os.iso: Specifies the output file name for the generated ISO image, which in this case is os.iso.
# iso: This is the source directory that contains the files to be included in the ISO image. The contents of this directory will be added to the root of the ISO filesystem.
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

