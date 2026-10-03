/* zone-contract.c — core bodies for the zone-contract
 * libsystem_malloc replacement (control #15).
 *
 * Slot order per the macOS-13 contract the chrome framework dispatches
 * through (control #14 measured the overlay's zone as
 * reserved/reserved/size/malloc/... — slot0 NULL at the call site):
 *   0 size  1 free  2 realloc  3 destroy  4 zone_name
 *   5 batch_malloc  6 batch_free  7 introspect
 * (the control #15 acceptance: vtable[0]=size, vtable[3]=destroy,
 * dladdr-valid).
 *
 * The allocator behind malloc/free/calloc/realloc is a bump allocator so
 * the guest keeps running; the zone methods themselves return 0/NULL —
 * the chrome run's next refusal past this contract is the artifact.
 */
#include <stddef.h>

typedef struct zone_contract {
	void *slots[16];
} zone_contract;

static struct zone_contract the_zone;
static unsigned char bump[64 * 1024 * 1024];
static size_t bump_off;

static size_t default_zone_size(void *zone, const void *ptr)
{ (void)zone; (void)ptr; return 0; }
static void default_zone_free(void *zone, void *ptr)
{ (void)zone; (void)ptr; }
static void *default_zone_realloc(void *zone, void *ptr, size_t size)
{ (void)zone; (void)ptr; (void)size; return 0; }
static void default_zone_destroy(void *zone)
{ (void)zone; }
static unsigned default_zone_batch_malloc(void *zone, size_t size,
	void **results, unsigned num)
{ (void)zone; (void)size; (void)results; (void)num; return 0; }
static void default_zone_batch_free(void *zone, void **ptrs, unsigned num)
{ (void)zone; (void)ptrs; (void)num; }

void *malloc(size_t n)
{
	size_t a = (n + 15) & ~(size_t)15;
	if (bump_off + a > sizeof(bump))
		return 0;
	void *p = &bump[bump_off];
	bump_off += a;
	return p;
}
void *calloc(size_t nm, size_t sz)
{
	size_t n = nm * sz;
	void *p = malloc(n);
	if (p) {
		unsigned char *c = p;
		while (n--)
			*c++ = 0;
	}
	return p;
}
void *realloc(void *p, size_t n)
{
	void *q = malloc(n);
	if (q && p) {
		unsigned char *d = q, *s = p;
		size_t m = n;
		while (m--)
			*d++ = *s++;
	}
	return q;
}
void free(void *p) { (void)p; }
void *valloc(size_t n) { return malloc(n); }

void *malloc_default_zone(void) { return &the_zone; }
void *malloc_default_purgeable_zone(void) { return &the_zone; }
int malloc_get_all_zones(void *task, void *reader, void **zones,
	unsigned *count)
{
	(void)task; (void)reader;
	if (zones)
		zones[0] = &the_zone;
	if (count)
		*count = 1;
	return 0;
}
void *malloc_zone_malloc(void *zone, size_t n) { (void)zone; return malloc(n); }
void malloc_zone_free(void *zone, void *p) { (void)zone; (void)p; }
void *malloc_zone_realloc(void *zone, void *p, size_t n)
{ (void)zone; return realloc(p, n); }
size_t malloc_zone_size(void *zone, const void *p) { (void)zone; (void)p; return 0; }
void *malloc_create_zone(size_t start, unsigned flags)
{ (void)start; (void)flags; return &the_zone; }
void malloc_set_zone_name(void *zone, const char *name)
{ (void)zone; (void)name; }

__attribute__((constructor)) static void zone_contract_init(void)
{
	the_zone.slots[0] = (void *)default_zone_size;
	the_zone.slots[1] = (void *)default_zone_free;
	the_zone.slots[2] = (void *)default_zone_realloc;
	the_zone.slots[3] = (void *)default_zone_destroy;
	the_zone.slots[4] = 0; /* zone_name */
	the_zone.slots[5] = (void *)default_zone_batch_malloc;
	the_zone.slots[6] = (void *)default_zone_batch_free;
	the_zone.slots[7] = 0; /* introspect */
}
