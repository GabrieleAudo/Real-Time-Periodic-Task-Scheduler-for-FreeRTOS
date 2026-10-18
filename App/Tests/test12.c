// Polling server - OVERRUN and KILL policy with budget exhaustion
/*
    -> Polling server: period 500 ms, capacity 100 ms, OVERRUN policy
    JobA: compute 40 ms, soft_deadline=30ms, priority 10 (served first)
    JobB: compute 40 ms, soft_deadline=30ms, priority 5  (served second)
    JobC: compute 40 ms, soft_deadline=15ms, priority 1  (served third)

    The soft deadlines are intentionally shorter than the compute times
    so that each job triggers a soft deadline miss, exercising the OVERRUN
    policy path.  The OVERRUN policy grants extra time (remaining budget)
    to let the job finish.  JobA and JobB complete within the extra window,
    but JobC exhausts the entire server budget and is killed.

    Budget breakdown per activation:
        JobA starts: remaining=100ms, timeout=min(30,100)=30ms
            -> 30ms expires, job still running (needs 40ms)
            -> Scenario C: soft deadline miss, OVERRUN grants extra=70ms
            -> job finishes at 40ms -> Complete  (budget used: 40ms)
        JobB starts: remaining=60ms, timeout=min(30,60)=30ms
            -> 30ms expires, job still running (needs 40ms)
            -> Scenario C: soft deadline miss, OVERRUN grants extra=30ms
            -> job finishes at 10ms into extra -> Complete  (budget used: 80ms)
        JobC starts: remaining=20ms, timeout=min(15,20)=15ms
            -> 15ms expires, job still running (needs 40ms)
            -> Scenario C: soft deadline miss, OVERRUN grants extra=5ms
            -> 5ms expires, job STILL running -> budget exhausted -> KILL

    Expected trace (first activation):
        ServerRefill (PS)
        Release (JobA), AperiodicOverrun (JobA), Complete (JobA)
        Release (JobB), AperiodicOverrun (JobB), Complete (JobB)
        Release (JobC), AperiodicOverrun (JobC), AperiodicKill (JobC),
        ServerExhaust (PS)
*/

#include "FreeRTOS.h"
#include "PollingServer.h"
#include "aperiodic_tasks.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "task.h"
#include "uart.h"

#define TEST12_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// jobA: 40 ms compute, soft_deadline=30ms, priority=10
static void vJobA_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(40UL)) {
  }
}

// JobB: 40ms compute, soft_deadline=30ms, priority=5
static void vJobB_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(40UL)) {
  }
}

// JobC: 40 ms compute, soft_deadline=15ms, priority=1
static void vJobC_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(40UL)) {
  }
}

// --- SUBMITTER TASK ---
// Runs at higher priority than the Polling Server so all three jobs are
// in the queue before the server's first activation.
static void vSubmitterTask(void *pvArgs) {
  (void)pvArgs;
  xAperiodicSubmitJob(vJobA_Body, NULL, "JobA", pdMS_TO_TICKS(30UL), 10U);
  xAperiodicSubmitJob(vJobB_Body, NULL, "JobB", pdMS_TO_TICKS(30UL), 5U);
  xAperiodicSubmitJob(vJobC_Body, NULL, "JobC", pdMS_TO_TICKS(15UL), 1U);
  vTaskSuspend(NULL);
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs) {
  (void)pvArgs;
  vTaskDelay(pdMS_TO_TICKS(3000UL));
  vUartPrintf("\n--- TEST 12 TRACE DUMP ---\n");
  vTraceDump();
  vUartPrintf("--- END ---\n");
  
  /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
  PTLTaskStats_t stats[1];
  UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
  
  uint32_t aperiodic_overruns = ulTraceGetEventCount(eEvtAperiodicOverrun);
  uint32_t aperiodic_kills = ulTraceGetEventCount(eEvtAperiodicKill);
  uint32_t server_exhausts = ulTraceGetEventCount(eEvtServerExhaust);
  uint32_t total_completions = ulTraceGetEventCount(eEvtComplete);
  
  /* Test 12: Budget exhaustion (100ms capacity).
     - JobA (40ms) triggers OVERRUN, finishes using 40ms budget.
     - JobB (40ms) triggers OVERRUN, finishes using 40ms budget (80ms total).
     - JobC (40ms) triggers OVERRUN, uses remaining 20ms, exhausts budget, and is KILLED.
     - Expected: 3 AperiodicOverruns, 1 AperiodicKill, 1 ServerExhaust.
     - Expected Completions: exactly 2 (JobA, JobB). 
     - Server itself remains perfectly stable. */
  if (num_tasks > 0 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
      aperiodic_overruns == 3 && aperiodic_kills == 1 && server_exhausts == 1 && 
      total_completions == 2) {
    vUartPrintf("\n[VERDICT] PASS - Complex budget exhaustion signature verified.\n");
  } else {
    vUartPrintf("\n[VERDICT] FAIL - Mismatch in expected complex trace signature.\n");
  }
  
  vPtlStop();
  for (;;) {
  }
}

// --- POLLING SERVER CONFIGURATION ---
// soft_deadline < compute triggers OVERRUN; total load (120ms) > capacity
// (100ms) -> budget exhaustion on JobC
static PSConfig_t xTest12PSCfg = {.uxPriority = TEST12_BASE_PRIO,
                                  .ulStackSize = 512U,
                                  .ulPeriodMs = 500U,
                                  .ulCapacityMs = 100U,
                                  .xPolicy = PS_POLICY_OVERRUN};

// --- PTL TASK CONFIGURATION ---
static PTLTaskConfig_t xTest12Tasks[1] = {
    // pcName     pvEntry              Arg   Stack   Priority          Period
    // Deadline  Phase  Type                 Policy
    {"PollSrv", vPollingServer_Task, NULL, 512U, TEST12_BASE_PRIO, 500U, 0U, 0U,
     eTracePollingServer, ePolicySkip}};

static PTLSchedulerConfig_t xTest12Cfg = {
    .xPolicy = ePolicySkip, .xTraceEnabled = pdTRUE, .ucNumTasks = 1U};

// --- ENTRY POINT ---
int main(void) {
  vUartInit();
  vUartPrintf("\n=== TEST 12: Polling Server - budget exhaustion === \n");

  vAperiodicInit();

  if (xPollingServer_Init(&xTest12PSCfg) != pdPASS) {
    vUartPrintf("ERROR: xPollingServer_init failed \n");
    for (;;) {
    }
  }

  xTest12Cfg.xTasks[0] = xTest12Tasks[0];

  if (xPtlInit(&xTest12Cfg) != pdPASS) {
    vUartPrintf("ERROR: xPtlInit failed \n");
    for (;;) {
    }
  }
  // Submitter at BASE_PRIO+1 to pre-load the queue before the PS activates
  xTaskCreate(vSubmitterTask, "Submitter", 256U, NULL, TEST12_BASE_PRIO + 1U,
              NULL);
  xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST12_BASE_PRIO + 2U,
              NULL);
  vPtlStart();
  vTaskStartScheduler();
  for (;;) {
  }
}