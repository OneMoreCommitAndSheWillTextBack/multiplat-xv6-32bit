        #
        # machine-mode timer interrupt for QEMU virt.
        # The platform exposes CLINT mtime/mtimecmp, so each interrupt
        # reprograms the next deadline before forwarding to S-mode.
        #
.globl timervec
.align 4
timervec:
        # start.c has set up the memory that mscratch points to:
        # scratch[0,4,8,12] : register save area.
        # scratch[16] : address of CLINT's MTIMECMP register.
        # scratch[20] : desired interval between interrupts.

        csrrw a0, mscratch, a0
        sw a1, 0(a0)
        sw a2, 4(a0)
        sw a3, 8(a0)
        sw a4, 12(a0)

        # schedule the next timer interrupt
        # by adding interval to mtimecmp.
        lw a1, 16(a0) # CLINT_MTIMECMP(hart)
        lw a2, 20(a0) # interval
        lw a3, 0(a1)
        lw a4, 4(a1) # timecmp a4:a3
        add a3, a3, a2
        sltu a2, a3, a2
        add a4, a4, a2
        sw a4, 4(a1)
        sw a3, 0(a1)

        # arrange for a supervisor software interrupt
        # after this handler returns.
        li a1, 2
        csrw sip, a1

        lw a4, 12(a0)
        lw a3, 8(a0)
        lw a2, 4(a0)
        lw a1, 0(a0)
        csrrw a0, mscratch, a0

        mret
