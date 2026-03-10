#ifndef FRMALLOC_H
#define FRMALLOC_H

#include <stddef.h>

void *frmalloc(size_t size);
void frfree(void *pointer);

#endif
