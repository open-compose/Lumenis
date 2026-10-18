#pragma once

// On Windows, a crash prints the exception and the call stack to stderr
// (functions and lines from cutthulhu.pdb next to the executable, else
// module + offset), then ends the process as before. Elsewhere it does
// nothing: the system's own crash reporter has the stack.
void installCrashReport();
