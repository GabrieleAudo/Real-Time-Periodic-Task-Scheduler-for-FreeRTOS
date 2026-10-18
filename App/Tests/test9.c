// Combination of periodic task and aperiodic tasks via Polling Server
/*
    -> TaskA: periodic, compute=20ms, T=200ms, D=200ms (completes on time)
    Polling Server: period=300ms, capacity=200ms, OVERRUN policy
    JobA: compute=30ms, softDeadline=100ms, priority=10 (first)
    JobB: compute=40ms, softDeadline=100ms, priority=5 (second)
    Both aperiodic submitted before the first PS activation
    Both complete within their soft deadlines and within the server budget

    Expected behaviour:
    Periodic releases and Complete events for TaskA every 200ms
    Release(PollSrv),
    ServerRefill (PollSrv, budget replenished),
    Release (JobA), Complete (JobA)
    Release (JobB), Complete (JobB)
    Server Refill (PollSrv, activation over)
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"
#define TEST09_BASE_PRIO (tskIDLE_PRIORITY + 2U)
// --- JOB BODIES ---
// TaskA: 20 ms compute, T=200ms, D=200ms -> always completes on time
static void vTaskA_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
// JobA: 30 ms compute, soft_deadline=100ms, priority=10 (higher)
static void vJobA_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(30UL)) {}
}
// JobB: 40 ms compute, soft_deadline=100ms, priority=5 (lower)
static void vJobB_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(40UL)) {}
}
// --- SUBMITTER TASK ---
// Runs at higher priority than the Polling Server so both jobs are
// in the queue before the server's first activation.
static void vSubmitterTask(void *pvArgs)
{
    (void)pvArgs;
    xAperiodicSubmitJob(vJobA_Body, NULL, "JobA", pdMS_TO_TICKS(100UL), 10U);
    xAperiodicSubmitJob(vJobB_Body, NULL, "JobB", pdMS_TO_TICKS(100UL),  5U);
    vTaskSuspend(NULL);
}
// --- REPORTER ---
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 09 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[2];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 2);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    uint32_t total_completions = ulTraceGetEventCount(eEvtComplete);
    
    /* Test 9: Combo Periodic + Aperiodic. 3000ms execution.
       - TaskA (T=200ms) completes 15 times.
       - PollSrv (T=300ms) completes 10 times.
       - 2 aperiodic jobs finish successfully (Total Completions = 15 periodic + 2 aperiodic = 17).
       - Zero errors, misses, or exhaustions. */
    if(num_tasks == 2 && 
       stats[0].ulCompletedJobs == 15 && stats[0].ulDeadlineMisses == 0 &&
       stats[1].ulCompletedJobs == 10 && stats[1].ulDeadlineMisses == 0 &&
       aperiodic_overruns == 0 && aperiodic_kills == 0 && server_exhausts == 0 && 
       total_completions == 17) 
    {
        vUartPrintf("\n[VERDICT] PASS - Combo execution perfectly stable (15 Periodic, 2 Aperiodic).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Mismatch in expected completions or errors found.\n");
    }
    
    vPtlStop();
    for(;;) {}
}
// --- POLLING SERVER CONFIGURATION ---
static PSConfig_t xTest09PSCfg = {
    .uxPriority    = TEST09_BASE_PRIO,
    .ulStackSize  = 512U,
    .ulPeriodMs   = 300U,
    .ulCapacityMs = 200U,
    .xPolicy      = PS_POLICY_OVERRUN
};
// --- PTL TASK CONFIGURATION ---
// TaskA is a periodic task; PollSrv is the Polling Server registered as periodic.
static PTLTaskConfig_t xTest09Tasks[2] = {
    /* pcName     pvEntry              Arg   Stack   Priority          Period  Deadline Phase  Type                 Policy      */
    { "TaskA",    vTaskA_Body,         NULL, 512U,   TEST09_BASE_PRIO, 200U,   200U,    0U,    eTracePeriodic,      ePolicySkip },
    { "PollSrv",  vPollingServer_Task, NULL, 512U,   TEST09_BASE_PRIO, 300U,   0U,      0U,    eTracePollingServer, ePolicySkip }
};
static PTLSchedulerConfig_t xTest09Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 2U
};
// --- ENTRY POINT ---
int main(void)
{
    uint8_t i;
    vUartInit();
    vUartPrintf("\n=== TEST 09: Combination of periodic task and aperiodic tasks ===\n");
    vAperiodicInit();
    if(xPollingServer_Init(&xTest09PSCfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPollingServer_Init failed\n");
        for(;;) {}
    }
    for(i = 0U; i < 2U; i++)
    {
        xTest09Cfg.xTasks[i] = xTest09Tasks[i];
    }
    if(xPtlInit(&xTest09Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    // Submitter at BASE_PRIO+1 to pre-load the queue before the PS activates
    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST09_BASE_PRIO + 1U, NULL);
    xTaskCreate(vReporterTask,  "Reporter",  512U, NULL, TEST09_BASE_PRIO + 2U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}