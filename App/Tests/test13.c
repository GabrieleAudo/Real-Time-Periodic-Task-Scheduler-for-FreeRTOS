// Stress test - overlapping periodic tasks with concurrent overruns under all three policies 
/*
    -> Three tasks share the CPU. On iteration 2 every task overruns its
    period simultaneously, exercising all three overrun policies at once.

    TaskA: compute=30ms normally, compute=100ms on iteration 2 (T=100ms, D=100ms, SKIP)
    TaskB: compute=40ms normally, compute=170ms on iteration 2 (T=150ms, D=150ms, KILL)
    TaskC: compute=50ms normally, compute=250ms on iteration 2 (T=200ms, D=200ms, CATCH-UP)

    At t=0 all three tasks release simultaneously.
    At t=200ms TaskA iter 3, TaskB iter 2, and taskC iter 2 all overrun at the same time
    forcing the scheduler to invoke three different policies

    Expected trace (approximately)
    t=0 Release(A), Release(B), Release(C)
    ... Complete(TaskA j1), Complete(TaskB j1), Complete(TaskC j1)
    t~100  Release(TaskA j2) - overrun -> OverrunSkip(TaskA)
    t~150  Release(TaskB j2) - overrun -> OverrunKill(TaskB) + new Release(TaskB)
    t~200  Release(TaskC j2) - overrun -> OverrunCatchUp(TaskC)
    ...    remaining activations complete normally..    Complete(TaskA j1), Complete(TaskB j1), Complete(TaskC j1)
    t~100  Release(TaskA j2) - overrun -> OverrunSkip(TaskA)
    t~150  Release(TaskB j2) - overrun -> OverrunKill(TaskB) + new Release(TaskB)
    t~200  Release(TaskC j2) - overrun -> OverrunCatchUp(TaskC)
    ...    remaining activations complete normally

*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"
#define TEST13_BASE_PRIO (tskIDLE_PRIORITY + 2U)
/* --- JOB BODIES --- */
/* TaskA: 30 ms normally, 100 ms on iteration 2 -> overruns T=100 ms (SKIP) */
static void vTaskA_Body(void *pvArgs)
{
    (void)pvArgs;
    static uint32_t ulCountA = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;
    ulCountA++;
    xDuration = (ulCountA == 2UL) ? pdMS_TO_TICKS(100UL) : pdMS_TO_TICKS(30UL);
    while((xTaskGetTickCount() - xNow) < xDuration) {}
}
/* TaskB: 40 ms normally, 170 ms on iteration 2 -> overruns T=150 ms (KILL) */
static void vTaskB_Body(void *pvArgs)
{
    (void)pvArgs;
    static uint32_t ulCountB = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;
    ulCountB++;
    xDuration = (ulCountB == 2UL) ? pdMS_TO_TICKS(170UL) : pdMS_TO_TICKS(40UL);
    while((xTaskGetTickCount() - xNow) < xDuration) {}
}
/* TaskC: 50 ms normally, 250 ms on iteration 2 -> overruns T=200 ms (CATCH-UP) */
static void vTaskC_Body(void *pvArgs)
{
    (void)pvArgs;
    static uint32_t ulCountC = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;
    ulCountC++;
    xDuration = (ulCountC == 2UL) ? pdMS_TO_TICKS(250UL) : pdMS_TO_TICKS(50UL);
    while((xTaskGetTickCount() - xNow) < xDuration) {}
}
/* --- REPORTER --- */
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 13 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[3];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 3);
    
    /* Test 13: Stress test with all 3 policies concurrently.
       - TaskA (SKIP): Overruns at least once, Misses at least once.
       - TaskB (KILL): Overruns at least once, Misses EXACTLY 0 times (killed before finishing).
       - TaskC (CATCH-UP): Overruns at least once, Misses at least once.
       - All tasks should have completed multiple jobs. */
    if(num_tasks == 3 &&
       stats[0].ulCompletedJobs > 0 && stats[0].ulOverruns >= 1 && stats[0].ulDeadlineMisses >= 1 &&
       stats[1].ulCompletedJobs > 0 && stats[1].ulOverruns >= 1 && stats[1].ulDeadlineMisses == 0 &&
       stats[2].ulCompletedJobs > 0 && stats[2].ulOverruns >= 1 && stats[2].ulDeadlineMisses >= 1) 
    {
        vUartPrintf("\n[VERDICT] PASS - Concurrent multi-policy stress signature verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected all 3 policies to trigger their distinct signatures.\n");
    }
    
    vPtlStop();
    for(;;) {}
}
/* --- CONFIGURATION --- */
static PTLTaskConfig_t xTest13Tasks[3] = {
    /* pcName   pvEntry        Arg   Stack   Priority          Period  Deadline  Phase  Type             Policy         */
    { "TaskA",  vTaskA_Body,  NULL,  512U,   TEST13_BASE_PRIO, 100U,   100U,     0U,    eTracePeriodic,  ePolicySkip    },
    { "TaskB",  vTaskB_Body,  NULL,  512U,   TEST13_BASE_PRIO, 150U,   150U,     0U,    eTracePeriodic,  ePolicyKill    },
    { "TaskC",  vTaskC_Body,  NULL,  512U,   TEST13_BASE_PRIO, 200U,   200U,     0U,    eTracePeriodic,  ePolicyCatchUp }
};
static PTLSchedulerConfig_t xTest13Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 3U
};
/* --- ENTRY POINT --- */
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 13: Stress - overlapping periodic tasks, concurrent overruns ===\n");
    for(i = 0U; i < 3U; i++)
    {
        xTest13Cfg.xTasks[i] = xTest13Tasks[i];
    }
    if(xPtlInit(&xTest13Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST13_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}