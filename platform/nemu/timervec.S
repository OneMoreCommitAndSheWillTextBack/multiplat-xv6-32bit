        #
        # machine-mode timer interrupt for NEMU.
        # The timer source is host-driven, so the guest only needs to
        # forward the event to S-mode; there is no CLINT mtimecmp to
        # reprogram on each tick.
        #
.globl timervec
.align 4
timervec:
        # start.c has set up the memory that mscratch points to:
        # scratch[0,4,8,12] : register save area.

        csrrw a0, mscratch, a0
        sw a1, 0(a0)
        sw a2, 4(a0)
        sw a3, 8(a0)
        sw a4, 12(a0)

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
