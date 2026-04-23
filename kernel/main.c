#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "platform.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    printf("kinit()\n");
    kinit();         // physical page allocator
    printf("kvminit()\n");
    kvminit();       // create kernel page table
    printf("kvminithart()\n");
    kvminithart();   // turn on paging
    printf("procinit()\n");
    procinit();      // process table
    printf("trapinit()\n");
    trapinit();      // trap vectors
    printf("trapinithart()\n");
    trapinithart();  // install kernel trap vector
    printf("platform_irq_init()\n");
    platform_irq_init();      // set up interrupt controller
    printf("platform_irq_init_hart()\n");
    platform_irq_init_hart(); // ask the platform for device interrupts
    printf("binit()\n");
    binit();         // buffer cache
    printf("iinit()\n");
    iinit();         // inode table
    printf("fileinit()\n");
    fileinit();      // file table
    printf("disk_init()\n");
    platform_disk_init();
    printf("userinit()\n");
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    platform_irq_init_hart();
  }

  scheduler();        
}
