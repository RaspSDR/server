#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/auxv.h>
#include <stdio.h>
 
/*
  __vdso_clock_gettime;
  __vdso_gettimeofday;
  __vdso_clock_getres;
  __vdso_clock_gettime64;

extract information:
https://www.baeldung.com/linux/vdso-vsyscall-memory-regions
 $cat /proc/3095/maps | grep -E 'vdso|vsyscall'
 ffcaa7fe000-7ffcaa800000 r-xp 00000000 00:00 0  

 $dd if=/proc/3095/mem of=out.vdso bs=1 count=$((0x7fff5c1aa000-0x7fff5c1a8000)) skip=$((0x7fff5c1aa000)) 

 $readelf -Ws out.vdso

2: 0000049c   108 FUNC    GLOBAL DEFAULT    9 @@LINUX_2.6
3: 00000508   104 FUNC    GLOBAL DEFAULT    9 __vdso_clock_getres@@LINUX_2.6
4: 000003e0   188 FUNC    GLOBAL DEFAULT    9 @@LINUX_2.6
5: 00000328   184 FUNC    GLOBAL DEFAULT    9 @@LINUX_2.6
  */

int (*_v_clock_gettime64) (clockid_t __clock_id, struct timespec *__tp);
int (*_v_clock_getres) (clockid_t __clock_id, struct timespec *__res);

int main()
{
    unsigned long base_ptr = getauxval(AT_SYSINFO_EHDR);

    printf("Get Base Addr: %lx\n", base_ptr);
    _v_clock_gettime64 = (void*)(base_ptr + 0x3e0);
    _v_clock_getres = (void*)(base_ptr  + 0x508);

    while(1) {
	struct timespec ts;
	_v_clock_gettime64(CLOCK_MONOTONIC, &ts);

    printf("%lld\n", ts.tv_sec);
	_v_clock_gettime64(CLOCK_REALTIME, &ts);

    printf("%lld\n", ts.tv_sec);
    }

    return 0;
    
}