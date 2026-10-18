// edge case: minimal phase gap between same-period tasks 
/*
    -> three tasks share the same period (200 ms) but are staggered by 
    the smallest meaningful phase offset (1ms) to exercise the scheduler
    at the tighest possible activation separation
    TaskA: compute=20ms, T=200ms, D=200ms, phase=0ms   (releases at t0)
    TaskB: compute=20ms, T=200ms, D=200ms, phase=1ms   (releases at t0+1ms)
    TaskC: compute=20ms, T=200ms, D=200ms, phase=2ms   (releases at t0+2ms)
    The 1 ms gaps are at the resolution limit of the 1 kHz FreeRTOS tick.
    Expected trace:
        Release(TaskA) at t0,      Complete(TaskA) at ~t0+20ms
        Release(TaskB) at t0+1ms,  Complete(TaskB) at ~t0+21ms (after TaskA)
        Release(TaskC) at t0+2ms,  Complete(TaskC) at ~t0+22ms (after TaskB)
        All three activations repeat every 200 ms with no deadline misses.
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"
#define TEST14_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES --- 
/* TaskA: 20 ms compute, T=200ms, D=200ms, phase=0ms */
static void vTaskA_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
/* TaskB: 20 ms compute, T=200ms, D=200ms, phase=1ms */
static void vTaskB_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
/* TaskC: 20 ms compute, T=200ms, D=200ms, phase=2ms */
static void vTaskC_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
/* --- REPORTER --- */
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(2000UL));
    vUartPrintf("\n--- TEST 14 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[3];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 3);
    
    /* Test 14: Minimal Phase Gap (1ms). 2000ms duration.
       - TaskA (Phase 0): 10 completions.
       - TaskB (Phase 1): 10 completions.
       - TaskC (Phase 2): 10 completions.
       - All tasks should have 0 misses and 0 overruns. */
    if(num_tasks == 3 &&
       stats[0].ulCompletedJobs == 10 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       stats[1].ulCompletedJobs == 10 && stats[1].ulDeadlineMisses == 0 && stats[1].ulOverruns == 0 &&
       stats[2].ulCompletedJobs == 10 && stats[2].ulDeadlineMisses == 0 && stats[2].ulOverruns == 0) 
    {
        vUartPrintf("\n[VERDICT] PASS - Minimal phase gap execution perfectly stable.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 10 completions per task with no errors.\n");
    }
    
    vPtlStop();
    for(;;) {}
}
/* --- CONFIGURATION --- */
static PTLTaskConfig_t xTest14Tasks[3] = {
    /* pcName   pvEntry        Arg   Stack   Priority          Period  Deadline  Phase  Type             Policy      */
    { "TaskA",  vTaskA_Body,  NULL,  512U,   TEST14_BASE_PRIO, 200U,   200U,     0U,    eTracePeriodic,  ePolicySkip },
    { "TaskB",  vTaskB_Body,  NULL,  512U,   TEST14_BASE_PRIO, 200U,   200U,     1U,    eTracePeriodic,  ePolicySkip },
    { "TaskC",  vTaskC_Body,  NULL,  512U,   TEST14_BASE_PRIO, 200U,   200U,     2U,    eTracePeriodic,  ePolicySkip }
};
static PTLSchedulerConfig_t xTest14Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 3U
};
/* --- ENTRY POINT --- */
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 14: Edge case - minimal 1 ms phase gap between same-period tasks ===\n");
    for(i = 0U; i < 3U; i++)
    {
        xTest14Cfg.xTasks[i] = xTest14Tasks[i];
    }
    if(xPtlInit(&xTest14Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST14_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}
