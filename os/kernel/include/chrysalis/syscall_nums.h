/* kernel/include/chrysalis/syscall_nums.h */
#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_MALLOC 3
#define SYS_FREE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_STAT 7
#define SYS_COMPILE_AND_RUN 8

/* Window Manager Syscalls */
#define SYS_WM_CREATE_WINDOW 10
#define SYS_WM_DESTROY_WINDOW 11
#define SYS_WM_MARK_DIRTY 12
#define SYS_WM_SET_TITLE 13
#define SYS_WM_GET_POS 14
#define SYS_WM_GET_SIZE 15

/* Drawing Syscalls (FlyUI Backend) */
#define SYS_FLY_DRAW_RECT_FILL 20
#define SYS_FLY_DRAW_TEXT 21
#define SYS_FLY_DRAW_SURFACE 22
#define SYS_FLY_DRAW_BMP 23
#define SYS_FLY_DRAW_BMP_FIT 24

/* Event Syscalls */
#define SYS_GET_EVENT 30
#define SYS_SLEEP 31
#define SYS_YIELD 32
#define SYS_GET_TIME 40
#define SYS_GET_LAUNCH_ARG 41
#define SYS_CMD_EXEC_CAPTURE 42

#endif
