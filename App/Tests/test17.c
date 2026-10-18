// Polling Server stress - backlog with mixed overruns and budget exhaustion
/*
    -> Polling Server: period=200ms, capacity=80ms, OVERRUN policy
    -> Six aperiodic jobs submitted at t0 with descending priorities.
       JobA/B: compute 30ms, soft_deadline=20ms -> OVERRUN + Complete
       JobC:   compute 30ms, soft_deadline=60ms -> budget exhaustion (killed)
       JobD/E: compute 20ms, soft_deadline=30ms -> Complete
       JobF:   compute 50ms, soft_deadline=60ms -> budget exhaustion (killed)
    -> The first activation processes JobA/B then exhausts on JobC.
       Remaining jobs stay in the queue for the second activation.
    Expected trace (approximate):
        Activation 1:
            ServerRefill
            Release(JobA) -> AperiodicOverrun(JobA) -> Complete(JobA)
            Release(JobB) -> AperiodicOverrun(JobB) -> Complete(JobB)
            Release(JobC) -> AperiodicKill(JobC) -> ServerExhaust
        Activation 2:
            ServerRefill
            Release(JobD) -> Complete(JobD)
            Release(JobE) -> Complete(JobE)
            Release(JobF) -> AperiodicKill(JobF) -> ServerExhaust
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"

#define TEST17_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
// 30 ms compute
static void vJob30ms_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(30UL)) {}
}
// 20 ms compute
static void vJob20ms_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(20UL)) {}
}
// 50 ms compute
static void vJob50ms_Body(void *pvArgs)
{
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(50UL)) {}
}

// --- SUBMITTER TASK ---
// Pre-loads the queue before the Polling Server's first activation.
static void vSubmitterTask(void *pvArgs)
{
    (void)pvArgs;
    if(xAperiodicSubmitJob(vJob30ms_Body, NULL, "JobA",
                           pdMS_TO_TICKS(20UL), 10U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobA submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJob30ms_Body, NULL, "JobB",
                           pdMS_TO_TICKS(20UL), 9U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobB submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJob30ms_Body, NULL, "JobC",
                           pdMS_TO_TICKS(60UL), 8U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobC submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJob20ms_Body, NULL, "JobD",
                           pdMS_TO_TICKS(30UL), 7U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobD submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJob20ms_Body, NULL, "JobE",
                           pdMS_TO_TICKS(30UL), 6U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobE submit failed\n");
        for(;;) {}
    }
    if(xAperiodicSubmitJob(vJob50ms_Body, NULL, "JobF",
                           pdMS_TO_TICKS(60UL), 5U) != pdTRUE)
    {
        vUartPrintf("ERROR: JobF submit failed\n");
        for(;;) {}
    }
    vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs)
{
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(2500UL));
    vUartPrintf("\n--- TEST 17 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    uint32_t total_completions = ulTraceGetEventCount(eEvtComplete);
    
    /* Test 17: Polling Server Backlog Stress
       - Expected Aperiodic Overruns: 2 (JobA, JobB miss soft deadline but finish)
       - Expected Aperiodic Kills: 2 (JobC, JobF hit strict capacity limits)
       - Expected Server Exhausts: 2 (Once per activation when capacity is hit)
       - Expected Aperiodic Completions: 4 (JobA, JobB, JobD, JobE) */
    if(num_tasks == 1 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       aperiodic_overruns == 2 && aperiodic_kills == 2 && server_exhausts == 2 &&
       total_completions == 4) 
    {
        vUartPrintf("\n[VERDICT] PASS - Backlog and exhaustion dynamics verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 2 overruns, 2 kills, 2 exhausts, and 4 completions.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// --- POLLING SERVER CONFIGURATION ---
static PSConfig_t xTest17PSCfg = {
    .uxPriority    = TEST17_BASE_PRIO,
    .ulStackSize  = 512U,
    .ulPeriodMs   = 200U,
    .ulCapacityMs = 80U,
    .xPolicy      = PS_POLICY_OVERRUN
};

// --- PTL TASK CONFIGURATION ---
static PTLTaskConfig_t xTest17Tasks[1] = {
    /* pcName    pvEntry              Arg   Stack   Priority          Period  Deadline  Phase  Type                 Policy      */
    { "PollSrv", vPollingServer_Task, NULL, 512U,   TEST17_BASE_PRIO, 200U,   0U,       0U,    eTracePollingServer, ePolicySkip }
};

static PTLSchedulerConfig_t xTest17Cfg = {
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 1U
};

// --- ENTRY POINT ---
int main(void)
{
    vUartInit();
    vUartPrintf("\n=== TEST 17: Polling Server stress - backlog with overruns and exhaustion ===\n");
   
    vAperiodicInit();
    
    if(xPollingServer_Init(&xTest17PSCfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPollingServer_Init failed\n");
        for(;;) {}
    }

    xTest17Cfg.xTasks[0] = xTest17Tasks[0];
    
    if(xPtlInit(&xTest17Cfg) != pdPASS)
    {
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;) {}
    }
    
    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST17_BASE_PRIO + 1U, NULL);
    xTaskCreate(vReporterTask,  "Reporter",  512U, NULL, TEST17_BASE_PRIO + 2U, NULL);
   
    vPtlStart();
    
    vTaskStartScheduler();
    for(;;) {}
}