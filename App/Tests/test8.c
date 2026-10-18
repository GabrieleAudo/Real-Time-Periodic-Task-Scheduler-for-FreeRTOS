// Polling Server -> KILL policy on soft deadline miss
/*
    -> Polling Server: period=500ms, capacity=400ms, KILL policy
    JobKill: compute 150ms, soft_deadline=80ms, priority=10 (higher)
    JobOk: compute 30 ms, soft_deadline=100ms, priority=5 (lower)
    JobKill exceeds its soft deadline and is killed immediately.
    The server recreates the worker and continues with JobOk, 
    which completes normally within its budget and soft deadline
    Expected trace:
        Release (pollSrv),
        ServerRefill (PollSrv, budget replenished),
        Release (JobKill),
        AperiodicKill (JobKill, killed on deadline miss),
        Release (JobOk),
        Complete (JobOk),
        ServerRefill (PollSrv, activation over)
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"

#define TEST08_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---

// JobKill: 150 ms compute, soft_deadline = 80 ms -> will be killed 
static void vJobKill_Body(void *pvArgs){
    (void) pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(150UL)) {}
}

// JobOk: 30 ms compute, soft_deadline = 100 ms -> completes normally
static void vJobOk_Body(void * pvArgs){
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(30UL)) {}
}

// SUBMITTER TASK
// runs before the PS to pre-load the queue. 
static void vSubmitterTask(void * pvArgs){
    (void)pvArgs;
    // submit jobKill: priority 10, softDeadline = 80ms, compute =150ms 
    xAperiodicSubmitJob(vJobKill_Body, NULL, "JobKill", pdMS_TO_TICKS(80UL), 10U);

    // submit jobOk: priority 5, softDeadline 100ms, compute 30ms
    xAperiodicSubmitJob(vJobOk_Body, NULL, "JobOk", pdMS_TO_TICKS(100UL), 5U);

    vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs){
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 08 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END --- \n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    
    /* Test 8: Polling Server KILL policy.
       - JobKill takes 150ms, Soft deadline is 80ms.
       - It should generate exactly 1 Aperiodic Kill, and 0 Overruns (it doesn't survive to overrun).
       - The server itself should remain perfectly stable. */
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       aperiodic_overruns == 0 && aperiodic_kills == 1 && server_exhausts == 0) {
        vUartPrintf("\n[VERDICT] PASS - Aperiodic kill policy verified (0 soft misses, 1 kill).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 1 Aperiodic Kill and 0 overruns.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// --- POLLING SERVER CONFIGURATION ---
static PSConfig_t xTest08PSCfg = {
    .uxPriority = TEST08_BASE_PRIO,
    .ulStackSize = 512U,
    .ulPeriodMs = 500U,
    .ulCapacityMs = 400U,
    .xPolicy = PS_POLICY_KILL
};

// --- PTL TASK CONFIGURATION ---
static PTLTaskConfig_t xTest08Tasks[1] = {
    // pcName,  Entry                Arg,  Stack,Priority,         Period,Deadline, Phase, Type,                Policy
    {"PollSrv", vPollingServer_Task, NULL, 512U, TEST08_BASE_PRIO, 500U,  0U,       0U,    eTracePollingServer, ePolicySkip}
};

static PTLSchedulerConfig_t xTest08Cfg = {
    .xPolicy = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks = 1U
};

// --- ENTRY POINT ---
int main(void){
    vUartInit();
    vUartPrintf("\n=== TEST 08: Polling Server - KILL policy ===\n");

    vAperiodicInit();

    if(xPollingServer_Init(&xTest08PSCfg) != pdPASS){
        vUartPrintf("ERROR: xPollingServer_Init failed \n");
        for(;;) {}
    }

    xTest08Cfg.xTasks[0] = xTest08Tasks[0];

    if(xPtlInit(&xTest08Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed \n");
        for(;;) {}
    }

    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST08_BASE_PRIO + 1U, NULL);
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST08_BASE_PRIO + 2U, NULL);

    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}


