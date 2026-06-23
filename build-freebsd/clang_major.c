#include <stdio.h>
#if !__clang__
#error "Not running on a clang compiler!"
#endif
int main() { printf("%d", __clang_major__); }