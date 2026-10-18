// Maximum tasks stress test (PTL_MAX_TASKS = 10)
/*
    -> The system is configured with exactly PTL_MAX_TASKS (10) periodic tasks.
       This tests the boundary limits of the PTL scheduler arrays and loops to
       ensure no off-by-one errors or memory corruption occurs when fully loaded.

    -> To demonstrate the superiority of the Hyperbolic Bound over the Liu & Layland
       bound, we configure a task set that FAILS the L&L bound but strictly PASSES
       the Hyperbolic Bound.
       L&L for n=10: U <= 71.77%
       Hyperbolic: Product(Ui + 1) <= 2.0

    -> Configuration:
       Task 0: C=40, T=100 -> U = 0.40
       Task 1-9: C=4, T=100 -> U = 0.04 (each)
       Total CPU utilization = 0.40 + 9 * 0.04 = 0.76 (76.0%) > 71.77% (Fails L&L)
       Hyperbolic Product = 1.40 * (1.04)^9 = 1.9926 <= 2.0 (Passes HB)

    -> Priorities are staggered from BASE to BASE+9 to simulate RM tie-breaking.
       The worst-case response time for the lowest priority task will be 
       40 + 9*4 = 76ms, well within the 100ms deadline.
*/

#include "FreeRTOS.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "task.h"
#include "uart.h"

/* BASE=1 so that Task9 (1+9=10) stays below the Timer Daemon (priority 11).
 * This ensures timer callbacks always preempt application tasks. */
#define TEST20_BASE_PRIO (tskIDLE_PRIORITY + 1U)

/* --- JOB BODIES --- */
static void vHeavyTask_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  // 40ms compute time
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(40UL)) {
  }
}

static void vLightTask_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  // 4ms compute time
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(4UL)) {
  }
}

/* --- REPORTER --- */
static void vReporterTask(void *pvArgs) {
  (void)pvArgs;
  // 20 periods * 100ms = 2000ms
  vTaskDelay(pdMS_TO_TICKS(2000UL));
  vUartPrintf("\n--- TEST 20 TRACE DUMP ---\n");
  vTraceDump();
  vUartPrintf("--- END ---\n");

  /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
  PTLTaskStats_t stats[10];
  UBaseType_t num_tasks = uxPtlGetStats(stats, 10);

  uint8_t all_passed = 1;
  uint8_t i;

  for (i = 0; i < 10; i++) {
    // Duration is 2000ms. Period is 100ms. Expected Completions: 20.
    if (stats[i].ulCompletedJobs != 20 || stats[i].ulDeadlineMisses != 0 ||
        stats[i].ulOverruns != 0) {
      all_passed = 0;
    }
  }

  if (num_tasks == 10 && all_passed) {
    vUartPrintf("\n[VERDICT] PASS - Max tasks (10) scheduled successfully with "
                "76%% RM Load (Fails L&L, Passes HB).\n");
  } else {
    vUartPrintf("\n[VERDICT] FAIL - Expected 10 tasks, 20 completions each, and 0 "
                "errors.\n");
  }

  vPtlStop();
  for (;;) {
  }
}

/* --- CONFIGURATION --- */
static PTLTaskConfig_t xTest20Tasks[10] = {
    {"Task0", vHeavyTask_Body, NULL, 512U, TEST20_BASE_PRIO + 9U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task1", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 8U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task2", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 7U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task3", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 6U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task4", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 5U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task5", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 4U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task6", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 3U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task7", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 2U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task8", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 1U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip},
    {"Task9", vLightTask_Body, NULL, 512U, TEST20_BASE_PRIO + 0U, 100U, 100U,
     0U, eTracePeriodic, ePolicySkip}};

static PTLSchedulerConfig_t xTest20Cfg = {
    .xPolicy = ePolicySkip, .xTraceEnabled = pdTRUE, .ucNumTasks = 10U};

/* --- ENTRY POINT --- */
int main(void) {
  uint8_t i;
  vUartInit();
  vUartPrintf("\n=== TEST 20: Maximum tasks stress test (Hyperbolic Bound) ===\n");

  for (i = 0U; i < 10U; i++) {
    xTest20Cfg.xTasks[i] = xTest20Tasks[i];
  }

  if (xPtlInit(&xTest20Cfg) != pdPASS) {
    vUartPrintf("ERROR: xPtlInit failed\n");
    for (;;) {
    }
  }

  /* Reporter priority must be higher than all tasks to preempt them */
  xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST20_BASE_PRIO + 10U,
              NULL);

  vPtlStart();
  vTaskStartScheduler();

  for (;;) {
  }
}
