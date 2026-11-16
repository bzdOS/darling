#pragma once
/* migcom host build on FreeBSD: version string normally injected by the bootstrap_cmds build. */
#ifndef MIG_VERSION
#define MIG_VERSION "bootstrap_cmds-freebsd"
#endif
/* Apple sys/_types.h pieces that FreeBSD sys/_types.h lacks; migcom parses them from xnu headers. */
typedef unsigned int __darwin_mach_port_name_t;
typedef __darwin_mach_port_name_t __darwin_mach_port_t;
typedef unsigned char __darwin_uuid_t[16];
typedef char __darwin_uuid_string_t[37];
