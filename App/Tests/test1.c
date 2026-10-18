// Normal periodic execution 
/* 
    -> 2 tasks always complete within deadline
*/
#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST01_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES

// task A: 15 ms compute, T = 100 ms, D = 100 ms
static void TaskA_Body(void *pvArgs){
    (void) pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow ) < pdMS_TO_TICKS(15UL)){}
}

// task B: 25 ms compute, T = 150 ms, D = 150 ms
static void TaskB_Body(void *pvArgs){
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(25UL)){}
}

// --- REPORTER
static void vReporterTask(void *pvParams){
    (void)pvParams;
    vTaskDelay(pdMS_TO_TICKS(1500UL));
    vUartPrintf("\n--- TEST 01 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[2];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 2);
    uint32_t total_misses = 0;
    uint32_t total_overruns = 0;
    
    for(UBaseType_t i=0; i<num_tasks; i++) {
        total_misses += stats[i].ulDeadlineMisses;
        total_overruns += stats[i].ulOverruns;
    }
    
    /* Test 1 expects 0 deadline misses and 0 overruns */
    if(total_misses == 0 && total_overruns == 0) {
        vUartPrintf("\n[VERDICT] PASS - Normal execution verified.\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected 0 misses/overruns, found some.\n");
    }
    
    vPtlStop();
    for(;;){}
}

// configuration
static PTLTaskConfig_t xTest01Taks[2] = {
    //pcName, pvEntry,    pvArg, Stack, Priority,         period,  deadline, Phase, Type,           Policy
    {"TaskA", TaskA_Body, NULL,  512U,  TEST01_BASE_PRIO, 100U,    100U,     0,     eTracePeriodic, ePolicySkip},
    {"TaskB", TaskB_Body, NULL,  512U,  TEST01_BASE_PRIO, 150U,    150U,     0,     eTracePeriodic, ePolicySkip}
};

static PTLSchedulerConfig_t xTest01Cfg = {
    .xPolicy = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks = 2U
};

// entry point
int main(void){

    uint8_t i;

    vUartInit();
    vUartPrintf("\n=== Test 01: Normal periodic execution ===\n");

    for(i = 0U; i < 2U; i++){
        xTest01Cfg.xTasks[i] = xTest01Taks[i];
    }

    if(xPtlInit(&xTest01Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed \n");
        for(;;){}
    }

    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST01_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;){}
}