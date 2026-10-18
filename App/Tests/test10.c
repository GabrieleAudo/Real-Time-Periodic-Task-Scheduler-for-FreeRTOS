// periodic tasks with phase offsets
/*
    -> TaskA: compute 15ms, T=200ms, D=200ms, phase=0ms (starts at t0)
    TaskB: compute 15ms, T=200ms, D=200ms, phase=100ms (starts at 100ms)
    Both tasks have the same period but different phases (the activation never overlap)
    Exepcted trace: 
        Release (TaskA) at t0  --- complete(TaskA) at t0+15ms
        Release (TaskB) at t0 + 100ms --- complete(TaskB) at t0+115ms
        Release (TaskA) at t0 + 200ms --- complete(TaskA) at t0+215ms
        Release (TaskB) at t0 + 300ms --- complete(TaskB) at t0+315ms
*/

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"
#include "uart.h"

#define TEST10_BASE_PRIO (tskIDLE_PRIORITY + 2U)

// --- JOB BODIES --- 

// taskA: 15 ms compute, T=200ms, D=200ms, phase = 0ms
static void vTaskA_Body(void * pvArgs){
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(15UL)) {}
}

// taskB: 15ms compute, T=200ms, D=200ms, phase=100ms
static void vTaskB_Body(void *pvArgs){
    (void)pvArgs;
    TickType_t xNow = xTaskGetTickCount();
    while((xTaskGetTickCount() - xNow) < pdMS_TO_TICKS(15UL)) {}
}

// --- REPORTER ---
static void vReporterTask(void *pvArgs){
    (void)pvArgs;
    vTaskDelay(pdMS_TO_TICKS(2000UL));
    vUartPrintf("\n--- TEST 10 TRACE DUMP ---\n");
    vTraceDump();
    vUartPrintf("--- END ---\n");
    
    /* --- AUTOMATIC PASS/FAIL EVALUATION --- */
    PTLTaskStats_t stats[2];
    UBaseType_t num_tasks = uxPtlGetStats(stats, 2);
    
    /* Test 10: Phase offsets. 2000ms execution.
       - TaskA (T=200ms, Phase=0) completes 10 times.
       - TaskB (T=200ms, Phase=100) completes 10 times.
       - 0 misses and 0 overruns. */
    if(num_tasks == 2 && 
       stats[0].ulCompletedJobs == 10 && stats[0].ulDeadlineMisses == 0 && stats[0].ulOverruns == 0 &&
       stats[1].ulCompletedJobs == 10 && stats[1].ulDeadlineMisses == 0 && stats[1].ulOverruns == 0) 
    {
        vUartPrintf("\n[VERDICT] PASS - Phase offsets verified (10 completions each).\n");
    } else {
        vUartPrintf("\n[VERDICT] FAIL - Expected 10 completions each and 0 misses.\n");
    }
    
    vPtlStop();
    for(;;) {}
}

// --- CONFIGURATION ---
static PTLTaskConfig_t xTest10Tasks[2] = {
    /* Name     Entry         Arg   Stack   Priority          Period  Deadline  Phase   Type             Policy      */
    { "TaskA",  vTaskA_Body,  NULL, 512U,   TEST10_BASE_PRIO, 200U,   200U,     0U,     eTracePeriodic,  ePolicySkip },
    { "TaskB",  vTaskB_Body,  NULL, 512U,   TEST10_BASE_PRIO, 200U,   200U,     100U,   eTracePeriodic,  ePolicySkip }
};

static PTLSchedulerConfig_t xTest10Cfg = {
    .xPolicy = ePolicySkip,
    .xTraceEnabled = pdTRUE,
    .ucNumTasks = 2U
};

// --- ENTRY POINT --- 
int main(void){
    uint8_t i;

    vUartInit();
    vUartPrintf("\n=== TEST 10: Periodic tasks with phase offsets ===\n");

    for(i=0U; i<2U; i++){
        xTest10Cfg.xTasks[i] = xTest10Tasks[i];
    }

    if(xPtlInit(&xTest10Cfg) != pdPASS){
        vUartPrintf("ERROR: xPtlInit failed \n");
        for(;;) {}
    }

    xTaskCreate(vReporterTask, "Reporter", 512U, NULL, TEST10_BASE_PRIO + 1U, NULL);
    vPtlStart();
    vTaskStartScheduler();
    for(;;) {}
}

