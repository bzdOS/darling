/* guest-segv-catch.d: $1 = pid. See guest-segv-catch.sh. */
#pragma D option quiet
proc:::signal-send
/(args[2] == 11 || args[2] == 10) && args[1]->p_pid == $1/
{
	printf("SEGVD pid=%d tid=%d sig=%d self=%d rip=0x%x rsp=0x%x rbp=0x%x addr=0x%x\n", args[1]->p_pid, args[0]->td_tid, args[2], (curthread->td_tid == args[0]->td_tid), uregs[R_RIP], uregs[R_RSP], uregs[R_RBP], curthread->td_frame->tf_addr);
	ustack(24);
	printf("SEGVD-END\n");
}
proc:::exit
/curpsinfo->pr_pid == $1/
{
	exit(0);
}
