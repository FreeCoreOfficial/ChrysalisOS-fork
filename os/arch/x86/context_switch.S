    .text
    .globl context_switch
    .type context_switch, @function

/* context_switch:
 *  args:
 *    %eax = address_of (task_t->kstack_ptr) for previous task  (we write current ESP there)
 *    %ebx = address_of (task_t->kstack_ptr) for next task      (we read ESP from there)
 *
 *  Effect: save all regs+eflags (via pushfl/pushal) by writing current ESP into *%eax,
 *          then load *%ebx into ESP and restore registers (popal/popfl) and return.
 */
context_switch:
    /* Get arguments from the stack (cdecl convention)
       4(%esp) = &prev->kstack_ptr
       8(%esp) = &next->kstack_ptr
     */
    movl 4(%esp), %eax
    movl 8(%esp), %edx

    cli                 /* disable interrupts during the very short switch */

    pushfl              /* push EFLAGS */
    pushal              /* push general regs */

    /* save pointer to the saved frame into prev->kstack_ptr  */
    /* current ESP points to the frame pushed by pushal/pushfl */
    movl %esp, (%eax)

    /* load next->kstack_ptr into ESP (switch stacks) */
    movl (%edx), %esp

    popal               /* restore registers for next task */
    popfl               /* restore flags for next task */

    sti                 /* re-enable interrupts */
    ret
