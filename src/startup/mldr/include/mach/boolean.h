/* FreeBSD port: inline Mach boolean (avoids 9p symlink issues) */
#ifndef _MACH_BOOLEAN_H_
#define _MACH_BOOLEAN_H_
#ifndef ASSEMBLER
#include <mach/machine/boolean.h>
#endif
#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif
#endif /* _MACH_BOOLEAN_H_ */
