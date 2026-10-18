// Polling Server - OVERRUN policy on soft deadline miss
/*
    -> Polling Server: period 500ms, capacity=400ms, OVERRUN policy
    JobX: compute 150 ms, soft deadline = 80 ms, priority = 5
    JobX exceeds its soft deadline (150 > 80). With Overrun 
    policy the server lets the job finish and logs the miss
    Excepted trace: 
        Release(PollSrv),
        ServerRefill (PollSrv, budget replenished),
        Release (Jobx)
        AperiodicOverrun(JobX, deadline missed - allowed to continue)
        Complete (JobX, finished after actual execution),
        ServerRefill (PollSrv, activation over)
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "aperiodic_tasks.h"
#include "PollingServer.h"
#include "uart.h"

#define TEST07_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES --- 

// JobX: 150 ms compute, soft_deadline = 80 ms -> deadline miss 
static void vJobX_Body(void *pvArgs){
    (void) pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(150UL)) {}
}

// --- SUBMITTER TASK --- 
// runs before the Polling Server's first activation to pre-load the queue
static void vSubmitterTask(void *pvArgs){
    (void)pvArgs;

    // submit JobX: priority=5, soft_deadline=80ms, compute=150ms (will overrun)
    xAperiodicSubmitJob(vJobX_Body, NULL, "JobX", pdMS_TO_TICKS(80UL), 5U);
    vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs){
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 07 TRACE DUMP --- \n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
    uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
    uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
    
    /* Test 7: Polling Server OVERRUN policy.
       - JobX takes 150ms, Soft deadline is 80ms.
       - It should generate exactly 1 Aperiodic Overrun, and 0 Kills.
       - The server itself should remain perfectly stable. */
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       aperiodic_overruns == 1 && aperiodic_kills == 0 && server_exhausts == 0) {
        vUartPrintf("\n[VERDICT] PASS - Aperiodic overrun policy verified (1 soft miss, 0 kills).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 1 Aperiodic Overrun and 0 kills.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// --- Polling server configuration ---
static PSConfig_t xTest07PSCfg = {
    .uxPriority = TEST07_BASE_PRIO,
    .ulStackSize = 512U,
    .ulPeriodMs = 500U,
    .ulCapacityMs = 400U,
    .xPolicy = PS_POLICY_OVERRUN
};

// -- PTL TASK CONFIGURATION --
static PTLTaskConfig_t xTest07Tasks[1] = {
    /* pcName     pvEntry              Arg   Stack   Priority          Period   Deadline  Phase  Type                Policy      */
    { "PollSrv",  vPollingServer_Task, NULL, 512U,   TEST07_BASE_PRIO, 500U,   0U,       0U,    eTracePollingServer, ePolicySkip }
};

static PTLSchedulerConfig_t xTest07Cfg = {
    .xPolicy = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks = 1U
};

// -- ENTRY POINT --
int main(void){
    vUartInit();
    vUartPrintf("\n=== TEST 07: Polling Server - Overrun policy ===\n");
    
    vAperiodicInit();

    if(xPollingServer_Init(&xTest07PSCfg) != pdPASS){
        vUartPrintf("Error: xPollingServer_init failed \n");
        for(;;) {};
    }

    xTest07Cfg.xTasks[0] = xTest07Tasks[0];

    if(xPtlInit(&xTest07Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed \n");
        for(;;) {}
    }

    xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST07_BASE_PRIO + 1U, NULL);
    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST07_BASE_PRIO + 2U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}

