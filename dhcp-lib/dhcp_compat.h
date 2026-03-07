#ifndef DHCP_COMPAT_H
#define DHCP_COMPAT_H

/*
 * dhcp_compat.h — Portability shim replacing pi-boot's kstring.h.
 *
 * Maps kmemcpy/kmemset/kmemcmp to GCC built-ins so the dhcp-lib C sources
 * can compile under both the bare-metal pi-boot toolchain and Circle's
 * arm-none-eabi / aarch64-none-elf cross-compilers without pulling in any
 * OS-specific string library.
 */

#include <stddef.h>

#define kmemcpy(dst, src, n)  __builtin_memcpy((dst), (src), (n))
#define kmemset(dst, val, n)  __builtin_memset((dst), (val), (n))
#define kmemcmp(a,   b,   n)  __builtin_memcmp((a),   (b),   (n))

#endif /* DHCP_COMPAT_H */
