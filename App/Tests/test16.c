// Timing consistency stress test - high-frequency tasks over many activations
/*
    -> Three tasks with short periods accumulate many activations over 3000 ms,
       verifying that the scheduler remains stable and no timing drift causes
       deadline misses after prolonged operation.
    TaskA: compute=15ms, T=50ms,  D=50ms   (60 activations in 3000 ms)
    TaskB: compute=20ms, T=75ms,  D=75ms   (40 activations in 3000 ms)
    TaskC: compute=25ms, T=100ms, D=100ms  (30 activations in 3000 ms)
    CPU utilization: 15/50 + 20/75 + 25/100 = 0.30 + 0.27 + 0.25 = 0.82
    The system is schedulable (U < 1) with substantial margin.
    Expected trace:
        All 130 activations complete within their respective deadlines.
        No OverrunSkip, OverrunKill, or DeadlineMiss events appear.
        Statistics printed by vPtlStop() show zero deadline misses and
        consistent worst-case response times well below each task's period.
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"
#define TEST16_BASE_PRIO (tskIDLE_PRIORITY + 2U)

/* --- JOB BODIES --- */
/* TaskA: 15 ms compute, T=50ms, D=50ms */
static void vTaskA_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(15UL)) {}
}

/* TaskB: 20 ms compute, T=75ms, D=75ms */
static void vTaskB_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}

/* TaskC: 25 ms compute, T=100ms, D=100ms */
static void vTaskC_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(25UL)) {}
}

/* --- REPORTER --- */
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 16 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[3];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 3);
    
    /* Test 16: High-Frequency Stress Test. 3000ms execution.
       - TaskA (T=50ms): 60 completions.
       - TaskB (T=75ms): 40 completions.
       - TaskC (T=100ms): 30 completions.
       - 0 misses and 0 overruns for all. */
    if(num_tasks == 3 &&
       stats[0].ulCompletedJobs == 60 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       stats[1].ulCompletedJobs == 40 && stats[1].ulDeadlineMisses == 0 && stats[1].ulOverruns == 0 &&
       stats[2].ulCompletedJobs == 30 && stats[2].ulDeadlineMisses == 0 && stats[2].ulOverruns == 0) 
    {
        vUartPrintf("\n[VERDICT] PASS - High-frequency timing stability verified (130 total jobs).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 60, 40, and 30 completions with 0 errors.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

/* --- CONFIGURATION --- */
static PTLTaskConfig_t xTest16Tasks[3] = {
    /* pcName   pvEntry        Arg   Stack   Priority          Period  Deadline  Phase  Type             Policy      */
    { "TaskA",  vTaskA_Body,  NULL,  512U,   TEST16_BASE_PRIO, 50U,    50U,      0U,    eTracePeriodic,  ePolicySkip },
    { "TaskB",  vTaskB_Body,  NULL,  512U,   TEST16_BASE_PRIO, 75U,    75U,      0U,    eTracePeriodic,  ePolicySkip },
    { "TaskC",  vTaskC_Body,  NULL,  512U,   TEST16_BASE_PRIO, 100U,   100U,     0U,    eTracePeriodic,  ePolicySkip }
};

static PTLSchedulerConfig_t xTest16Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 3U
};

/* --- ENTRY POINT --- */
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 16: Timing consistency - high-frequency tasks over many activations ===\n");
    for(i = 0U; i < 3U; i++)
    {
        xTest16Cfg.xTasks[i] = xTest16Tasks[i];
    }

    if(xPtlInit(&xTest16Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }

    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST16_BASE_PRIO + 1U, NULL);
    
    vPtlStart();
    
    vTaskStartScheduler();
    for(;;) {}
}