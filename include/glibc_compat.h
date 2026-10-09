#ifndef GLIBC_COMPAT_H
#define GLIBC_COMPAT_H

// ============================================================
// glibc 版本向下兼容
//
// 问题: 交叉编译时编译机的 glibc (2.39) 比目标机新。
//       UOS Desktop 20 基于 Debian, glibc 约 2.31。
//       glibc 2.34 把 pthread / dl / rt 的符号并入 libc, 默认版本号
//       随之从 GLIBC_2.17 变为 GLIBC_2.34。若二进制引用了 2.34,
//       在旧 glibc 上直接失败:
//           /lib/ld-linux-aarch64.so.1: version `GLIBC_2.34' not found
//
// 解决: 用 .symver 把这些符号显式绑定到 GLIBC_2.17。
//       该版本在 glibc 2.2.5 ~ 2.39 上都存在, 向后兼容。
//
// 注意: 本文件必须在其余所有系统头文件之后生效, 所以自己包含它们。
// ============================================================

#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stddef.h>

// dl 系列
__asm__(".symver dlopen,  dlopen@GLIBC_2.17");
__asm__(".symver dlsym,   dlsym@GLIBC_2.17");
__asm__(".symver dlclose, dlclose@GLIBC_2.17");
__asm__(".symver dladdr,  dladdr@GLIBC_2.17");
__asm__(".symver dlerror, dlerror@GLIBC_2.17");

// 共享内存 (旧 glibc 位于 librt, 新 glibc 已并入 libc)
__asm__(".symver shm_open,   shm_open@GLIBC_2.17");
__asm__(".symver shm_unlink, shm_unlink@GLIBC_2.17");

// 线程
__asm__(".symver pthread_create, pthread_create@GLIBC_2.17");
__asm__(".symver pthread_join,   pthread_join@GLIBC_2.17");

#endif // GLIBC_COMPAT_H