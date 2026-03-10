#include <stdio.h>
#include "frmalloc.h"

int main() {
  int *a = (int *)frmalloc(sizeof(int) * 50);
  printf("Allocating %zu bytes at a - %p\n", sizeof(int) * 50, a);
  int *b = (int *)frmalloc(sizeof(int) * 13);
  printf("Allocating %zu bytes at b - %p\n", sizeof(int) * 13, b); 
  float *c = (float *)frmalloc(sizeof(float) * 100); 
  printf("Allocating %zu bytes at c - %p\n", sizeof(float) * 100, c);
  printf("Freeing a\n");
  frfree(a);  
  printf("Freeing b\n");
  frfree(b);
  a = (int *)frmalloc(sizeof(int) * 63);
  printf("Allocating %zu bytes at a - %p\n", sizeof(int) * 63, a);
  return 0;
}
