/* FreeBSD port: inline Mach VM types (avoids 9p symlink issues) */
#ifndef _MACH_MACHINE_VM_TYPES_H_
#define _MACH_MACHINE_VM_TYPES_H_
#ifndef ASSEMBLER
#include <stdint.h>
#include <i386/_types.h>

typedef __darwin_natural_t      natural_t;
typedef int                     integer_t;

/* LP64 on both x86_64 and aarch64 */
typedef uintptr_t               vm_offset_t;
typedef uintptr_t               vm_size_t;

typedef uint64_t                mach_vm_address_t;
typedef uint64_t                mach_vm_offset_t;
typedef uint64_t                mach_vm_size_t;
typedef uint64_t                vm_map_offset_t;
typedef uint64_t                vm_map_address_t;
typedef uint64_t                vm_map_size_t;
typedef mach_vm_address_t       mach_port_context_t;
#endif /* ASSEMBLER */
#endif /* _MACH_MACHINE_VM_TYPES_H_ */
