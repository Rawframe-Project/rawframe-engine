// The one unit that holds Wuffs's implementation (ours; wuffs-v0.4.c is
// upstream's). Its modules are chosen by the definitions CMakeLists.txt
// gives every user: the image formats ADR-0058 admits and the auxiliary
// image API over them.
#define WUFFS_IMPLEMENTATION
#include "wuffs-v0.4.c"
