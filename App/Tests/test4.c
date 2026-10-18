// Period overrun wit KILL policy
/*
    -> on iteraction 3, "TaskA" overrunis. With Kill policy the
    running task is deleted and a fresh task istance is created
    for the new period
    Expected trace: Kill on iteration 3, then clean releases after
*/

#include "FreeRTOS.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "task.h"
#include "uart.h"

#define TEST04_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
static void TaskA_Body(void *pvArgs) {
  (void)pvArgs;
  static uint32_t ulCount = 0UL;
  TickType_t xNow = xTaskGetTickCount();
  TickType_t xDuration;

  ulCount++;
  xDuration = (ulCount == 3UL) ? pdMS_TO_TICKS(450UL) : pdMS_TO_TICKS(10UL);
  while ((xTaskGetTickCount() - xNow) < xDuration) {
  }
}

// REPORTER
static void vReporterTask(void *pvArgs) {
  (void)pvArgs;
  vTaskDelay(pdMS_TO_TICKS(3000UL));
  vUartPrintf("\n--- TEST 04 TRACE DUMP ---\n");
  vTraceDump();
  vUartPrintf("--- END ---\n");

  /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
  PTLTaskStats_t stats[1];
  UBaseType_t num_tasks = uxPtlGetStats(stats, 1);

  /* In KILL policy, 15 total periods elapse in 3000ms.
     The task is deleted upon overrun before it can complete and
     register a deadline miss. We expect exactly 1 overrun and 0 deadline misses.
     Because 1 job was killed out of 15, we expect exactly 14 completed jobs. */
  if (num_tasks > 0 && stats[0].ulDeadlineMisses == 0 &&
      stats[0].ulOverruns == 1 && stats[0].ulCompletedJobs == 14) {
    vUartPrintf("\n[VERDICT] PASS - Period overrun and KILL policy signature verified (14 completed).\n");
  } else {
    vUartPrintf("\n[VERDICT] FAIL - Expected 0 misses, 1 overrun, and 14 completed jobs.\n");
  }

  vPtlStop();
  for (;;) {
  }
}

// --- configuration ---

static PTLTaskConfig_t xTest04Tasks[1] = {
    /* pcName   pvEntry      Arg    Stack    Prio              Period Deadline
       Phase    Type             Policy      */
    {"TaskA", TaskA_Body, NULL, 512U, TEST04_BASE_PRIO, 200U, 0U, 0U,
     eTracePeriodic, ePolicyKill}};

static PTLSchedulerConfig_t xTest04Cfg = {
    .xPolicy = ePolicyKill, .xTraceEnabled = pdTRUE, .ucNumTasks = 1U};

// --- entry point ---
int main(void) {
  vUartInit();
  vUartPrintf("\n=== TEST 04: Overrun - KILL policy ===\n");

  xTest04Cfg.xTasks[0] = xTest04Tasks[0];

  if (xPtlInit(&xTest04Cfg) != pdPASS) {
    vUartPrintf("ERROR: xPtlInit failed\n");
    for (;;) {
    }
  }

  xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST04_BASE_PRIO + 1U,
              NULL);

  vPtlStart();
  vTaskStartScheduler();
  for (;;) {
  }
}