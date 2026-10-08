// Instrumented entry point for the harness.
//
// The real ld.lld calls lld::macho::link() and turns its bool into an exit
// code. Do the same here, otherwise the harness reports the opposite of the
// reference binary (true -> 1 instead of true -> 0) and every rc in a parity
// table reads inverted.
//
// The markers on stderr are the reason this file exists: they say how far the
// harness got, which is what the crash reports are read against.

#include "lld/MachO/Driver.h"
#include "lld/Common/ErrorHandler.h"
#include <cstdio>
#include <vector>

namespace lld::macho {
bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput);
}

int main(int argc, char **argv) {
  fprintf(stderr, "main: start\n");
  lld::ErrorHandler handler;
  handler.initialize(llvm::outs(), llvm::errs(), false, false);
  fprintf(stderr, "main: handler initialized\n");
  std::vector<const char *> args;
  args.push_back("ld64.lld");
  for (int i = 1; i < argc; i++)
    args.push_back(argv[i]);
  fprintf(stderr, "main: calling link\n");
  bool ret = lld::macho::link(args, llvm::outs(), llvm::errs(), false, false);
  fprintf(stderr, "main: link returned %d\n", static_cast<int>(ret));
  return ret ? 0 : 1;
}