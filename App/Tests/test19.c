// Round-robin scheduling at equal priority
/*
    -> Three tasks with the exact same period, deadline, compute time, and
   priority. Because configUSE_TIME_SLICING is enabled in FreeRTOS by default,
   tasks sharing the same priority will be time-sliced in a Round-Robin fashion
       at every tick interrupt (1ms).

    TaskA: compute=30ms (wall-clock), T=150ms, priority=BASE
    TaskB: compute=30ms (wall-clock), T=150ms, priority=BASE
    TaskC: compute=30ms (wall-clock), T=150ms, priority=BASE

    HOW THIS TEST PROVES ROUND-ROBIN:
    The busy-wait loop uses xTaskGetTickCount() (global wall-clock), NOT CPU
   cycles. This means the 30ms "compute" counts elapsed wall-clock time, which
   advances even when a task is preempted and not running on the CPU.

    SCENARIO A - NO Round-Robin (sequential, FIFO):
      TaskA starts at t=0, runs alone for 30ms, finishes at t=30.  WCRT_A = 30.
      TaskB starts at t=30, runs alone for 30ms, finishes at t=60. WCRT_B = 60.
      TaskC starts at t=60, runs alone for 30ms, finishes at t=90. WCRT_C = 90.
      -> TaskB and TaskC would have WCRT >> 35ms.

    SCENARIO B - Round-Robin IS active (our expectation):
      All three tasks are time-sliced 1ms each in rotation.
      The global clock advances for all of them simultaneously.
      TaskA sees 30ms elapsed around t=30, finishes at ~t=30.   WCRT_A = 30.
      TaskB sees 30ms elapsed around t=31, finishes at ~t=31.   WCRT_B = 32.
      TaskC sees 30ms elapsed around t=32, finishes at ~t=32.   WCRT_C = 32.
      -> ALL three tasks have WCRT <= 35ms.

    VERIFICATION: We assert WCRT <= 35ms for ALL three tasks.
    If any task has WCRT > 35ms, it means it was blocked waiting for another
   task to finish first (sequential execution), which would disprove
   Round-Robin.

    NOTE: On real hardware, a more rigorous test would use DWT CYCCNT (CPU cycle
    counter) to measure actual CPU time consumed. QEMU does not faithfully
   emulate DWT, so we use the wall-clock approach which is still a valid
   indirect proof.
*/

#include "FreeRTOS.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "task.h"
#include "uart.h"

#define TEST19_BASE_PRIO (tskIDLE_PRIORITY + 2U)

/* --- JOB BODIES --- */
static void vTask_Body(void *pvArgs) {
  (void)pvArgs;
  TickType_t xNow = xTaskGetTickCount();
  while ((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(30UL)) {
  }
}

/* --- REPORTER --- */
static void vReporterTask(void *pvArgs) {
  (void)pvArgs;
  vTaskDelay(pdMS_TO_TICKS(3000UL));
  vUartPrintf("\n--- TEST 19 TRACE DUMP ---\n");
  vTraceDump();
  vUartPrintf("--- END ---\n");

  /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
  PTLTaskStats_t stats[3];
  UBaseType_t num_tasks = uxPtlGetStats(stats, 3);

  /* Test 19: Round-Robin at equal priority
     - 3000ms duration. Period = 150ms.
     - Expected Completions: 20 per task.
     - Expected WCRT: <= 35ms for ALL THREE tasks (proving time-slicing).

     EXPLANATION OF THE PROOF:
     If FreeRTOS did NOT use time-slicing (sequential execution), TaskA would
     start at t=0 and finish at t=30. TaskB would start at t=30 (xNow=30) and
     finish at t=60. (WCRT = 60). TaskC would start at t=60 (xNow=60) and finish
     at t=90. (WCRT = 90).

     Because Round-Robin time-slicing IS active, all three tasks get 1ms of CPU
     each and progress concurrently. Because they all check the GLOBAL clock
     `xTaskGetTickCount()`, they all hit the 30ms wall-clock limit
     simultaneously! Thus, they all finish around t=32, having consumed ~10ms of
     CPU each. The fact that TaskB and TaskC have a WCRT of ~32ms instead of
     60ms and 90ms is the absolute mathematical proof that they ran concurrently
     via Round-Robin!

     - Expected Errors: 0 misses, 0 overruns. */

  uint8_t all_round_robin_proven = 1;
  // They should all finish around 30-33ms. If WCRT > 35 for any of them, they
  // were not time-sliced.
  if (stats[0].ulWorstCaseResponse > 35 || stats[1].ulWorstCaseResponse > 35 ||
      stats[2].ulWorstCaseResponse > 35) {
    all_round_robin_proven = 0;
  }

  if (num_tasks == 3 && stats[0].ulCompletedJobs == 20 &&
      stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
      stats[1].ulCompletedJobs == 20 && stats[1].ulDeadlineMisses == 0 &&
      stats[1].ulOverruns == 0 && stats[2].ulCompletedJobs == 20 &&
      stats[2].ulDeadlineMisses == 0 && stats[2].ulOverruns == 0 &&
      all_round_robin_proven) {
    vUartPrintf("\n[VERDICT] PASS - Round-Robin time-slicing mathematically "
                "verified (WCRT <= 35ms for all).\n");
  } else {
    vUartPrintf("\n[VERDICT] FAIL - Expected exactly 20 completions, 0 errors, "
                "and WCRT <= 35ms.\n");
  }

  vPtlStop();
  for (;;) {
  }
}

/* --- CONFIGURATION --- */
static PTLTaskConfig_t xTest19Tasks[3] = {
    /* pcName   pvEntry       Arg   Stack   Priority          Period  Deadline
       Phase  Type             Policy      */
    {"TaskA", vTask_Body, NULL, 512U, TEST19_BASE_PRIO, 150U, 150U, 0U,
     eTracePeriodic, ePolicySkip},
    {"TaskB", vTask_Body, NULL, 512U, TEST19_BASE_PRIO, 150U, 150U, 0U,
     eTracePeriodic, ePolicySkip},
    {"TaskC", vTask_Body, NULL, 512U, TEST19_BASE_PRIO, 150U, 150U, 0U,
     eTracePeriodic, ePolicySkip}};

static PTLSchedulerConfig_t xTest19Cfg = {
    .xPolicy = ePolicySkip, .xTraceEnabled = pdTRUE, .ucNumTasks = 3U};

/* --- ENTRY POINT --- */
int main(void) {
  uint8_t i;
  vUartInit();
  vUartPrintf("\n=== TEST 19: Round-Robin at equal priority ===\n");

  for (i = 0U; i < 3U; i++) {
    xTest19Cfg.xTasks[i] = xTest19Tasks[i];
  }

  if (xPtlInit(&xTest19Cfg) != pdPASS) {
    vUartPrintf("ERROR: xPtlInit failed\n");
    for (;;) {
    }
  }

  xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST19_BASE_PRIO + 1U,
              NULL);

  vPtlStart();
  vTaskStartScheduler();

  for (;;) {
  }
}
