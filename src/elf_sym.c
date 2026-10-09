// ============================================================
// elf_sym.c - 直接解析 ELF 文件获取符号偏移
//
// 为什么不用 dlopen/dlsym:
//   1. 命令行版静态链接后 (STATIC=1) dlopen 不可用
//   2. 交叉编译场景下, 控制器运行的机器上可能没有目标架构的库文件,
//      而 dlopen 依赖本机动态链接器加载
//   3. 直接解析磁盘上的 ELF 得到的偏移就是真实加载偏移, 更准确
//
// 兼容两种 ELF:
//   ET_DYN (共享库): st_value 就是相对加载基址的偏移
//   ET_EXEC (可执行文件): st_value 是绝对地址, 需减去最低 PT_LOAD 的 vaddr
// ============================================================

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

#include "elf_sym.h"

// 查找 ELF 符号, 返回其相对加载基址的偏移; 找不到返回 0
uintptr_t elf_symbol_offset(const char *path, const char *sym_name) {
    if (!path || !sym_name) return 0;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(Elf64_Ehdr)) {
        close(fd);
        return 0;
    }

    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return 0;

    uintptr_t result = 0;
    const unsigned char *base = (const unsigned char *)map;

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) goto out;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) goto out;
    if (eh->e_shoff == 0 || eh->e_shnum == 0) goto out;

    // ET_EXEC 需要减去最低 PT_LOAD 的 vaddr
    uintptr_t image_base = 0;
    if (eh->e_type == ET_EXEC) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + eh->e_phoff);
        image_base = (uintptr_t)-1;
        for (int i = 0; i < eh->e_phnum; i++) {
            if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr < image_base) {
                image_base = ph[i].p_vaddr;
            }
        }
        if (image_base == (uintptr_t)-1) goto out;
    }

    const Elf64_Shdr *sh = (const Elf64_Shdr *)(base + eh->e_shoff);

    // 优先 .dynsym (动态符号一定存在), 再试 .symtab (未 strip 时才有)
    for (int pass = 0; pass < 2 && result == 0; pass++) {
        Elf64_Xword want = (pass == 0) ? SHT_DYNSYM : SHT_SYMTAB;

        for (int i = 0; i < eh->e_shnum; i++) {
            if (sh[i].sh_type != want) continue;
            if (sh[i].sh_link >= eh->e_shnum) continue;
            if (sh[i].sh_entsize != sizeof(Elf64_Sym)) continue;

            const Elf64_Shdr *strsh = &sh[sh[i].sh_link];
            const char *strtab = (const char *)(base + strsh->sh_offset);
            const Elf64_Sym *sym = (const Elf64_Sym *)(base + sh[i].sh_offset);

            size_t n = sh[i].sh_size / sh[i].sh_entsize;
            for (size_t k = 0; k < n; k++) {
                if (sym[k].st_name == 0) continue;
                if (sym[k].st_shndx == SHN_UNDEF) continue;   // 未定义符号
                if (sym[k].st_value == 0) continue;
                if (strcmp(strtab + sym[k].st_name, sym_name) != 0) continue;

                result = (sym[k].st_value >= image_base)
                       ? (sym[k].st_value - image_base)   // ET_EXEC
                       :  sym[k].st_value;                // ET_DYN
                break;
            }
            if (result) break;
        }
    }

out:
    munmap(map, (size_t)st.st_size);
    return result;
}

// 在候选路径里逐个查找符号, 返回 (找到的路径, 偏移)
int elf_find_symbol(const char *const *paths, int npaths,
                    const char *sym_name, char *out_path, size_t out_size) {
    if (!paths || !sym_name) return -1;

    for (int i = 0; i < npaths; i++) {
        if (!paths[i] || !*paths[i]) continue;
        uintptr_t off = elf_symbol_offset(paths[i], sym_name);
        if (off) {
            if (out_path && out_size) {
                strncpy(out_path, paths[i], out_size - 1);
                out_path[out_size - 1] = '\0';
            }
            return (int)off;
        }
    }
    return -1;
}

// 收集某库可能的磁盘路径 (aarch64 为目标时 lib/ 与默认 multiarch 布局)
int elf_candidate_paths(const char *lib, const char *extra_dir,
                        const char *const *multiarch, int n_multi,
                        char paths[][512], int max) {
    int n = 0;

    // 1. 极域安装目录
    if (extra_dir && *extra_dir && lib && *lib) {
        if (n < max) snprintf(paths[n++], 512, "%s/%s", extra_dir, lib);
    }

    // 2. multiarch 库目录
    for (int i = 0; i < n_multi && n < max; i++) {
        if (!multiarch[i] || !*multiarch[i]) continue;
        if (lib && strncmp(lib, "lib", 3) == 0) {
            snprintf(paths[n++], 512, "%s/%s", multiarch[i], lib);
        } else {
            snprintf(paths[n++], 512, "%s/lib%s", multiarch[i], lib);
        }
    }

    // 3. 标准路径
    static const char *std[] = { "/lib/", "/usr/lib/", "/lib64/", "/usr/lib64/",
                                 "/lib/aarch64-linux-gnu/", "/usr/lib/aarch64-linux-gnu/" };
    for (size_t i = 0; i < sizeof(std)/sizeof(std[0]) && n < max; i++) {
        if (lib && strncmp(lib, "lib", 3) == 0) {
            snprintf(paths[n++], 512, "%s%s", std[i], lib);
        } else {
            snprintf(paths[n++], 512, "%slib%s", std[i], lib);
        }
    }

    return n;
}