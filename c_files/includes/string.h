// it is raw string header file, it is not the standard library string header file as it will be used in kernel development as driver development where we cannot use the standard library functions, so we need to implement our own string functions.

#ifndef STRING_H
#define STRING_H

/* String Functions */

// length of a string
unsigned int strlen(const char *str);
// copy a string from src to dest
char *strcpy(char *dest, const char *src);
// copy n bytes from src to dest
char *strncpy(char *dest, const char *src, unsigned int n);
// concatenate src to dest
char *strcat(char *dest, const char *src);
// concatenate n bytes from src to dest
char *strncat(char *dest, const char *src, unsigned int n);
// compare two strings
int strcmp(const char *str1, const char *str2);
// compare n bytes of two strings
int strncmp(const char *str1, const char *str2, unsigned int n);
// set n bytes of dest to val
void *memset(void *dest, int val, unsigned int n);
// copy n bytes from src to dest
void *memcpy(void *dest, const void *src, unsigned int n);
// compare n bytes of two memory areas
int memcmp(const void *ptr1, const void *ptr2, unsigned int n);


// convert a string to an integer
int atoi(const char *str);
// convert an integer to a string
char *itoa(int value, char *str, int base);

// formatting functions using placeholder %d for integers and %s for strings
int sprintf(char *str, const char *format, ...);




#endif /* STRING_H */