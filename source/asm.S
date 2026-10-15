    .syntax unified
    .arm
    .text

@ Import sin implementar: el thunk deja su índice en r12. Registra el nombre y devuelve 0
@ (r0 = r1 = 0, que también sirve como "nil" para ObjC y 0 para retornos de 64 bits).
    .global ml_unimpl_entry
    .type ml_unimpl_entry, %function
ml_unimpl_entry:
    push    {r0-r4, lr}
    mov     r0, r12
    blx     ml_unimpl_log
    pop     {r0-r4, lr}
    mov     r0, #0
    mov     r1, #0
    bx      lr

@ uint32_t call_guest(void *fn, a0, a1, a2, a3): llama al juego guardando r9, que el código
@ iOS 3 trata como temporal.
    .global call_guest
    .type call_guest, %function
call_guest:
    push    {r4, r9, r10, lr}
    mov     r12, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp, #16]
    blx     r12
    pop     {r4, r9, r10, pc}
