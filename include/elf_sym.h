#ifndef ELF_SYM_H
#define ELF_SYM_H

#include <stdint.h>

// 从 ELF 文件里查符号, 返回相对加载基址的偏移 (0 = 未找到)
uintptr_t elf_symbol_offset(const char *path, const char *sym_name);

// 在一组候选路径里查找, 返回偏移, 并可回填命中的路径
int elf_find_symbol(const char *const *paths, int npaths,
                    const char *sym_name, char *out_path, size_t out_size);

// 收集某库可能的磁盘路径, 返回写入的条数
int elf_candidate_paths(const char *lib, const char *extra_dir,
                        const char *const *multiarch, int n_multi,
                        char paths[][512], int max);

#endif // ELF_SYM_H