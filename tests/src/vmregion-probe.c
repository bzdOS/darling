/* vmregion-probe: the lookup Chrome's malloc zone code (base/allocator/dispatcher/.. allocator_interception_apple.mm
 * DeprotectMallocZone) does: vm_region_64(VM_REGION_BASIC_INFO_64) at the address of the default malloc zone must
 * return a region containing it. Also the same for a stack and a heap address. Prints VMR <step> ok|FAIL. */
extern int printf(const char *, ...);
extern int fflush(void *);
extern void *malloc(unsigned long);
extern void *malloc_default_zone(void);
extern unsigned mach_task_self_;
extern int vm_region_64(unsigned, unsigned long *, unsigned long *, int, int *, unsigned *, unsigned *);
static int bad;
static void chk(const char *n, void *p)
{
	unsigned long a = (unsigned long)p, sz = 0; int info[16]; unsigned cnt = 9, obj = 0;
	int kr = vm_region_64(mach_task_self_, &a, &sz, 9 /* VM_REGION_BASIC_INFO_64 */, info, &cnt, &obj);
	int ok = kr == 0 && a <= (unsigned long)p && (unsigned long)p < a + sz;
	printf("VMR %s %s addr=%p kr=%d region=%lx+%lx\n", n, ok ? "ok" : "FAIL", p, kr, a, sz); fflush(0);
	if (!ok) bad++;
}
int main(void)
{
	int local;
	chk("zone", malloc_default_zone());
	chk("heap", malloc(100));
	chk("stack", &local);
	chk("text", (void *)main);
	printf("VMR DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
