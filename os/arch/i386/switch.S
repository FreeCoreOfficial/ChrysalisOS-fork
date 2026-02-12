/* +build !go */
/* =========================================================================
 * ChrysalisOS Context Switch (arch/i386/switch.S)
 * =========================================================================
 * This is a GNU Assembler (GAS) file for x86 architecture.
 */

.text
.global context_switch
.type context_switch, @function

/* void context_switch(void **prev_esp_addr, void **next_esp_addr)
 * 
 * This function performs a cooperative context switch by saving the
 * current task's registers and flags onto its stack, saving the stack
 * pointer, and then loading the next task's stack pointer and restoring
 * its registers and flags.
 */
context_switch:
    /* 1. Save current task state */
    pushfl        /* Save EFLAGS */
    pushal        /* Save all general purpose registers */

    /* 2. Retrieve parameters from stack 
     * Stack layout after pushes:
     * [esp + 44] : Arg 2 (next_esp_addr)
     * [esp + 40] : Arg 1 (prev_esp_addr)
     * [esp + 36] : Return Address
     * [esp + 32] : Saved EFLAGS
     * [esp + 0-31] : Saved Registers (EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX)
     */
    movl 40(%esp), %eax    /* eax = address of prev task's kstack_ptr field */
    movl 44(%esp), %edx    /* edx = address of next task's kstack_ptr field */

    /* 3. Switch Stacks */
    movl %esp, (%eax)      /* Save current ESP to old task's structure */
    movl (%edx), %esp      /* Load new ESP from new task's structure */

    /* 4. Restore next task state */
    popal         /* Restore registers */
    popfl         /* Restore EFLAGS */

    /* 5. Return to new task */
    ret
