/* so_util.c -- ELF32 ARM ET_DYN loader for Nintendo 3DS.
 *
 * Based on the API of the Vita so_util loader by Andy Nguyen, but it uses
 * standard C I/O and libctru cache maintenance instead of Vita kernel memory
 * blocks.  It intentionally obtains metadata from PT_DYNAMIC, not section
 * headers: stripped Android libraries commonly omit section headers.
 */

#include <3ds.h>

#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_util.h"

#define SO_PAGE_SIZE 0x1000u

static uintptr_t page_down(uintptr_t value) {
  return value & ~(uintptr_t)(SO_PAGE_SIZE - 1);
}

static uintptr_t page_up(uintptr_t value) {
  return ALIGN_MEM(value, SO_PAGE_SIZE);
}

static int range_inside(size_t offset, size_t length, size_t size) {
  return offset <= size && length <= size - offset;
}

static int address_inside(const so_module *mod, uintptr_t address, size_t size) {
  uintptr_t start = (uintptr_t)mod->memory;
  return address >= start && size <= mod->memory_size &&
         address - start <= mod->memory_size - size;
}

static uintptr_t vaddr_to_addr(const so_module *mod, Elf32_Addr vaddr) {
  return mod->load_bias + (uintptr_t)vaddr;
}

static int relocation_target(const so_module *mod, Elf32_Addr offset,
                             uintptr_t **target) {
  uintptr_t address = vaddr_to_addr(mod, offset);
  if (!address_inside(mod, address, sizeof(**target)))
    return 0;
  *target = (uintptr_t *)address;
  return 1;
}

static int symbol_valid(const so_module *mod, uint32_t index) {
  return mod->dynsym != NULL && index < (uint32_t)mod->num_dynsym;
}

static uintptr_t symbol_address(const so_module *mod, const Elf32_Sym *sym) {
  return vaddr_to_addr(mod, sym->st_value);
}

static uint32_t so_hash(const uint8_t *name) {
  uint32_t hash = 0;
  uint32_t high;

  while (*name) {
    hash = (hash << 4) + *name++;
    high = hash & 0xf0000000u;
    if (high != 0)
      hash ^= high >> 24;
    hash &= ~high;
  }
  return hash;
}

static uint32_t gnu_hash(const uint8_t *name) {
  uint32_t hash = 5381;
  while (*name)
    hash = (hash << 5) + hash + *name++;
  return hash;
}

static const char *symbol_name(const so_module *mod, const Elf32_Sym *sym) {
  if (mod->dynstr == NULL)
    return NULL;
  return mod->dynstr + sym->st_name;
}

static int read_headers(FILE *file, size_t file_size, Elf32_Ehdr *ehdr,
                        Elf32_Phdr **phdr) {
  Elf32_Phdr *headers;

  if (file_size < sizeof(*ehdr))
    return -ENOEXEC;
  if (fseek(file, 0, SEEK_SET) != 0 ||
      fread(ehdr, sizeof(*ehdr), 1, file) != 1)
    return -EIO;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
      ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
      ehdr->e_ident[EI_DATA] != ELFDATA2LSB || ehdr->e_type != ET_DYN ||
      ehdr->e_machine != EM_ARM || ehdr->e_phentsize != sizeof(Elf32_Phdr) ||
      !range_inside(ehdr->e_phoff, (size_t)ehdr->e_phnum * sizeof(Elf32_Phdr),
                    file_size))
    return -ENOEXEC;

  headers = malloc((size_t)ehdr->e_phnum * sizeof(Elf32_Phdr));
  if (headers == NULL)
    return -ENOMEM;
  if (fseek(file, (long)ehdr->e_phoff, SEEK_SET) != 0 ||
      fread(headers, sizeof(*headers), ehdr->e_phnum, file) != ehdr->e_phnum) {
    free(headers);
    return -EIO;
  }
  *phdr = headers;
  return 0;
}

static void parse_dynamic(so_module *mod) {
  int i;

  for (i = 0; i < mod->num_dynamic; i++) {
    Elf32_Dyn *entry = &mod->dynamic[i];
    if (entry->d_tag == DT_NULL)
      break;
    switch (entry->d_tag) {
      case DT_SONAME:
        if (mod->dynstr != NULL)
          mod->soname = mod->dynstr + entry->d_un.d_val;
        break;
      case DT_HASH:
        mod->hash = (uint32_t *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_GNU_HASH:
        mod->gnu_hash = (uint32_t *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      default:
        break;
    }
  }

  if (mod->hash != NULL)
    mod->num_dynsym = (int)mod->hash[1];
}

static void set_gnu_dynsym_count(so_module *mod) {
  uint32_t buckets;
  uint32_t symoffset;
  uint32_t bloom_size;
  uint32_t *bucket_table;
  uint32_t *chain;
  uint32_t maximum = 0;
  uint32_t i;

  if (mod->num_dynsym != 0 || mod->gnu_hash == NULL)
    return;

  buckets = mod->gnu_hash[0];
  symoffset = mod->gnu_hash[1];
  bloom_size = mod->gnu_hash[2];
  if (buckets == 0)
    return;

  bucket_table = mod->gnu_hash + 4 + bloom_size;
  chain = bucket_table + buckets;
  for (i = 0; i < buckets; i++) {
    if (bucket_table[i] > maximum)
      maximum = bucket_table[i];
  }
  if (maximum < symoffset) {
    mod->num_dynsym = (int)symoffset;
    return;
  }

  for (i = maximum; address_inside(mod, (uintptr_t)&chain[i - symoffset],
                                  sizeof(chain[0])); i++) {
    if (chain[i - symoffset] & 1u) {
      mod->num_dynsym = (int)(i + 1);
      return;
    }
  }
}

void so_flush_caches(so_module *mod) {
  if (mod == NULL || mod->memory == NULL || mod->memory_size == 0)
    return;

  svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)(uintptr_t)mod->memory,
                           (u32)mod->memory_size);

  svcControlProcessMemory(CUR_PROCESS_HANDLE, (u32)(uintptr_t)mod->memory, 0,
                          (u32)mod->memory_size, MEMOP_PROT,
                          MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE);
}

static void flush_patch(uintptr_t address, size_t size) {
  svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)address, (u32)size);
}

void hook_thumb(uintptr_t addr, uintptr_t dst) {
  /* ARM11 has no Thumb-2, and Thumb-1 cannot load pc from a literal, so the
   * stub continues in ARM state: bx pc lands on the ARM word at addr + 4. */
  uint16_t stub[2] = {
      0x4778, /* bx pc */
      0x46c0, /* mov r8, r8 */
  };
  uint32_t arm[2] = {
      0xe51ff004, /* ldr pc, [pc, #-4] */
      (uint32_t)dst,
  };

  if (addr == 0)
    return;
  addr &= ~(uintptr_t)1;
  if (addr & 2u) {
    const uint16_t nop = 0x46c0; /* mov r8, r8: 0xbf00 needs Thumb-2. */
    memcpy((void *)addr, &nop, sizeof(nop));
    addr += 2;
  }

  memcpy((void *)addr, stub, sizeof(stub));
  memcpy((void *)(addr + sizeof(stub)), arm, sizeof(arm));
  flush_patch(addr, sizeof(stub) + sizeof(arm));
}

void hook_arm(uintptr_t addr, uintptr_t dst) {
  uint32_t hook[2];

  if (addr == 0)
    return;
  hook[0] = 0xe51ff004; /* ldr pc, [pc, #-4] */
  hook[1] = (uint32_t)dst;
  memcpy((void *)addr, hook, sizeof(hook));
  flush_patch(addr, sizeof(hook));
}

int so_load(so_module *mod, const char *filename) {
  FILE *file = NULL;
  size_t file_size = 0;
  long file_length;
  Elf32_Ehdr ehdr_storage;
  Elf32_Ehdr *ehdr = &ehdr_storage;
  Elf32_Phdr *phdr = NULL;
  uintptr_t min_vaddr = UINTPTR_MAX;
  uintptr_t max_vaddr = 0;
  int dynamic_segment = -1;
  int result;
  int i;

  if (mod == NULL || filename == NULL)
    return -EINVAL;
  memset(mod, 0, sizeof(*mod));

  file = fopen(filename, "rb");
  if (file == NULL)
    return -errno;
  if (fseek(file, 0, SEEK_END) != 0 || (file_length = ftell(file)) <= 0) {
    result = -EIO;
    goto fail;
  }
  file_size = (size_t)file_length;

  result = read_headers(file, file_size, ehdr, &phdr);
  if (result < 0)
    goto fail;

  for (i = 0; i < ehdr->e_phnum; i++) {
    Elf32_Phdr *segment = &phdr[i];
    uintptr_t end;

    if (segment->p_type == PT_DYNAMIC)
      dynamic_segment = i;
    if (segment->p_type != PT_LOAD)
      continue;
    if (segment->p_memsz < segment->p_filesz ||
        !range_inside(segment->p_offset, segment->p_filesz, file_size)) {
      result = -ENOEXEC;
      goto fail;
    }
    end = (uintptr_t)segment->p_vaddr + segment->p_memsz;
    if (end < segment->p_vaddr) {
      result = -ENOEXEC;
      goto fail;
    }
    if (page_down(segment->p_vaddr) < min_vaddr)
      min_vaddr = page_down(segment->p_vaddr);
    if (page_up(end) > max_vaddr)
      max_vaddr = page_up(end);
  }

  if (min_vaddr == UINTPTR_MAX || max_vaddr <= min_vaddr || dynamic_segment < 0) {
    result = -ENOEXEC;
    goto fail;
  }

  mod->memory_size = max_vaddr - min_vaddr;
  mod->memory = memalign(SO_PAGE_SIZE, mod->memory_size);
  if (mod->memory == NULL) {
    result = -ENOMEM;
    goto fail;
  }
  memset(mod->memory, 0, mod->memory_size);
  mod->load_bias = (uintptr_t)mod->memory - min_vaddr;
  mod->text_base = mod->load_bias;

  for (i = 0; i < ehdr->e_phnum; i++) {
    Elf32_Phdr *segment = &phdr[i];
    uintptr_t destination;

    if (segment->p_type != PT_LOAD)
      continue;
    destination = vaddr_to_addr(mod, segment->p_vaddr);
    if (!address_inside(mod, destination, segment->p_memsz)) {
      result = -ENOEXEC;
      goto fail;
    }
    if (fseek(file, (long)segment->p_offset, SEEK_SET) != 0 ||
        fread((void *)destination, 1, segment->p_filesz, file) !=
            segment->p_filesz) {
      result = -EIO;
      goto fail;
    }

    if (segment->p_flags & PF_X) {
      mod->text_size += segment->p_memsz;
    } else {
      if (mod->data_base == 0)
        mod->data_base = destination;
      mod->data_size += segment->p_memsz;
    }
  }

  mod->ehdr = (Elf32_Ehdr *)mod->memory;
  mod->phdr = (Elf32_Phdr *)vaddr_to_addr(mod, ehdr->e_phoff);
  mod->dynamic = (Elf32_Dyn *)vaddr_to_addr(mod, phdr[dynamic_segment].p_vaddr);
  mod->num_dynamic = (int)(phdr[dynamic_segment].p_memsz / sizeof(Elf32_Dyn));

  for (i = 0; i < mod->num_dynamic; i++) {
    Elf32_Dyn *entry = &mod->dynamic[i];
    switch (entry->d_tag) {
      case DT_STRTAB:
        mod->dynstr = (char *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_SYMTAB:
        mod->dynsym = (Elf32_Sym *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_REL:
        mod->reldyn = (Elf32_Rel *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_RELSZ:
        mod->num_reldyn = (int)(entry->d_un.d_val / sizeof(Elf32_Rel));
        break;
      case DT_JMPREL:
        mod->relplt = (Elf32_Rel *)vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_PLTRELSZ:
        mod->num_relplt = (int)(entry->d_un.d_val / sizeof(Elf32_Rel));
        break;
      case DT_INIT_ARRAY:
        mod->init_array = (void (**)(void))vaddr_to_addr(mod, entry->d_un.d_ptr);
        break;
      case DT_INIT_ARRAYSZ:
        mod->num_init_array = (int)(entry->d_un.d_val / sizeof(void *));
        break;
      default:
        break;
    }
  }
  parse_dynamic(mod);
  set_gnu_dynsym_count(mod);

  if (mod->dynamic == NULL || mod->dynstr == NULL || mod->dynsym == NULL ||
      mod->num_dynsym <= 0 || (mod->num_reldyn != 0 && mod->reldyn == NULL) ||
      (mod->num_relplt != 0 && mod->relplt == NULL)) {
    result = -ENOEXEC;
    goto fail;
  }

  free(phdr);
  fclose(file);
  return 0;

fail:
  free(phdr);
  if (file != NULL)
    fclose(file);
  so_unload(mod);
  return result;
}

static int relocate_one(so_module *mod, Elf32_Rel *rel) {
  uint32_t symbol_index = ELF32_R_SYM(rel->r_info);
  Elf32_Sym *symbol;
  uintptr_t *target;

  if (!relocation_target(mod, rel->r_offset, &target))
    return -ENOEXEC;
  if (ELF32_R_TYPE(rel->r_info) == R_ARM_RELATIVE) {
    *target += mod->load_bias;
    return 0;
  }
  if (ELF32_R_TYPE(rel->r_info) == R_ARM_NONE)
    return 0;
  if (!symbol_valid(mod, symbol_index))
    return -ENOEXEC;

  symbol = &mod->dynsym[symbol_index];
  switch (ELF32_R_TYPE(rel->r_info)) {
    case R_ARM_ABS32:
      if (symbol->st_shndx != SHN_UNDEF)
        *target += symbol_address(mod, symbol);
      return 0;
    case R_ARM_GLOB_DAT:
    case R_ARM_JUMP_SLOT:
      if (symbol->st_shndx != SHN_UNDEF)
        *target = symbol_address(mod, symbol);
      return 0;
    default:
      return -ENOTSUP;
  }
}

int so_relocate(so_module *mod) {
  int i;
  int result;

  if (mod == NULL || mod->memory == NULL)
    return -EINVAL;
  for (i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
    Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i]
                                         : &mod->relplt[i - mod->num_reldyn];
    result = relocate_one(mod, rel);
    if (result < 0)
      return result;
  }
  return 0;
}

static uintptr_t resolve_import(const char *name, DynLibFunction *funcs,
                                int num_funcs) {
  int i;
  for (i = 0; i < num_funcs; i++) {
    if (funcs[i].symbol != NULL && strcmp(name, funcs[i].symbol) == 0)
      return funcs[i].func;
  }
  return 0;
}

static int resolve_one(so_module *mod, Elf32_Rel *rel, DynLibFunction *funcs,
                       int num_funcs, int taint_missing_imports) {
  uint32_t type = ELF32_R_TYPE(rel->r_info);
  uint32_t symbol_index = ELF32_R_SYM(rel->r_info);
  Elf32_Sym *symbol;
  const char *name;
  uintptr_t function;
  uintptr_t *target;

  if (type != R_ARM_ABS32 && type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT)
    return 0;
  if (!symbol_valid(mod, symbol_index))
    return -ENOEXEC;
  symbol = &mod->dynsym[symbol_index];
  if (symbol->st_shndx != SHN_UNDEF)
    return 0;
  if (!relocation_target(mod, rel->r_offset, &target))
    return -ENOEXEC;

  name = symbol_name(mod, symbol);
  if (name == NULL)
    return -ENOEXEC;
  function = resolve_import(name, funcs, num_funcs);
  if (function == 0) {
    if (taint_missing_imports)
      *target = rel->r_offset;
    return 0;
  }

  if (type == R_ARM_ABS32)
    *target += function;
  else
    *target = function;
  return 0;
}

int so_resolve(so_module *mod, DynLibFunction *funcs, int num_funcs,
               int taint_missing_imports) {
  int i;
  int result;

  if (mod == NULL || funcs == NULL || num_funcs < 0)
    return -EINVAL;
  for (i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
    Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i]
                                         : &mod->relplt[i - mod->num_reldyn];
    result = resolve_one(mod, rel, funcs, num_funcs, taint_missing_imports);
    if (result < 0)
      return result;
  }
  return 0;
}

void so_initialize(so_module *mod) {
  int i;
  if (mod == NULL || mod->init_array == NULL)
    return;
  for (i = 0; i < mod->num_init_array; i++) {
    if (mod->init_array[i] != NULL)
      mod->init_array[i]();
  }
}

static uintptr_t symbol_from_sysv_hash(so_module *mod, const char *symbol) {
  uint32_t buckets;
  uint32_t *bucket;
  uint32_t *chain;
  uint32_t index;
  uint32_t hash;

  if (mod->hash == NULL || mod->hash[0] == 0)
    return 0;
  buckets = mod->hash[0];
  bucket = &mod->hash[2];
  chain = &bucket[buckets];
  hash = so_hash((const uint8_t *)symbol);
  for (index = bucket[hash % buckets]; index != 0 && symbol_valid(mod, index);
       index = chain[index]) {
    if (strcmp(symbol_name(mod, &mod->dynsym[index]), symbol) == 0)
      return symbol_address(mod, &mod->dynsym[index]);
  }
  return 0;
}

static uintptr_t symbol_from_gnu_hash(so_module *mod, const char *symbol) {
  uint32_t buckets;
  uint32_t symoffset;
  uint32_t bloom_size;
  uint32_t *bucket_table;
  uint32_t *chain;
  uint32_t hash;
  uint32_t index;

  if (mod->gnu_hash == NULL || mod->gnu_hash[0] == 0)
    return 0;
  buckets = mod->gnu_hash[0];
  symoffset = mod->gnu_hash[1];
  bloom_size = mod->gnu_hash[2];
  bucket_table = mod->gnu_hash + 4 + bloom_size;
  chain = bucket_table + buckets;
  hash = gnu_hash((const uint8_t *)symbol);
  index = bucket_table[hash % buckets];
  if (index < symoffset)
    return 0;
  for (; symbol_valid(mod, index); index++) {
    uint32_t chain_hash = chain[index - symoffset];
    if ((chain_hash | 1u) == (hash | 1u) &&
        strcmp(symbol_name(mod, &mod->dynsym[index]), symbol) == 0)
      return symbol_address(mod, &mod->dynsym[index]);
    if (chain_hash & 1u)
      break;
  }
  return 0;
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
  uintptr_t address;
  int i;

  if (mod == NULL || symbol == NULL)
    return 0;
  address = symbol_from_sysv_hash(mod, symbol);
  if (address != 0)
    return address;
  address = symbol_from_gnu_hash(mod, symbol);
  if (address != 0)
    return address;
  for (i = 0; i < mod->num_dynsym; i++) {
    if (strcmp(symbol_name(mod, &mod->dynsym[i]), symbol) == 0)
      return symbol_address(mod, &mod->dynsym[i]);
  }
  return 0;
}

void so_unload(so_module *mod) {
  if (mod == NULL)
    return;
  free(mod->memory);
  memset(mod, 0, sizeof(*mod));
}
