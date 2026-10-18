// Aperiodic tasks - normal execution with Polling Server
/*
    -> Polling Server: period=500ms, capacity=300ms, OVERRUN policy
       JobA: compute=30ms, soft_deadline=100ms, priority=10 (served first)
       JobB: compute=50ms, soft_deadline=100ms, priority=5  (served second)
       Both jobs are submitted before the first server activation and
       complete within their soft deadlines and budget.
    Expected trace:
       Release (PollSrv),
       ServerRefill (PollSrv, budget replenished),
       Release (JobA), Complete (JobA),
       Release (JobB), Complete (JobB),
       ServerRefill (PollSrv, activation over)
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"

#define TEST06_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
// JobA: 30 ms compute, soft_deadline = 100 ms, priority = 10 (higher)
static void vJobA_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(30UL)) {}
}

// JobB: 50 ms compute, soft_deadline = 100 ms, priority = 5 (lower)
static void vJobB_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(50UL)) {}
}

// --- SUBMITTER TASK ---
// Runs at higher priority than the Polling Server so both jobs are
// in the queue before the server's first activation.
static void vSubmitterTask(void *pvArgs)
{
    (void)pvArgs;
    // Submit JobA: priority=10, soft_deadline=100ms, compute=30ms
    xAperiodicSubmitJob(vJobA_Body, NULL, "JobA",
                        pdMS_TO_TICKS(100UL), 10U);
    // Submit JobB: priority=5, soft_deadline=100ms, compute=50ms
    xAperiodicSubmitJob(vJobB_Body, NULL, "JobB",
                        pdMS_TO_TICKS(100UL), 5U);
    vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 06 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    
    /* Test 6: Normal execution. 
       - Server should not miss its deadline or overrun its period.
       - Aperiodic jobs should not miss soft deadlines or be killed.
       - Budget should not be exhausted. */
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       aperiodic_overruns == 0 && aperiodic_kills == 0 && server_exhausts == 0) {
        vUartPrintf("\n[VERDICT] PASS - Normal aperiodic execution verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Unexpected errors in normal execution.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// --- POLLING SERVER CONFIGURATION ---
static PSConfig_t xTest06PSCfg = {
    .uxPriority    = TEST06_BASE_PRIO,
    .ulStackSize  = 512U,
    .ulPeriodMs   = 500U,
    .ulCapacityMs = 300U,
    .xPolicy      = PS_POLICY_OVERRUN
};

// --- PTL TASK CONFIGURATION ---
// The Polling Server is registered as a periodic task in the PTL.
static PTLTaskConfig_t xTest06Tasks[1] = {
    /* pcName     Entry                Arg   Stack   Priority          Period   Deadline Phase  Type                 Policy      */
    { "PollSrv",  vPollingServer_Task, NULL, 512U,   TEST06_BASE_PRIO, 500U,   0U,       0U,    eTracePollingServer, ePolicySkip }
};
static PTLSchedulerConfig_t xTest06Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 1U
};

// --- ENTRY POINT ---
int main(void)
{
    vUartInit();
    vUartPrintf("\n=== TEST 06: Aperiodic tasks - normal execution ===\n");
    // Initialise the aperiodic job queue
    vAperiodicInit();
    // Initialise the Polling Server (creates worker task and semaphores)
    if(xPollingServer_Init(&xTest06PSCfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPollingServer_Init failed\n");
        for(;;) {}
    }

    xTest06Cfg.xTasks[0] = xTest06Tasks[0];
    if(xPtlInit(&xTest06Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }

    // Submitter runs at TEST06_BASE_PRIO+1, before the PS (TEST06_BASE_PRIO)
    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL,
                TEST06_BASE_PRIO + 1U, NULL);
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL,
                TEST06_BASE_PRIO + 2U, NULL);

    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}
