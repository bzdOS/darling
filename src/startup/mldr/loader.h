#ifndef _MLDR_LOADER_H_
#define _MLDR_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct load_results {
	unsigned long mh;
	unsigned long entry_point;
	unsigned long stack_size;
	unsigned long dyld_all_image_location;
	unsigned long dyld_all_image_size;
	/* Where dyld's own mach header was mapped.
	 *
	 * Not derivable from anything else here: dyld_all_image_location is the
	 * address of dyld_all_image_infos, which lives somewhere inside the
	 * image, and `mh` is only recorded for MH_EXECUTE, so without this the
	 * address of a fault INSIDE dyld cannot be turned into an offset in it.
	 * That is exactly the question a crash log asks, and for one run it had
	 * to be answered by subtracting a constant from the boot entry point
	 * because this field did not exist. */
	unsigned long dyld_mh;
	uint8_t uuid[16];

	unsigned long vm_addr_max;
	bool _32on64;
	unsigned long base;
	uint32_t bprefs[4];
	char* root_path;
	size_t root_path_length;
	unsigned long stack_top;
	/* How many bytes the guest's stack was actually mapped with. Not the
	 * same as `stack_size`, which is the LC_MAIN request; this is the size
	 * that got clamped to the rlimit and the cap, and a crash log needs it
	 * to know whether a stack fault was an overflow or something else. */
	unsigned long stack_size_mapped;
	char* socket_path;
	int kernfd;
	int lifetime_pipe;

	size_t argc;
	size_t envc;
	char** argv;
	char** envp;
};

#endif // _MLDR_LOADER_H_
