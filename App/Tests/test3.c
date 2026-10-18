// Period overrun with SKIP policy
/*
    -> on iteration 3, "TaskA" takes 450ms >> T=200ms
    with ePolicySkip the timer callback returns early, so the new
    job is dropped and the overrunning job finishes undisturbed.
    Expected trace: OverrunSkip on iteration 3, normal releases before and after
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST03_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
static void TaskA_Body(void * pvArgs){
    (void)pvArgs;
    static uint32_t ulCount = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;

    ulCount++;
    xDuration = (ulCount == 3UL) ? pdMS_TO_TICKS(450UL) : pdMS_TO_TICKS(10UL);

    while((xTaskGetTickCount() - xNow) < xDuration) {}
}

// --- REPORTER ---
static void vReporterTask(void * pvParams){

    (void) pvParams;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 03 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    /* In SKIP policy, 15 total periods elapse in 3000ms. 
       The 450ms job crosses two period boundaries (600ms and 800ms), causing 2 overruns and 2 skipped jobs.
       It eventually finishes, recording 1 deadline miss.
       15 total periods - 2 skipped = 13 completed jobs. */
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 1 && stats[0].ulOverruns == 2 && stats[0].ulCompletedJobs == 13) {
        vUartPrintf("\n[VERDICT] PASS - Period overrun and SKIP policy signature verified (13 completed).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected 1 miss, 2 overruns, and 13 completed jobs.\n");
    }
    
    vPtlStop();
    for(;;){}

}

// configuration
static PTLTaskConfig_t xTest03Tasks[ 1 ] =
{
    /* pcName   pvEntry      Arg  Stack      Priority          Period    Deadline    Phase    Type             Policy      */
    { "TaskA",  TaskA_Body,  NULL,  512U,    TEST03_BASE_PRIO, 200U,     0U,         0U,      eTracePeriodic,  ePolicySkip  }
};

static PTLSchedulerConfig_t xTest03Cfg =
{
    .xPolicy       = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 1U
};

// entry point
int main(void){

    vUartInit();
    vUartPrintf("\n=== TEST 03: Overrun - SKIP policy ===\n");

    xTest03Cfg.xTasks[0] = xTest03Tasks[0];

    if(xPtlInit(&xTest03Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed\n");
        for(;;){}
    }

    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST03_BASE_PRIO + 1U, NULL);

    vPtlStart();
    vTaskStartScheduler();
    for(;;){}
}

