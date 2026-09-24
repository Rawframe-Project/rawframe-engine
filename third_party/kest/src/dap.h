#ifndef KEST_DAP_H
#define KEST_DAP_H

#include <stdio.h>

#include "debug.h"

// What a program run under the adapter may call, made by the caller: the same
// host the command line runs a program with, writing where it is told and
// reading from where it is told.
typedef KestHost *(*KestHostMaker)(FILE *output, FILE *input);

// `kest dap`: the debugger an editor drives, over the Debug Adapter Protocol
// on two streams. The editor names the program in its `launch` request; it is
// built with `library` and run with a host `make` makes, and what it writes
// arrives in the editor as output rather than in the protocol. Answers what
// the process should exit with. See D1182.
int kest_dap_serve(const char *library, KestHostMaker make, FILE *in,
                   FILE *out);

#endif
