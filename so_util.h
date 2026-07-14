/*
 * Small ELF32/ARM shared-object loader for Nintendo 3DS.
 *
 * The public API deliberately matches the loader used by the Vita ports.
 * A port normally only needs to replace this header and so_util.c, then
 * provide its own DynLibFunction table for the platform APIs it exposes.
 */
#ifndef BC2_SO_UTIL_H
#define BC2_SO_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include "elf.h"

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

typedef struct {
  uintptr_t text_base;
  uintptr_t data_base;
  size_t text_size;
  size_t data_size;

  void *memory;
  size_t memory_size;
  uintptr_t load_bias;

  Elf32_Ehdr *ehdr;
  Elf32_Phdr *phdr;
  Elf32_Dyn *dynamic;
  Elf32_Sym *dynsym;
  Elf32_Rel *reldyn;
  Elf32_Rel *relplt;
  void (**init_array)(void);

  uint32_t *hash;
  uint32_t *gnu_hash;
  char *soname;
  char *dynstr;

  int num_dynamic;
  int num_dynsym;
  int num_reldyn;
  int num_relplt;
  int num_init_array;
} so_module;

typedef struct {
  const char *symbol;
  uintptr_t func;
} DynLibFunction;

void hook_thumb(uintptr_t addr, uintptr_t dst);
void hook_arm(uintptr_t addr, uintptr_t dst);

void so_flush_caches(so_module *mod);

int so_load(so_module *mod, const char *filename);
int so_relocate(so_module *mod);
int so_resolve(so_module *mod, DynLibFunction *funcs, int num_funcs,
               int taint_missing_imports);
void so_initialize(so_module *mod);

uintptr_t so_symbol(so_module *mod, const char *symbol);
void so_unload(so_module *mod);

#endif
