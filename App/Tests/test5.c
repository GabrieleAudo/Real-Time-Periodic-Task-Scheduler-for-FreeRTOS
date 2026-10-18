// Description: Overrun with CATCH-UP policy 
/*
    -> on iteration 3, "Task" takes 250 ms (overrun T = 200ms)
    with Catch-up the timer fires again, marks the previous job missed
    and notifies the task to start the next job as soon as the current one finishes
    Expected trace: CatchUp on iteration 3
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST05_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
static void vTask_Body(void *pvArgs){
    (void) pvArgs;
    static uint32_t ulCount = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;

    ulCount++;
    xDuration = (ulCount == 3UL) ? pdMS_TO_TICKS(250UL) : pdMS_TO_TICKS(10UL);

    while((xTaskGetTickCount() - xNow) < xDuration) {}
} 

// --- REPORTER ---
static void vReporter(void * pvArgs){

    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(3000UL));
    vUartPrintf("\n--- TEST 05 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END --- \n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    /* In CATCH-UP policy, 15 total periods elapse in 3000ms.
       The task continues running and misses its deadline, triggering 1 overrun and 1 miss.
       Since the next job is immediately queued and no jobs are ever dropped or killed,
       we expect exactly 15 completed jobs. */
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 1 && stats[0].ulOverruns == 1 && stats[0].ulCompletedJobs == 15) {
        vUartPrintf("\n[VERDICT] PASS - Period overrun and CATCH-UP policy signature verified (15 completed).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected exactly 1 miss, 1 overrun, and 15 completed jobs.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// configuration 
static PTLTaskConfig_t xTest05Tasks[ 1 ] =
{
    /* pcName  pvEntry      Arg    Stack    Prio              Period    Deadline    Phase    Type             Policy          */
    { "Task",  vTask_Body,  NULL,  512U,    TEST05_BASE_PRIO, 200U,     0U,         0U,      eTracePeriodic,  ePolicyCatchUp   }
};

static PTLSchedulerConfig_t xTest05Cfg =
{
    .xPolicy       = ePolicyCatchUp,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks    = 1U
};

/* --- entry point --- */

int main( void )
{
    vUartInit();
    vUartPrintf( "\n=== Test 05: Overrun - CATCH-UP Policy ===\n" );

    xTest05Cfg.xTasks[ 0 ] = xTest05Tasks[ 0 ];

    if( xPtlInit( &xTest05Cfg ) != pdPASS )
    {
        vUartPrintf( "ERROR: xPtlInit failed\n" );
        for( ;; ) {}
    }

    xTaskCreate( vReporter, "Reporter", 512U, NULL, TEST05_BASE_PRIO + 1U, NULL );

    vPtlStart();
    vTaskStartScheduler();
    for( ;; ) {}
}
