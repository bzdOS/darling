/* dns-probe: what Chrome's DNS config/hosts watchers (net/dns/dns_config_service_posix.cc) need on macOS.
 * config watch: notify_register_file_descriptor on the SystemConfiguration dns key must succeed (status 0, valid fd);
 * hosts watch: kqueue EVFILT_VNODE on an O_EVTONLY descriptor of /etc/hosts must register. Prints DNSW <step> ok|FAIL. */
extern int printf(const char *, ...);
extern int fflush(void *);
extern int *__error(void);
extern int open(const char *, int, ...);
extern int kqueue(void);
extern int close(int);
extern int notify_register_file_descriptor(const char *, int *, int, int *);
extern int notify_register_mach_port(const char *, unsigned *, int, int *);
extern int notify_cancel(int);
struct kev { unsigned long ident; short filter; unsigned short flags; unsigned fflags; long data; void *udata; };
extern int kevent(int, const struct kev *, int, struct kev *, int, const void *);
static int bad;
static void res(const char *n, int ok, int info) { printf("DNSW %s %s (%d)\n", n, ok ? "ok" : "FAIL", info); fflush(0); if (!ok) bad++; }
int main(void)
{
	int fd = -1, tok = -1;
	int st = notify_register_file_descriptor("com.apple.system.SystemConfiguration.dns_configuration", &fd, 0, &tok);
	res("notify_register_file_descriptor", st == 0 && fd >= 0, st);
	unsigned port = 0; int tok2 = -1;
	st = notify_register_mach_port("com.apple.system.SystemConfiguration.dns_configuration", &port, 0, &tok2);
	res("notify_register_mach_port", st == 0 && port != 0, st);
	if (tok >= 0) res("notify_cancel", notify_cancel(tok) == 0, 0);
	int f = open("/etc/hosts", 0x8000 /* O_EVTONLY */);
	res("open-evtonly", f >= 0, *__error());
	int kq = kqueue();
	res("kqueue", kq >= 0, *__error());
	struct kev ev = { (unsigned long)f, -4 /* EVFILT_VNODE */, 0x0001 | 0x0020 /* EV_ADD|EV_CLEAR */, 0x1|0x2|0x4|0x8|0x10|0x20|0x40 , 0, 0 };
	int r = kevent(kq, &ev, 1, 0, 0, 0);
	res("kevent-vnode", r >= 0, *__error());
	printf("DNSW DONE fail=%d\n", bad); fflush(0);
	return bad != 0;
}
