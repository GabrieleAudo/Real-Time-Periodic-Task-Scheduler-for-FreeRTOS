// Multiple periodic tasks: one misses its deadline, the other completes normally
/*
    -> TaskA: compute varies (3rd activation=100ms >> D=80ms), T=200ms, D=80ms
              On all other activations compute=20ms < D=80ms -> ok
       TaskB: compute=20ms, T=300ms, D=300ms -> always completes on time
       The two tasks share the CPU but their activations are independent.
    Expected trace:
       TaskA: Complete, Complete, DeadlineMiss, Complete, ...
       TaskB: Complete on every activation, no misses
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST11_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
// TaskA: 3rd activation takes 100ms (> D=80ms), all others take 20ms
static void vTaskA_Body(void *pvArgs)
{
    (void)pvArgs;
    static uint32_t ulCount = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;
    ulCount++;
    xDuration = (ulCount == 3UL) ? pdMS_TO_TICKS(100UL) : pdMS_TO_TICKS(20UL);
    while((xTaskGetTickCount() - xNow) < xDuration) {}
}
// TaskB: 20 ms compute, T=300ms, D=300ms -> always completes on time
static void vTaskB_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
// --- REPORTER ---
static void vReporterTask(void *pvParams)
{
    (void)pvParams;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 11 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[2];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 2);
    
    /* Test 11: 3000ms execution.
       - TaskA (T=200ms, D=80ms): Iteration 3 takes 100ms. This misses the deadline (100 > 80)
         but does NOT overrun the period (100 < 200). 
         Expect 15 completed jobs, 1 miss, 0 overruns.
       - TaskB (T=300ms, D=300ms): Expect 10 completed jobs, 0 misses, 0 overruns. */
    if(num_tasks == 2 && 
       stats[0].ulCompletedJobs == 15 && stats[0].ulDeadlineMisses == 1 && stats[0].ulOverruns == 0 &&
       stats[1].ulCompletedJobs == 10 && stats[1].ulDeadlineMisses == 0 && stats[1].ulOverruns == 0) 
    {
        vUartPrintf("\n[VERDICT] PASS - Single deadline miss signature verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Mismatch in expected completions or misses.\n");
    }
    
    vPtlStop();
    for(;;) {}
}
// --- CONFIGURATION ---
static PTLTaskConfig_t xTest11Tasks[2] = {
    /* pcName   pvEntry       Arg   Stack   Priority          Period  Deadline  Phase  Type             Policy      */
    { "TaskA",  vTaskA_Body,  NULL, 512U,   TEST11_BASE_PRIO, 200U,   80U,      0U,    eTracePeriodic,  ePolicySkip },
    { "TaskB",  vTaskB_Body,  NULL, 512U,   TEST11_BASE_PRIO, 300U,   300U,     0U,    eTracePeriodic,  ePolicySkip }
};
static PTLSchedulerConfig_t xTest11Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 2U
};
// --- ENTRY POINT ---
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 11: Multiple periodic tasks - one deadline miss, one always ok ===\n");
    for(i = 0U; i < 2U; i++)
    {
        xTest11Cfg.xTasks[i] = xTest11Tasks[i];
    }
    if(xPtlInit(&xTest11Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST11_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}