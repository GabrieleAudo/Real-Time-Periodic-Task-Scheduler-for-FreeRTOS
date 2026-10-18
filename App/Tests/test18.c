// Polling Server stress - max queue depth with KILL policy
/*
    -> Polling Server: period=300ms, capacity=150ms, KILL policy
    -> Ten jobs are submitted at t0 (queue length limit), alternating
       long and short executions to stress repeated kills and backlog.
       Long jobs: compute 80ms, soft_deadline=30ms -> KILL
       Short jobs: compute 20ms, soft_deadline=80ms -> Complete
    -> The first activation drains the highest-priority six jobs
       (3 long + 3 short) and exhausts the budget.
       Remaining jobs are served in the next activation.
       
    *CORNER CASE NOTE (JobS3)*:
       JobS3 starts at tick 130 and requires 20ms. The server has exactly 20ms
       budget remaining (total 150ms). Because of the exact tie, the FreeRTOS 
       timeout expires at exactly the same tick the job finishes. The server wakes
       up first, sees budget exhausted, and kills JobS3 before it can register completion.
       Therefore, we expect 6 Kills and 4 Completions total.

    Expected trace:
        Activation 1:
            ServerRefill
            Release(JobL1) -> AperiodicKill(JobL1)
            Release(JobS1) -> Complete(JobS1)
            Release(JobL2) -> AperiodicKill(JobL2)
            Release(JobS2) -> Complete(JobS2)
            Release(JobL3) -> AperiodicKill(JobL3)
            Release(JobS3) -> AperiodicKill(JobS3) -> ServerExhaust
        Activation 2:
            ServerRefill
            Release(JobL4) -> AperiodicKill(JobL4)
            Release(JobS4) -> Complete(JobS4)
            Release(JobL5) -> AperiodicKill(JobL5)
            Release(JobS5) -> Complete(JobS5)
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"

#define TEST18_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
// Long job: 80 ms compute
static void vJobLong_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(80UL)) {}
}
// Short job: 20 ms compute
static void vJobShort_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}

// --- SUBMITTER TASK ---
// Pre-loads the queue to its maximum depth before the first activation.
static void vSubmitterTask(void *pvArgs)
{
    (void)pvArgs;
    if(xAperiodicSubmitJob(vJobLong_Body, NULL, "JobL1",
                           pdMS_TO_TICKS(30UL), 10U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobL1 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobShort_Body, NULL, "JobS1",
                           pdMS_TO_TICKS(80UL), 9U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobS1 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobLong_Body, NULL, "JobL2",
                           pdMS_TO_TICKS(30UL), 8U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobL2 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobShort_Body, NULL, "JobS2",
                           pdMS_TO_TICKS(80UL), 7U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobS2 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobLong_Body, NULL, "JobL3",
                           pdMS_TO_TICKS(30UL), 6U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobL3 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobShort_Body, NULL, "JobS3",
                           pdMS_TO_TICKS(80UL), 5U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobS3 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobLong_Body, NULL, "JobL4",
                           pdMS_TO_TICKS(30UL), 4U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobL4 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobShort_Body, NULL, "JobS4",
                           pdMS_TO_TICKS(80UL), 3U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobS4 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobLong_Body, NULL, "JobL5",
                           pdMS_TO_TICKS(30UL), 2U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobL5 submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJobShort_Body, NULL, "JobS5",
                           pdMS_TO_TICKS(80UL), 1U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobS5 submit failed\n");
        for(;;) {}
    }
    vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(2500UL));
    vUartPrintf("\n--- TEST 18 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    uint32_t total_completions = ulTraceGetEventCount(eEvtComplete);
    
    /* Test 18: Maximum Queue Depth with KILL policy
       - 10 jobs in queue. 5 Long jobs, 5 Short jobs.
       - First activation completes 2 short jobs (S1, S2), kills 3 long jobs (L1, L2, L3).
         When JobS3 runs, it starts at tick 130 and needs 20ms. The remaining budget is exactly 20ms.
         Because of the exact timeout tie (150ms), the Polling Server wakes up from the timeout
         and kills JobS3 due to budget exhaustion BEFORE JobS3 can register its completion.
       - Second activation completes 2 short jobs (S4, S5), kills 2 long jobs (L4, L5).
       - Expected Kills: 6 (L1, L2, L3, S3, L4, L5). Expected Completions: 4 (S1, S2, S4, S5). 
       - Expected Exhausts: 1. Expected Overruns: 0 (KILL policy). */
    if(num_tasks == 1 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       aperiodic_overruns == 0 && aperiodic_kills == 6 && server_exhausts == 1 &&
       total_completions == 4) 
    {
        vUartPrintf("\n[VERDICT] PASS - Max queue depth and KILL backlog verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 6 kills, 4 completions, 1 exhaust.\n");
    }
    
    vPtlStop();
    for(;;) {}
}
// --- POLLING SERVER CONFIGURATION ---
static PSConfig_t xTest18PSCfg = {
    .uxPriority    = TEST18_BASE_PRIO,
    .ulStackSize  = 512U,
    .ulPeriodMs   = 300U,
    .ulCapacityMs = 150U,
    .xPolicy      = PS_POLICY_KILL
};

// --- PTL TASK CONFIGURATION ---
static PTLTaskConfig_t xTest18Tasks[1] = {
    /* pcName    pvEntry              Arg   Stack   Priority          Period  Deadline  Phase  Type                 Policy      */
    { "PollSrv", vPollingServer_Task, NULL, 512U,   TEST18_BASE_PRIO, 300U,   0U,       0U,    eTracePollingServer, ePolicySkip }
};

static PTLSchedulerConfig_t xTest18Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 1U
};

// --- ENTRY POINT ---
int main(void)
{
    vUartInit();
    vUartPrintf("\n=== TEST 18: Polling Server stress - max queue depth with KILL ===\n");
    vAperiodicInit();
    
    if(xPollingServer_Init(&xTest18PSCfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPollingServer_Init failed\n");
        for(;;) {}
    }
    
    xTest18Cfg.xTasks[0] = xTest18Tasks[0];
    if(xPtlInit(&xTest18Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    
    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST18_BASE_PRIO + 1U, NULL);
    
    xTaskCreate(vReporterTask,  "Reporter",  512U, NULL, TEST18_BASE_PRIO + 2U, NULL);
    
    vPtlStart();
    
    vTaskStartScheduler();
    for(;;) {}
}