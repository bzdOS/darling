/*
 * linux_ifioctl.h - the Linux interface ioctls that the guest sysctl(NET_RT_IFLIST) emulation
 * (libsystem_kernel sysctl_net.c: SIOCGIFCONF, SIOCGIFHWADDR, SIOCGIFNETMASK, SIOCGIFBRDADDR) issues,
 * translated to the host (FreeBSD) interface ioctls on the same socket. Without them the host answered ENOTTY,
 * sysctl(NET_RT_IFLIST) failed and getifaddrs() returned -1 (Chrome: "getifaddrs: Inappropriate ioctl for device").
 *
 * Included by freebsd_syscall_trap.c after its host headers. Everything runs inside the syscall trap: only raw
 * host syscalls and stack buffers, no malloc and no stdio. Errors are host errnos as -errno (the trap translates).
 *
 * Linux struct ifreq is 40 bytes (name[16] + a 24-byte union, sockaddr family is 16 bit); host struct ifreq is 32 bytes
 * (sockaddr with a length byte). Linux ifconf = { int len; char *buf } holding 40-byte ifreq records; the host
 * SIOCGIFCONF returns variable-length records (name[16] + sockaddr of sa_len, at least 16).
 */
#ifndef LINUX_IFIOCTL_H
#define LINUX_IFIOCTL_H

#define LIF_SIOCGIFCONF    0x8912UL
#define LIF_SIOCGIFFLAGS   0x8913UL
#define LIF_SIOCGIFADDR    0x8915UL
#define LIF_SIOCGIFBRDADDR 0x8919UL
#define LIF_SIOCGIFNETMASK 0x891bUL
#define LIF_SIOCGIFHWADDR  0x8927UL
#define LIF_IFREQ_SIZE     40
#define LIF_AF_INET        2

/* host ioctls: _IOWR(i, n, struct ifreq / struct ifconf) */
#define LIF_H_SIOCGIFCONF    0xc0106924UL
#define LIF_H_SIOCGIFFLAGS   0xc0206911UL
#define LIF_H_SIOCGIFADDR    0xc0206921UL
#define LIF_H_SIOCGIFBRDADDR 0xc0206923UL
#define LIF_H_SIOCGIFNETMASK 0xc0206925UL
#define LIF_H_SIOCGHWADDR    0xc0206941UL

struct lif_host_ifreq { char name[16]; unsigned char sa_len, sa_family; unsigned char sa_data[14]; };
struct lif_host_ifconf { int len; int pad; char *buf; };

static int lif_is_linux_if_ioctl(unsigned long cmd)
{
    return cmd == LIF_SIOCGIFCONF || cmd == LIF_SIOCGIFFLAGS || cmd == LIF_SIOCGIFADDR ||
           cmd == LIF_SIOCGIFBRDADDR || cmd == LIF_SIOCGIFNETMASK || cmd == LIF_SIOCGIFHWADDR;
}

static void lif_zero(void *p, int n) { char *c = p; while (n-- > 0) *c++ = 0; }
static void lif_copy(void *d, const void *s, int n) { char *dc = d; const char *sc = s; while (n-- > 0) *dc++ = *sc++; }

/* Linux sockaddr_in image (family u16, port, addr) from the host sockaddr in a host ifreq. */
static void lif_sockaddr_to_linux(unsigned char *dst /* 16 bytes */, const unsigned char *hfamily /* sa_family */, const unsigned char *hdata /* 14 */)
{
    lif_zero(dst, 16);
    dst[0] = *hfamily; dst[1] = 0;
    lif_copy(dst + 2, hdata, 14);
}

static long linux_if_ioctl(long fd, unsigned long cmd, long arg)
{
    unsigned char *lreq = (unsigned char *)arg;
    if (cmd == LIF_SIOCGIFCONF) {
        struct { int len; int pad; unsigned char *buf; } *lc = (void *)arg;
        char hbuf[4096];
        struct lif_host_ifconf hc = { (int)sizeof(hbuf), 0, hbuf };
        long r = freebsd_raw_syscall(SYS_ioctl, fd, LIF_H_SIOCGIFCONF, (long)&hc, 0, 0, 0);
        if (r < 0)
            return r;
        int off = 0, out = 0;
        while (off + (int)sizeof(struct lif_host_ifreq) <= hc.len) {
            struct lif_host_ifreq *h = (struct lif_host_ifreq *)(hbuf + off);
            int salen = h->sa_len > 16 ? h->sa_len : 16;
            off += 16 + salen;
            if (h->sa_family != LIF_AF_INET)
                continue;
            if (out + LIF_IFREQ_SIZE > lc->len)
                break;
            lif_zero(lc->buf + out, LIF_IFREQ_SIZE);
            lif_copy(lc->buf + out, h->name, 16);
            lif_sockaddr_to_linux(lc->buf + out + 16, &h->sa_family, h->sa_data);
            out += LIF_IFREQ_SIZE;
        }
        lc->len = out;
        return 0;
    }
    struct lif_host_ifreq h;
    lif_zero(&h, sizeof h);
    lif_copy(h.name, lreq, 16);
    if (cmd == LIF_SIOCGIFFLAGS) {
        long r = freebsd_raw_syscall(SYS_ioctl, fd, LIF_H_SIOCGIFFLAGS, (long)&h, 0, 0, 0);
        if (r < 0)
            return r;
        lif_zero(lreq + 16, 24);
        lreq[16] = h.sa_len; lreq[17] = h.sa_family; /* ifr_flags (short, host) */
        return 0;
    }
    if (cmd == LIF_SIOCGIFHWADDR) {
        long r = freebsd_raw_syscall(SYS_ioctl, fd, LIF_H_SIOCGHWADDR, (long)&h, 0, 0, 0);
        lif_zero(lreq + 16, 24);
        lreq[16] = 1; /* ARPHRD_ETHER */
        if (r >= 0)
            lif_copy(lreq + 18, h.sa_data, 6);
        return 0; /* an interface without a link address (loopback) reports zeros */
    }
    unsigned long hcmd = cmd == LIF_SIOCGIFADDR ? LIF_H_SIOCGIFADDR : cmd == LIF_SIOCGIFNETMASK ? LIF_H_SIOCGIFNETMASK : LIF_H_SIOCGIFBRDADDR;
    long r = freebsd_raw_syscall(SYS_ioctl, fd, hcmd, (long)&h, 0, 0, 0);
    if (r < 0) {
        if (cmd == LIF_SIOCGIFBRDADDR) { /* no broadcast address (loopback, point to point) */
            lif_zero(lreq + 16, 24);
            lreq[16] = LIF_AF_INET;
            return 0;
        }
        return r;
    }
    lif_zero(lreq + 16, 24);
    lif_sockaddr_to_linux(lreq + 16, &h.sa_family, h.sa_data);
    return 0;
}
#endif
