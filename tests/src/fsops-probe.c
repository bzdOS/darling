/* fsops-probe: the file system calls a browser's SQLite-backed profile databases make, in the guest.
 * statfs/fstatfs of "/" and of a directory, create + pwrite/pread, byte-range locks (fcntl F_SETLK/F_GETLK on the
 * SQLite lock bytes), F_FULLFSYNC, F_NOCACHE, fsync, ftruncate, shared mmap, flock, rename, unlink, mkdir, fsync of a directory.
 * One line per check "FSOPS-T <name> rc=<r> errno=<e>", then "FSOPS-VERDICT pass|FAIL n=<checks> bad=<failed> [names]".
 * A failing profile database is the cause of the "Something went wrong when opening your profile" alert. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/file.h>

static int n, bad;
static char names[512];

static void rep(const char *name, int ok, long rc)
{
	int e = errno;
	n++;
	printf("FSOPS-T %s rc=%ld errno=%d%s\n", name, rc, ok ? 0 : e, ok ? "" : " BAD");
	if (!ok) {
		bad++;
		if (strlen(names) + strlen(name) + 2 < sizeof names) {
			if (names[0])
				strcat(names, ",");
			strcat(names, name);
		}
	}
	fflush(stdout);
}

#define CHK(name, expr) do { errno = 0; long _r = (long)(expr); rep(name, _r >= 0, _r); } while (0)

int main(void)
{
	const char *dir = "/fsops-probe.d";
	char path[128], path2[128], buf[4096], rd[4096];
	struct statfs sf;
	struct flock fl;
	struct stat st;
	int fd, dfd;
	void *m;

	snprintf(path, sizeof path, "%s/db", dir);
	snprintf(path2, sizeof path2, "%s/db.moved", dir);
	unlink(path); unlink(path2); rmdir(dir);

	memset(&sf, 0, sizeof sf);
	CHK("statfs-root", statfs("/", &sf));
	printf("FSOPS-INFO statfs-root type=%s mnt=%s blocks=%llu\n", sf.f_fstypename, sf.f_mntonname, (unsigned long long)sf.f_blocks);
	CHK("mkdir", mkdir(dir, 0755));
	CHK("statfs-dir", statfs(dir, &sf));
	rmdir(dir);
	printf("FSOPS-INFO tmp-exists=%d proc-exists=%d private-exists=%d\n", stat("/tmp", &st) == 0, stat("/proc/self/mounts", &st) == 0, stat("/private", &st) == 0);
	CHK("stat-root", stat("/", &st));
	CHK("mkdir", mkdir(dir, 0755));
	fd = open(path, O_RDWR | O_CREAT, 0644);
	CHK("open-create", fd);
	if (fd < 0)
		goto done;
	CHK("fstatfs", fstatfs(fd, &sf));
	memset(buf, 0x5a, sizeof buf);
	CHK("pwrite", pwrite(fd, buf, sizeof buf, 0) == (ssize_t)sizeof buf ? 0 : -1);
	memset(rd, 0, sizeof rd);
	CHK("pread", pread(fd, rd, sizeof rd, 0) == (ssize_t)sizeof rd && memcmp(rd, buf, sizeof buf) == 0 ? 0 : -1);
	memset(&fl, 0, sizeof fl);
	fl.l_type = F_RDLCK; fl.l_whence = SEEK_SET; fl.l_start = 0x40000000 + 2; fl.l_len = 1;
	CHK("fcntl-setlk-shared", fcntl(fd, F_SETLK, &fl));
	fl.l_type = F_WRLCK; fl.l_start = 0x40000000; fl.l_len = 1;
	CHK("fcntl-setlk-excl", fcntl(fd, F_SETLK, &fl));
	fl.l_type = F_WRLCK; fl.l_start = 0x40000000; fl.l_len = 512;
	CHK("fcntl-getlk", fcntl(fd, F_GETLK, &fl));
	fl.l_type = F_UNLCK; fl.l_start = 0x40000000; fl.l_len = 512;
	CHK("fcntl-unlock", fcntl(fd, F_SETLK, &fl));
	CHK("fcntl-fullfsync", fcntl(fd, F_FULLFSYNC, 0));
	/* a cache hint: informational only (SQLite does not need it) */
	errno = 0;
	printf("FSOPS-INFO fcntl-nocache rc=%d errno=%d\n", fcntl(fd, F_NOCACHE, 1), errno);
	CHK("fsync", fsync(fd));
	CHK("flock-ex", flock(fd, LOCK_EX | LOCK_NB));
	CHK("flock-un", flock(fd, LOCK_UN));
	CHK("ftruncate", ftruncate(fd, 8192));
	CHK("fstat-size", fstat(fd, &st) == 0 && st.st_size == 8192 ? 0 : -1);
	m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHK("mmap-shared", m == MAP_FAILED ? -1 : 0);
	if (m != MAP_FAILED) {
		((char *)m)[0] = 1;
		CHK("msync", msync(m, 4096, MS_SYNC));
		munmap(m, 4096);
	}
	close(fd);
	CHK("rename", rename(path, path2));
	dfd = open(dir, O_RDONLY);
	CHK("open-dir", dfd);
	if (dfd >= 0) {
		CHK("fsync-dir", fsync(dfd));
		close(dfd);
	}
	CHK("access", access(path2, R_OK | W_OK));
	CHK("unlink", unlink(path2));
	CHK("rmdir", rmdir(dir));
done:
	printf("FSOPS-VERDICT %s n=%d bad=%d%s%s\n", bad ? "FAIL" : "pass", n, bad, bad ? " " : "", names);
	fflush(stdout);
	return bad ? 1 : 0;
}
