#ifndef ROX_H
#define ROX_H

/* =========================================================================
 * rox.h – Rabin OS eXecutable format (.rox)
 *
 * A simple flat executable format for MYOS.  A .rox file consists of:
 *   1. A fixed-size header (rox_header_t)
 *   2. Code/data bytes starting at offset ROX_HEADER_SIZE
 *
 * The loader reads the header, validates the magic number and version,
 * then copies the code into a kernel buffer and jumps to it.
 *
 * For Phase 1, executables are kernel-mode functions (ring 0).
 * User-mode support (with separate address space) is planned for Phase 2.
 *
 * File extension: .rox
 * Magic number:   0x524F5821  ("ROX!" in ASCII)
 * ========================================================================= */

#include "string.h"

/* -------------------------------------------------------------------------
 * Magic & version
 * ------------------------------------------------------------------------- */
#define ROX_MAGIC       0x524F5821u   /* "ROX!" little-endian            */
#define ROX_VERSION     1u            /* Current format version          */
#define ROX_HEADER_SIZE 32u           /* Fixed header size (bytes)       */

/* -------------------------------------------------------------------------
 * Executable header (32 bytes)
 * ------------------------------------------------------------------------- */
typedef struct __attribute__((packed)) rox_header {
    unsigned int  magic;          /**< Must be ROX_MAGIC               */
    unsigned int  version;        /**< Format version (ROX_VERSION)    */
    unsigned int  entry_offset;   /**< Byte offset of entry point from
                                       start of code section           */
    unsigned int  code_size;      /**< Size of code+data section (bytes)*/
    unsigned int  flags;          /**< Reserved flags (0 for now)      */
    char          name[12];       /**< Null-terminated program name    */
} rox_header_t;

/* -------------------------------------------------------------------------
 * Loader API
 * ------------------------------------------------------------------------- */

/**
 * rox_load_and_run – load a .rox file from the VFS and execute it.
 *
 * @param path  Absolute path to the .rox file (e.g. "/bin/hello.rox").
 * @param argc  Argument count (passed to the program).
 * @param argv  Argument vector (passed to the program).
 * @return      0 on success, negative error code on failure.
 *              -1 = file not found
 *              -2 = invalid header / bad magic
 *              -3 = memory allocation failure
 */
int rox_load_and_run(const char *path, int argc, char **argv);

/**
 * rox_validate_header – check if a buffer contains a valid .rox header.
 *
 * @param hdr   Pointer to a rox_header_t.
 * @return      1 if valid, 0 if not.
 */
int rox_validate_header(const rox_header_t *hdr);

#endif /* ROX_H */
