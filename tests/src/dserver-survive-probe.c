/* dserver-survive-probe.c - guest half of run-dserver-survive-probe.sh. Keeps making darlingserver round trips
 * (a Mach port allocate/destroy pair, five per second) and prints a tick every second, so the runner can see
 * that the server keeps serving while it injects malformed requests, and then kills this process mid-call to see
 * that the server outlives a dead client. Prints SURVIVE-GUEST lines only; the verdict is the runner's. */
#include <stdio.h>
#include <unistd.h>
#include <mach/mach.h>

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("SURVIVE-GUEST ready\n");
	for (int i = 0; i < 40 * 5; i++) {
		mach_port_t p = MACH_PORT_NULL;
		kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p);
		if (kr != KERN_SUCCESS) { printf("SURVIVE-GUEST rpc-fail kr=%d i=%d\n", kr, i); return 2; }
		mach_port_destroy(mach_task_self(), p);
		if (i % 5 == 0) printf("SURVIVE-GUEST tick %d\n", i / 5);
		usleep(200000);
	}
	printf("SURVIVE-GUEST done\n");
	return 0;
}
