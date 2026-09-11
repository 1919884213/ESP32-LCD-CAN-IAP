    .syntax unified
    .cpu    cortex-m3
    .thumb

/* =====================================================================
 * PendSV_Handler —— 上下文切换（项目灵魂）
 * ---------------------------------------------------------------------
 * 触发方式：SCB->ICSR |= PENDSVSET（os_start / os_yield / os_tick 置位）
 *
 * 为什么用 PendSV？优先级设最低(15)，等所有更高中断退出后再切换，
 * 防止在中断处理中切换导致重入/丢现场。
 *
 * 16 字栈帧：
 *   硬件自动压 8 字（进异常时 CPU 自己做）: xPSR, PC, LR, R12, R3, R2, R1, R0
 *   软件手动压 8 字（本函数 STMDB 做）    : R11, R10, R9, R8, R7, R6, R5, R4
 *
 * 任务跑在 PSP（每任务一栈），中断跑在 MSP —— 换 PSP 就是换任务
 * ===================================================================== */

    .global PendSV_Handler      /* 导出符号：覆盖 startup 的 weak，供向量表使用 */
    .thumb_func                 /* 标记 Thumb 函数（bit0=1） */
    .type   PendSV_Handler, %function
PendSV_Handler:
    /* ===== 判断是否首次启动 ===== */
    LDR     R1, =current_tcb    /* R1 = current_tcb 变量的【地址】 （= 是取地址） */
    LDR     R2, [R1]            /* R2 = current_tcb 的值 = 当前任务 TCB 指针 */
    CBZ     R2, PendSV_restore  /* R2==0（还没当前任务）→ 跳过保存，直接恢复首帧 */

    /* ===== 保存当前任务上下文（只存 R4-R11，硬件已自动存另外 8 字） ===== */
    MRS     R0, PSP             /* R0 = 当前任务栈指针（任务跑在 PSP） */
    STMDB   R0!, {R4-R11}       /* 软件压 R4~R11：先减地址再存（栈向下长），R0 指向 R4 保存区 */
    STR     R0, [R2, #0]        /* task2->sp = R0（R2=current_tcb 的值=当前 TCB，sp 是第1成员偏移0） */

PendSV_restore:
    /* ===== 调度：选最高就绪任务 ===== */
    PUSH    {LR}                /* 保护 LR（BL 会改写它，LR 里是 EXC_RETURN 不能丢） */
    BL      os_schedule         /* 调 C：位图 O(1) 选任务 → 更新 current_tcb */
    POP     {LR}                /* 恢复 LR（还是那个 EXC_RETURN 魔法值） */

    /* ===== 恢复新任务上下文 ===== */
    LDR     R1, =current_tcb    /* 最高优先级任务 */
    LDR     R1, [R1]            /* R1 = 新任务 TCB 指针 */
    LDR     R0, [R1, #0]        /* R0 = 新任务的 sp（指向 R4 保存区） */
    LDMIA   R0!, {R4-R11}       /* 弹 R4~R11：先取后增，R0 自动前进到硬件压栈区 */
    MSR     PSP, R0             /* 切栈：PSP = 新任务栈（下次中断从这存） */
    BX      LR                  /* 异常返回：硬件自动弹 xPSR/PC/LR/R12/R3-R0 → 新任务开跑 */

    .size   PendSV_Handler, .-PendSV_Handler  /* 记录函数大小（调试/链接用） */

/* =====================================================================
 * SVC_Handler —— 启动第一个任务（只在 os_start 里用一次）
 * FreeRTOS 同款：SVC 异常里恢复首个任务的首帧，替代 PendSV 首次启动
 *（避免 PendSV 与 SysTick 的优先级交互导致任务误跑 MSP）
 * ===================================================================== */
    .global SVC_Handler
    .thumb_func
    .type   SVC_Handler, %function
SVC_Handler:
    LDR     R0, =current_tcb
    LDR     R0, [R0]            /* R0 = current_tcb（os_start 已预选） */
    LDR     R0, [R0, #0]        /* R0 = current_tcb->sp（首任务栈） */
    LDMIA   R0!, {R4-R11}       /* 弹首帧软件 8 字 */
    MSR     PSP, R0             /* PSP = 首帧硬件区 */
    BX      LR                  /* 异常返回：弹硬件 8 字 → 任务从入口开跑 */

    .size   SVC_Handler, .-SVC_Handler

    .end                        /* 汇编文件结束 */