// Fiber context switch for win32_compat.c (aarch64): saves the callee-saved registers of AAPCS64 on the current stack.
//   void compat_fiber_switch(void **save_sp, void *new_sp)
// A new fiber's stack holds a frame made by CreateFiberEx: x19 = Fiber *, x30 = compat_fiber_entry.
    .text
    .globl compat_fiber_switch
    .type compat_fiber_switch, %function
    .p2align 4
compat_fiber_switch:
    sub sp, sp, #176
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8, d9, [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mov x2, sp
    str x2, [x0]
    mov sp, x1
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp d8, d9, [sp, #96]
    ldp d10, d11, [sp, #112]
    ldp d12, d13, [sp, #128]
    ldp d14, d15, [sp, #144]
    add sp, sp, #176
    ret
    .size compat_fiber_switch, .-compat_fiber_switch

    .globl compat_fiber_entry
    .type compat_fiber_entry, %function
    .p2align 4
compat_fiber_entry:
    mov x0, x19
    mov x29, #0
    mov x30, #0
    b compat_fiber_start
    .size compat_fiber_entry, .-compat_fiber_entry
    .section .note.GNU-stack,"",%progbits
