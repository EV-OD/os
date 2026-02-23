#ifndef MODULE_H
#define MODULE_H

/**
 * Validate multiboot info and run the first loaded GRUB module.
 *
 * @param eax  Value of eax passed by GRUB (should be the magic number).
 * @param ebx  Value of ebx passed by GRUB (pointer to multiboot info struct).
 * @return 0 on success (module returned), negative on error.
 */
int module_run(unsigned int eax, unsigned int ebx);

#endif /* MODULE_H */
