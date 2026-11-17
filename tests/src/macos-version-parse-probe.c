/* macos-version-parse-probe: the OS version lookup the way a browser does it.
 * Size query (NULL buffer), then the read with exactly that size, drop the
 * trailing NUL, split on '.', every component must be a plain decimal integer,
 * the number must fall in the supported range. A failed parse is a crash
 * (int3/ud2) in the browser at startup. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

static int parse_int(const char *s, size_t n, int *out)
{
	size_t i;
	int v = 0;
	if (n == 0)
		return 0;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return 0;
		v = v * 10 + (s[i] - '0');
	}
	*out = v;
	return 1;
}

static int check(const char *name, int *version)
{
	size_t len = 0, i, start = 0;
	char *buf;
	int parts = 0, v = 0, n;

	if (sysctlbyname(name, NULL, &len, NULL, 0) != 0 || len == 0) {
		printf("macos-version-parse-probe: size query failed len=%zu\n", len);
		return 0;
	}
	buf = calloc(1, len + 8);
	if (sysctlbyname(name, buf, &len, NULL, 0) != 0) {
		printf("macos-version-parse-probe: read failed\n");
		return 0;
	}
	if (len < 1 || buf[len - 1] != '\0' || strlen(buf) != len - 1) {
		printf("macos-version-parse-probe: size %zu does not match string \"%s\" (strlen=%zu)\n", len, buf, strlen(buf));
		return 0;
	}
	len--;
	for (i = 0; i <= len; i++) {
		if (i == len || buf[i] == '.') {
			if (!parse_int(buf + start, i - start, &n)) {
				printf("macos-version-parse-probe: component %d of \"%s\" is not an integer\n", parts, buf);
				return 0;
			}
			v = v * 100 + n;
			parts++;
			start = i + 1;
		}
	}
	while (parts++ < 3)
		v *= 100;
	*version = v;
	return 1;
}

int main(void)
{
	int v = 0;
	if (!check("kern.osproductversion", &v)) {
		printf("PROBE-FAIL macos-version-parse-probe\n");
		return 1;
	}
	if (v < 100000 || (v >= 160000 && v < 260000)) {
		printf("PROBE-FAIL macos-version-parse-probe version=%d out of range\n", v);
		return 1;
	}
	printf("PROBE-OK macos-version-parse-probe version=%d\n", v);
	return 0;
}
