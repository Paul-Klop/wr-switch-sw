/* 
 * Copyright (c) 2011 Grzegorz Daniluk <g.daniluk@elproma.com.pl>
 * ELF support added by Alessandro Rubini for CERN, 2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h> /* htonl */
#include <libgen.h>

#define BASE_FPGA 		0x10000000
#define SIZE_FPGA 		0x20000

#define URVBOOT_BASE 		0x10900
#define URVBOOT_RESET		0x00
#define URVBOOT_IRAM_ADDR	0x04
#define URVBOOT_IRAM_DATA	0x08

#define URV_RAM_SIZE_WORD   16384

static void *base_fpga;
static char *prgname;


static void fpga_writel(uint32_t data, uint32_t addr)
{
    *(volatile uint32_t *)(base_fpga + addr) = data;
}

static uint32_t fpga_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(base_fpga + addr);
}

static void urv_reset(int rst)
{
    /* Reset is bit 0 of first register @ URV_REGS */
    fpga_writel(rst, (URVBOOT_BASE + URVBOOT_RESET));
}

static void urv_write_iram(uint32_t data, uint32_t addr)
{
    /* Write address register with the address of the data */
    fpga_writel(addr, (URVBOOT_BASE + URVBOOT_IRAM_ADDR));
    /* Write data register with the data to be written at specified address */
    fpga_writel(htonl(data), (URVBOOT_BASE + URVBOOT_IRAM_DATA));
}

static uint32_t urv_read_iram(uint32_t addr)
{
    /* Write address register with the address of the data */
    fpga_writel(addr, (URVBOOT_BASE + URVBOOT_IRAM_ADDR));
    /* Read data register */
    return ntohl(fpga_readl(URVBOOT_BASE + URVBOOT_IRAM_DATA));
}

static int copy_urv(void *data, int noload, int size, uint32_t base_addr)
{
    int i;
    uint32_t *buf = data; /* LE 32-bit oriented */
    int buf_nwords = (size + 3) / 4;

    /* Do not actually load anything. This is used to read/write variables */
    if (noload)
	    return 0;

    /* First put urv core in reset to be able to access its internal RAM through instruction port */
    urv_reset(1);

    printf("Writing memory (0x%04x bytes at 0x%04x): ", size, base_addr);

    /* WARNING: Because of the way gateware is designed, incrementing URVBOOT_ADDR register 
     * by 1 will result in selecting the next 4 bytes in urv IRAM (granularity is 32-bit word).
     * The base address provided is expecting a byte addressed memory, so we need to divide
     * this address by 4 in order to cope with this specificity.
     * This is only used while loading ELF files because BIN files are loaded in one step
     * and the base address provided is always 0
     */
    base_addr >>= 2;

    for(i = 0; i < buf_nwords; i++) {
	urv_write_iram(buf[i], base_addr + i);
	if (!(i & 0x3ff))
	    printf(".");
    }

    printf("\nVerifing memory: ");

    for(i = 0; i < buf_nwords; i++) {
	uint32_t x = urv_read_iram(base_addr + i);
	if (buf[i] != x)
	{
	    printf("Verify failed (%x vs %x)\n", buf[i], x);
	    return -1;
	}

	if (!(i & 0x3ff))
	    printf(".");
    }

    printf(" OK.\n");
    return 0;
}

static char *global_strptr;
static Elf32_Shdr *global_sh;

/* The elf loader relies on the binary loader above (and the global mmap) */
static int copy_urv_elf(void *data, int noload, int size)
{
    int i, flags, verbose = getenv("LOAD_urv_VERBOSE") != NULL;
    Elf32_Ehdr *eh;
    Elf32_Phdr *ph;
    Elf32_Shdr *sh;
    char *strptr;

    eh = data;
    if (verbose) {
	printf("type:    %8i\n", eh->e_type);
	printf("machine: %8i\n", eh->e_machine);
	printf("version: %8i\n", eh->e_version);
	printf("entry:   %08lx\n", (long)eh->e_entry);
	printf("phoff:   %8i\n", eh->e_phoff);
	printf("shoff:   %8i\n", eh->e_shoff);
	printf("ehsize:  %8i\n", eh->e_ehsize);
	printf("shstrndx:%8i\n", eh->e_shstrndx);
    }

    ph = (Elf32_Phdr *)((char *)eh + (int)(eh->e_phoff));

    /* program headers. Irrelevant, actually... */
    for (i = 0; i < eh->e_phnum; i++) {
	flags = ph->p_flags;
	if (verbose) {
	    printf("prg: %i 0x%08lx, 0x%08lx, 0x%08lx, %c%c%c "
		   "(%08x), %i, %i %i\n",
		   ph->p_type,
		   (long)(ph->p_offset),
		   (long)(ph->p_vaddr),
		   (long)(ph->p_paddr),
		   flags & PF_R ? 'r' : '-',
		   flags & PF_W ? 'w' : '-',
		   flags & PF_X ? 'x' : '-',
		   flags,
		   ph->p_filesz,
		   ph->p_memsz,
		   ph->p_align);
	}
    }

    /* first loop: look for strtab */
    sh = (Elf32_Shdr *)((char *)eh + (int)(eh->e_shoff));
    for (i = 0; i < eh->e_shstrndx; i++)
    sh=(Elf32_Shdr *)((char *)sh + (int)eh->e_shentsize);
    strptr = (char *)eh + sh->sh_offset;
    sh = (Elf32_Shdr *)((char *)eh + (int)(eh->e_shoff));

    /* Save them for later (setting vriables) */
    global_strptr = strptr;
    global_sh = sh;

    /* Section headers: this is what we load */
    for (i = 0; i < eh->e_shnum; i++) {
	unsigned long off, len, ram;
	if (i) /* next header */
	    sh = (Elf32_Shdr *)((char *)sh + (int)eh->e_shentsize);

	if (verbose) {
	    printf("sect: %3i %-25.25s %2i 0x%08lx, 0x%08lx, (%i) %i %i\n",
		   sh->sh_name,
		   strptr + sh->sh_name,
		   sh->sh_type,
		   (long)(sh->sh_offset),
		   (long)(sh->sh_addr),
		   sh->sh_size,
		   sh->sh_addralign,
		   sh->sh_entsize);
	}

	off = sh->sh_offset;
	len = sh->sh_size;
	ram = sh->sh_addr & 0x0fffffff;

	/* ignore unloadable sections */
	if (sh->sh_type != SHT_PROGBITS)
	    continue;
	if (!(sh->sh_flags & SHF_ALLOC))
	    continue;
	if (len == 0)
	    continue;

	/*
	* First argument is base in file, third is offset
	* in both fpga and file, so adjust file base (hack)
	*/
	if (copy_urv(data + off, noload, len, ram))
	    return -1;
    }

    return 0;
}


int load_urv(char *fname, int noload)
{
    void *buf;
    FILE *f;
    int iself, ret;

    f=fopen(fname,"rb");
    if (!f) {
	fprintf(stderr, "%s: %s: %s\n", prgname,
		fname, strerror(errno));
	return -1;
    }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);

    buf = malloc(size + 4);
    ret = fread(buf, 1, size, f);
    fclose(f);
    if (ret != size) {
	fprintf(stderr, "%s: %s: read error (\n", prgname, fname);
	return -1;
    }

    /* Detect ELF file by reading the ELF header */
    if (!memcmp(buf, ELFMAG, SELFMAG))
	iself = 1;
    /* Search for "WRPC" string @ 0x10 to detect BIN file */
    else if (!memcmp(&((char *)buf)[16], "\x57\x52\x50\x43", 4))
	iself = 0;
    /* File not ELF nor BIN, so it is not recognized */
    else {
	fprintf(stderr, "%s: %s: Unrecognized file type\n", prgname,
		fname);
	return -1;
    }

    /*
     * If ELF, we need to call the function even if (noload)
     * because the function parses ELF and sets global variables.
     * To the same to the binary loader for symmetry.
     */
    if (iself) {
	printf("Loading ELF file \"%s\" (size: %d bytes)\n",
	       basename(fname), size);
	ret = copy_urv_elf(buf, noload, size);
    } else {
	    printf("Loading BIN file \"%s\" (size: %d bytes)\n",
		   basename(fname), size);
	    ret = copy_urv(buf, noload, size, 0);
    }

    free(buf);
    return ret;
}

/* Set, or read, a variable. We already loaded to memory the file */
static int varaction_urv(char *fname, char *action)
{
    char vname[64], sname[64];
    char stmp[256];
    int i, write, vvalue, saddr;
    FILE *f;
    char eq;

    if (!global_strptr) {
	    fprintf(stderr, "%s: Can't execute \"%s\" on a non-elf file\n",
		    prgname, action);
	    return -1;
    }

    i = sscanf(action, "%[^=]%c%i", vname, &eq, &vvalue);
    if (i < 2 || eq != '=') {
	fprintf(stderr, "%s: Can't parse action \"%s\"\n",
		prgname, action);
	return -1;
    }

    if (i == 3)
	write = 1;
    else
	write = 0;

    /* Open "nm" (lazy me)" to find the variable's address */
    sprintf(stmp, "nm %s", fname);
    f = popen(stmp, "r");
    if (!f) {
	fprintf(stderr, "%s: Can't run \"%s\" (%s)\n",
		prgname, stmp, strerror(errno));
	return -1;
    }

    while (fgets(stmp, sizeof(stmp), f)) {
	if (sscanf(stmp, "%x %*c %s", &saddr, sname) != 2)
	    continue;
	if (!strcmp(vname, sname))
	    break;
    }

    if (feof(f)) {
	fprintf(stderr, "%s: no symbol \"%s\" in \"%s\"\n",
		prgname, vname, fname);
	pclose(f);
	return -1;
    }
    pclose(f);

    /* NOTE: we must not convert endianness here: it's bitwise ok */
    if (write) {
	/* FIXME: check it is in a writable section */
	urv_write_iram(vvalue, saddr>>2);
    } else {
	vvalue = urv_read_iram(saddr>>2);
	printf("%s = %i (0x%08x)\n", vname, vvalue, vvalue);
    }

    return 0;
}

int dump_urv(char *fname, int count)
{
    FILE *f;
    int i;
    uint32_t v;

    f=fopen(fname,"w");
    if (!f) {
	fprintf(stderr, "%s: %s: %s\n", prgname,
		fname, strerror(errno));
	return -1;
    }

    if (!count)
        count = URV_RAM_SIZE_WORD;

    for(i = 0; i < count; i++) {
        v = urv_read_iram(i);
        fwrite(&v, sizeof(v), 1, f);
    }

    fclose(f);

    return 0;
}

int main(int argc, char **argv)
{
    int fdmem, ret;
    int i;
    int noload = 0;
    int dump = 0;
    int count = 0;

    prgname = argv[0];
    if (argc > 1 && !strcmp(argv[1], "-n")) {
	noload = 1;
	argv++;
	argc--;
    } else if (argc > 1 && !strcmp(argv[1], "-d")) {
	dump = 1;
	count = strtol(argv[2], NULL, 10);
	printf("Dumping %u bytes from URV IRAM into %s\n", count, argv[3]);
	argv += 2;
	argc -= 2;
    }

    if (argc < 2) {
	fprintf(stderr, "%s: Use: \"%s [-n] [-d] <filename> "
		"[<var>=<value> ...]\"\n", prgname, prgname);
    }

    setbuffer(stdout, NULL, 0);
    if ((fdmem = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
	fprintf(stderr, "%s: /dev/mem: %s\n", prgname, strerror(errno));
	exit(1);
    }

    base_fpga = mmap(0, SIZE_FPGA, PROT_READ | PROT_WRITE, MAP_SHARED, fdmem,
		     BASE_FPGA);
    close(fdmem);

    if (base_fpga == MAP_FAILED) {
	fprintf(stderr, "%s: mmap(/dev/mem): %s\n",
		prgname, strerror(errno));
	exit(1);
    }

    if (!noload)
	urv_reset(1);

    if (!dump) {
	ret = load_urv(argv[1], noload);
	if (!ret)
	    printf("File loaded with success\n");

	for (i = 2; i < argc; i++)
	    if (varaction_urv(argv[1], argv[i]))
		exit(1);
    } else {
        ret = dump_urv(argv[1], count);
	    printf("File dumped with success\n");
    }
	
    if (ret)
	exit(1);

    if (!noload)
	urv_reset(0);

    return 0;
}
