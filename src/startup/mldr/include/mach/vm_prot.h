/* FreeBSD port: inline Mach vm_prot definitions (avoids 9p symlink issues) */
#ifndef _MACH_VM_PROT_H_
#define _MACH_VM_PROT_H_
typedef int             vm_prot_t;
#define VM_PROT_NONE    ((vm_prot_t) 0x00)
#define VM_PROT_READ    ((vm_prot_t) 0x01)
#define VM_PROT_WRITE   ((vm_prot_t) 0x02)
#define VM_PROT_EXECUTE ((vm_prot_t) 0x04)
#define VM_PROT_DEFAULT (VM_PROT_READ|VM_PROT_WRITE)
#define VM_PROT_ALL     (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)
#define VM_PROT_NO_CHANGE       ((vm_prot_t) 0x08)
#define VM_PROT_COPY            ((vm_prot_t) 0x10)
#define VM_PROT_WANTS_COPY      ((vm_prot_t) 0x10)
#define VM_PROT_IS_MASK         ((vm_prot_t) 0x40)
#define VM_PROT_STRIP_READ      ((vm_prot_t) 0x80)
#define VM_PROT_EXECUTE_ONLY    (VM_PROT_EXECUTE|VM_PROT_STRIP_READ)
#endif /* _MACH_VM_PROT_H_ */
