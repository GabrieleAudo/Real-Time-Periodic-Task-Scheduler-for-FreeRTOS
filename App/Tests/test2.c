// Deadline miss scenario
/* 
    -> on 3rd activation, the task takes 120 ms which exceeds its D = 80 ms
    Expected trace: DeadlineMiss on iteraction 3, Complete on all others
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST02_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES ---
static void vSamplerJob(void *pvArgs){
    (void) pvArgs;
    static uint32_t ulCount = 0UL;
    TickType_t xNow = xTaskGetTickCount();
    TickType_t xDuration;

    ulCount++;
    // 3rd activation: 120 ms > D = 80 ms -> deadline miss 
    xDuration = (ulCount == 3UL) ? pdMS_TO_TICKS(120UL) : pdMS_TO_TICKS(20UL);

    while((xTaskGetTickCount()-xNow) < xDuration) {}

}

// --- REPORTER ---
static void vReporterTask(void *pvArgs){

    ( void ) pvArgs;
    vTaskDelay( pdMS_TO_TICKS( 2000UL ) );
    vUartPrintf( "\n--- TEST 02 TRACE DUMP ---\n" );
    vTraceDump();
    vUartPrintf( "--- END ---\n" );
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[1];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 1);
    
    if(num_tasks > 0 && stats[0].ulDeadlineMisses == 1 && stats[0].ulOverruns == 0) {
        vUartPrintf("\n[VERDICT] PASS - Deadline miss detected without overrun.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected 1 miss and 0 overruns.\n");
    }
    
    vPtlStop();
    for( ;; ) {}

}

// configuration
static PTLTaskConfig_t xTest02Tasks[1] = {
    //pcName,   pvEntry,     pvArg, Stack, Priority,         period,  deadline, Phase, Type,           Policy
    {"Sampler", vSamplerJob, NULL,  512U,  TEST02_BASE_PRIO, 200U,    80U,      0,     eTracePeriodic, ePolicySkip}
};

static PTLSchedulerConfig_t xTest02Cfg = {
    .xPolicy = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks = 1U
};

// entry point
int main(void){

    uint8_t i;

    vUartInit();
    vUartPrintf("\n=== Test 02: Deadline Miss ===\n");

    xTest02Cfg.xTasks[0] = xTest02Tasks[0];


    if(xPtlInit(&xTest02Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed \n");
        for(;;){}
    }

    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST02_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;){}
}