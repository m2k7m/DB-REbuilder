/*
** Bootstrap loader for DB-REbuilder.
**
** Based on bootstrap-bin.c from elfldr by John Törnblom.
** See https://github.com/ps4-payload-dev/elfldr/blob/master/bootstrap-bin.c and the GNU GPL v3 license.
**
** This tiny loader is embedded at the start of a raw .bin payload.  It maps
** an RWX region, copies the embedded db-rebuilder ELF image into it, applies
** R_X86_64_RELATIVE relocations, and jumps to the ELF entry point.
*/

#include "db_rebuilder_elf.c"

#define MAP_PRIVATE 0x0002
#define MAP_ANONYMOUS 0x1000
#define MAP_FAILED ((void *)-1)

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define PAGE_SIZE 0x4000
#define ROUND_PG(x) (((x) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))
#define TRUNC_PG(x) ((x) & ~(PAGE_SIZE - 1))

#define PT_LOAD 1
#define R_X86_64_RELATIVE 8
#define SHT_RELA 4

typedef struct {
  unsigned char e_ident[16];
  unsigned short e_type;
  unsigned short e_machine;
  unsigned int e_version;
  unsigned long e_entry;
  unsigned long e_phoff;
  unsigned long e_shoff;
  unsigned int e_flags;
  unsigned short e_ehsize;
  unsigned short e_phentsize;
  unsigned short e_phnum;
  unsigned short e_shentsize;
  unsigned short e_shnum;
  unsigned short e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  unsigned int p_type;
  unsigned int p_flags;
  unsigned long p_offset;
  unsigned long p_vaddr;
  unsigned long p_paddr;
  unsigned long p_filesz;
  unsigned long p_memsz;
  unsigned long p_align;
} Elf64_Phdr;

typedef struct {
  unsigned int sh_name;
  unsigned int sh_type;
  unsigned long sh_flags;
  unsigned long sh_addr;
  unsigned long sh_offset;
  unsigned long sh_size;
  unsigned int sh_link;
  unsigned int sh_info;
  unsigned long sh_addralign;
  unsigned long sh_entsize;
} Elf64_Shdr;

typedef struct {
  unsigned long r_offset;
  unsigned long r_info;
  long r_addend;
} Elf64_Rela;

static void *
memcpy(void *dest, const void *src, unsigned long n) {
  __asm__ __volatile__("rep movsb"
                       : "+D"(dest), "+S"(src), "+c"(n)
                       :
                       : "memory");
  return dest;
}

static inline long
__syscall(long n, ...) {
  long a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
  __builtin_va_list ap;
  unsigned long ret;
  char iserror;

  __builtin_va_start(ap, n);
  a1 = __builtin_va_arg(ap, long);
  a2 = __builtin_va_arg(ap, long);
  a3 = __builtin_va_arg(ap, long);
  a4 = __builtin_va_arg(ap, long);
  a5 = __builtin_va_arg(ap, long);
  a6 = __builtin_va_arg(ap, long);
  __builtin_va_end(ap);

  register long r10 __asm__("r10") = a4;
  register long r8 __asm__("r8") = a5;
  register long r9 __asm__("r9") = a6;

  __asm__ __volatile__("syscall"
                       : "=a"(ret), "=@ccc"(iserror), "+r"(r10), "+r"(r8),
                         "+r"(r9)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                       : "rcx", "r11", "memory");

  return iserror ? -ret : ret;
}

static inline void *
mmap(void *addr, unsigned long len, int prot, int flags, int fd,
     unsigned long offset) {
  return (void *)__syscall(477, addr, len, prot, flags, fd, offset);
}

static inline int
munmap(void *addr, unsigned long len) {
  return (int)__syscall(73, addr, len);
}

static void
payload_exec(unsigned char *elf) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf;
  Elf64_Phdr *phdr = (Elf64_Phdr *)(elf + ehdr->e_phoff);
  Elf64_Shdr *shdr = (Elf64_Shdr *)(elf + ehdr->e_shoff);
  unsigned long min_vaddr = -1;
  unsigned long max_vaddr = 0;
  unsigned long img_size = 0;
  unsigned char *img = 0;
  void (*entry)(void);

  /* Sanity check: we only support ELF files. */
  if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E'
      || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
    return;
  }

  /* Compute the size of the virtual memory region. */
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_vaddr < min_vaddr) {
      min_vaddr = phdr[i].p_vaddr;
    }

    if (max_vaddr < phdr[i].p_vaddr + phdr[i].p_memsz) {
      max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
  }

  min_vaddr = TRUNC_PG(min_vaddr);
  max_vaddr = ROUND_PG(max_vaddr);
  img_size = max_vaddr - min_vaddr;

  /* Reserve an address space of sufficient size. */
  if ((img = mmap(0, img_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
      == MAP_FAILED) {
    return;
  }

  /* Parse program headers. */
  for (int i = 0; i < ehdr->e_phnum; i++) {
    switch (phdr[i].p_type) {
      case PT_LOAD:
        if (phdr[i].p_memsz && phdr[i].p_filesz) {
          memcpy(img + phdr[i].p_vaddr, elf + phdr[i].p_offset,
                 phdr[i].p_filesz);
        }
        break;
    }
  }

  /* Apply relocations. */
  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdr[i].sh_type != SHT_RELA) {
      continue;
    }

    Elf64_Rela *rela = (Elf64_Rela *)(elf + shdr[i].sh_offset);
    for (unsigned long j = 0; j < shdr[i].sh_size / sizeof(Elf64_Rela); j++) {
      if ((rela[j].r_info & 0xffffffffl) == R_X86_64_RELATIVE) {
        void *loc = (img + rela[j].r_offset);
        void *val = (img + rela[j].r_addend);
        memcpy(loc, &val, sizeof(val));
      }
    }
  }

  entry = (void *)(img + ehdr->e_entry);
  entry();

  munmap(img, img_size);
}

#ifdef BUILD_INSTALLER

#define O_WRONLY 1
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

static inline int
sys_open(const char *path, int flags, int mode) {
  return (int)__syscall(5, path, flags, mode);
}

static inline int
sys_close(int fd) {
  return (int)__syscall(6, fd);
}

static inline long
sys_write(int fd, const void *buf, unsigned long n) {
  return __syscall(4, fd, buf, n);
}

static inline int
sys_mkdir(const char *path, unsigned int mode) {
  return (int)__syscall(136, path, mode);
}

static inline int
sys_chmod(const char *path, unsigned int mode) {
  return (int)__syscall(15, path, mode);
}

typedef struct {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, unsigned long, int);

static void
send_notification(const char *message) {
  notify_request_t req;
  unsigned char *p = (unsigned char *)&req;
  unsigned long i;
  unsigned long len;

  for (i = 0; i < sizeof(req); i++) {
    p[i] = 0;
  }

  for (len = 0; len < sizeof(req.message) - 1 && message[len]; len++) {
    req.message[len] = message[len];
  }
  req.message[len] = '\0';

  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

int
main(void) {
  const char *dst = "/data/payloads/db-rebuilder-v" PAYLOAD_VERSION ".elf";
  int fd;

  sys_mkdir("/data/payloads", 0777);

  fd = sys_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0) {
    return 1;
  }

  if (sys_write(fd, db_rebuilder_elf, db_rebuilder_elf_len)
      != (long)db_rebuilder_elf_len) {
    sys_close(fd);
    return 1;
  }

  sys_close(fd);
  sys_chmod(dst, 0777);

  send_notification("DB-Rebuilder v" PAYLOAD_VERSION
                    " Payload installed to /data/payloads/.");

  payload_exec(db_rebuilder_elf);
  return 0;
}

#else /* BUILD_INSTALLER */

int
_start(void) {
  payload_exec(db_rebuilder_elf);
  return 0;
}

#endif /* BUILD_INSTALLER */
