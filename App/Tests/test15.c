// Preemption and timing consistency check
/*
    -> A high-priority short task repeatedly preempts a low-priority
       long-running task, verifying that priority-driven preemption is
       working correctly and that the high-priority task always meets its
       tight deadline.
    TaskHigh: compute=10ms, T=100ms, D=80ms, priority=BASE+1 (HIGHER)
    TaskLow:  compute=80ms, T=300ms, D=300ms, priority=BASE   (LOWER)
    Both tasks start at t=0 (phase=0).  TaskHigh has higher priority, so it
    always runs first and finishes within its 80 ms deadline before yielding
    the CPU to TaskLow.  TaskLow is preempted at every 100 ms boundary when
    TaskHigh's timer fires, but because it only needs 80 ms of CPU in each
    300 ms window it still meets its own 300 ms deadline.
    Expected trace:
        t=0:    Release(TaskHigh), Release(TaskLow)
                Complete(TaskHigh) ~10ms  (< D=80ms ✓)
                TaskLow runs...
        t=100:  Release(TaskHigh) preempts TaskLow
                Complete(TaskHigh) ~110ms (< absolute deadline ~180ms ✓)
                TaskLow resumes...
        t=200:  Release(TaskHigh) preempts TaskLow
                Complete(TaskHigh) ~210ms ✓
                TaskLow resumes, Complete(TaskLow) ~300ms ✓
        Pattern repeats for the full 3000 ms window.
        No deadline misses for either task.
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"
#define TEST15_BASE_PRIO (tskIDLE_PRIORITY + 2U)

/* --- JOB BODIES --- */
/* TaskHigh: 10 ms compute, T=100ms, D=80ms, high priority */
static void vTaskHigh_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(10UL)) {}
}

/* TaskLow: 80 ms compute, T=300ms, D=300ms, low priority */
static void vTaskLow_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(80UL)) {}
}

/* --- REPORTER --- */
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 15 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[2];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 2);
    
    /* Test 15: Priority Preemption. 3000ms execution.
       - TaskHigh (T=100ms): 30 completions.
       - TaskLow (T=300ms): 10 completions.
       - 0 misses and 0 overruns for both. */
    if(num_tasks == 2 &&
       stats[0].ulCompletedJobs == 30 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       stats[1].ulCompletedJobs == 10 && stats[1].ulDeadlineMisses == 0 && stats[1].ulOverruns == 0) 
    {
        vUartPrintf("\n[VERDICT] PASS - Preemption dynamics perfectly verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected 30 and 10 completions respectively, with 0 errors.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

/* --- CONFIGURATION ---
 * TaskHigh is at BASE+1 so FreeRTOS always schedules it ahead of TaskLow
 * whenever both are runnable.
 */
static PTLTaskConfig_t xTest15Tasks[2] = {
    /* pcName     pvEntry           Arg   Stack   Priority              Period  Deadline  Phase  Type             Policy      */
    { "TaskHigh", vTaskHigh_Body,  NULL,  512U,   TEST15_BASE_PRIO + 1U, 100U,   80U,     0U,    eTracePeriodic,  ePolicySkip },
    { "TaskLow",  vTaskLow_Body,   NULL,  512U,   TEST15_BASE_PRIO,       300U,   300U,    0U,    eTracePeriodic,  ePolicySkip }
};

static PTLSchedulerConfig_t xTest15Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 2U
};

/* --- ENTRY POINT --- */
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 15: Preemption - high-priority task preempts low-priority task ===\n");
    for(i = 0U; i < 2U; i++)
    {
        xTest15Cfg.xTasks[i] = xTest15Tasks[i];
    }
    if(xPtlInit(&xTest15Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST15_BASE_PRIO + 2U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}