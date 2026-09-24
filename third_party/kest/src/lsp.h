#ifndef KEST_LSP_H
#define KEST_LSP_H

#include <stdbool.h>
#include <stdio.h>

// A language server, speaking LSP over two streams. It is this compiler and
// not a reader of its own: every answer it gives -- what a name is, where it
// was declared, what else names it, what is wrong with the file -- comes from
// the same check a build does, over the same tree, through the index the
// checker writes as it resolves. A second parser inside an editor is a second
// semantic pipeline, which is the thing this project does not have. See D977.
//
// `library` is where the standard library is, the same answer the command line
// works out for itself. Returns what the process should exit with.
int kest_lsp_serve(const char *library, FILE *in, FILE *out);

#endif
