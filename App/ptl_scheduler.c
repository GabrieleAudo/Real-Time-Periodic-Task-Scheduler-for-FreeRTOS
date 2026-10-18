/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "ptl_scheduler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "uart.h"
#include <stdio.h>
#include <string.h>

/* Internal task control block */
#if ( configUSE_PTL_SCHEDULER == 1 )

typedef struct
{
    TaskHandle_t xHandle;            /* FreeRTOS task handle */
    TimerHandle_t xReleaseTimer;     /* Software timer for periodic release */
    const PTLTaskConfig_t *pxConfig; /* Pointer to configuration */
    TickType_t xReleaseTime;         /* Absolute release time */
    TickType_t xAbsoluteDeadline;    /* Absolute deadline */
    uint32_t ulJobNumber;            /* Current job iteration of the task */
    PTLTaskState_t xState;           /* Current task state */
    uint32_t xCatchUpPending;      /* Number of catch-up job pending */

    /* Statistics */
    uint32_t ulTotalJobs;
    uint32_t ulCompletedJobs;
    uint32_t ulDeadlineMisses;
    uint32_t ulOverruns;
    uint32_t ulTotalResponseTime;
    uint32_t ulWorstCaseResponse;
} PTLTaskControlBlock_t;
// NB: PTLTaskControlBlock_t is the runtime control block for each task, while PTLTaskConfig_t (from ptl_config.h) is the static configuration

/* Global PTL state */
static struct
{
    const PTLSchedulerConfig_t *pxConfig; // pointer to the struct in the ptl_config.h, pointer because avoids copying the config structure
    PTLTaskControlBlock_t xTasks[PTL_MAX_TASKS];
    TickType_t xTimeZero;    // it stores the time 0 - the synchronized start time for all tasks
    BaseType_t xInitialized; // flag indicating whether PTL_init() has been called. It prevents PTL_Start() before PTL_init()
    BaseType_t xStarted;     // flag indicating whether PTL_Start() has been called
    // initialized = tasks and timers created but not started
    // started = timers actually running

} xGlobalPTLState;

/* Forward declarations */
static void prvJobComplete(PTLTaskControlBlock_t *pxTcb);
static void prvPeriodicTaskWrapper(void *pvParameters);
static void prvReleaseTimerCallback(TimerHandle_t xTimer);
static void prvCleanupInit(uint8_t ucNumCreated);
UBaseType_t uxPtlGetStats(PTLTaskStats_t *pxStatsOut, UBaseType_t uxMaxTasks);

/**
 * @brief Task wrapper that calls user job body in a loop
 */
static void prvPeriodicTaskWrapper(void *pvParameters)
{
    /* Retrieve task's TCB from paramteters */
    PTLTaskControlBlock_t *pxTcb = (PTLTaskControlBlock_t *)pvParameters;

    for (;;)
    {
        /* Task waits for the timer to notify it*/
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY); // pdFALSE decreases notification count by 1 after take
        /* Job released, execute user function */
        pxTcb->xState = eStateTaskRunning;                                        // mark the task as running: if timer fires and state is still running, it's an overrun
        pxTcb->pxConfig->pvEntryFunction(pxTcb->pxConfig->pvEntryFunctionArgument); // entry is the function pointer and arg is the user's param
        /* Job complete, check if deadline was missed and update statistics */
        prvJobComplete(pxTcb);
    }
}

/**
 * @brief Release timer callback - handles periodic releases. Called by FreeRTOS every period to release a periodic task
    goal: detect overruns, apply overrun policies, update release time and deadline, resume the task
*/
static void prvReleaseTimerCallback(TimerHandle_t xTimer) // xTimer is the timer that fired
{
    /* SECTION 1 : Get task's TCB */

    /* Get task's TCB pointer directly from the Timer ID */
    PTLTaskControlBlock_t *pxTcb = (PTLTaskControlBlock_t *)pvTimerGetTimerID(xTimer);
    uint8_t ucSkipUpdate = 0; // Tells the callback to skip the update of release and deadline fields if catch-up policy is executed, while still executing the rest of the callback

    /* Safety check*/
    if (pxTcb == NULL)
    {
        return;
    }

    /*-------------------------------------------------------------------------------------------------------------*/
    /* SECTION 2 (only first release):  If this is the first release after phase, reset timer to normal period */

    /*
        Why this code ? First release: timer period set to ulPhaseMs
                        After first release: change timer period to normal ulPeriodMs
    */
    if (pxTcb->ulJobNumber == 0 && pxTcb->pxConfig->ulPhaseMs > 0)
    {
        xTimerChangePeriod(xTimer, pdMS_TO_TICKS(pxTcb->pxConfig->ulPeriodMs), 0);
    }

    /*-------------------------------------------------------------------------------------------------------------*/
    /* SECTION 3: Overrun detection and handling */

    /* Check for overrun - previous job still running */
    if (pxTcb->xState == eStateTaskRunning)
    {
        /* Increment overrun counter */
        pxTcb->ulOverruns++;

        /* Get overrun policy */
        OverrunPolicy_t xPolicy;

        if (pxTcb->pxConfig->xPolicy >= 0)
        {
            xPolicy = pxTcb->pxConfig->xPolicy; // if defined, use task policy
        }
        else
        {
            xPolicy = xGlobalPTLState.pxConfig->xPolicy; // else, use global default policy
        }

        switch (xPolicy)
        {
        case ePolicySkip:
        {
            /* Skip this release, let current job finish and ignore the new release */
            if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
            {
                vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtOverrunSkip, pxTcb->pxConfig->ulPeriodMs);
            }
            return; // return immediately, so new job is not released
        }
        case ePolicyKill:
        {
            /* Kill current job and start new one */
            TaskHandle_t xOldHandle; // save the old task handle (need it for deletion)
            BaseType_t xCreateResult;
            if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
            {
                vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtOverrunKill, pxTcb->pxConfig->ulPeriodMs);
            }

            /* Delete old task */
            xOldHandle = pxTcb->xHandle;
            pxTcb->xHandle = NULL;
            pxTcb->xState = eStateTaskIdle;
            vTaskDelete(xOldHandle);

            /* Create new task */
            xCreateResult = xTaskCreate(prvPeriodicTaskWrapper, pxTcb->pxConfig->pcName,
                                        pxTcb->pxConfig->ulStackSize, (void *)pxTcb,
                                        pxTcb->pxConfig->uxPriority, &pxTcb->xHandle);

            /* If task recreation fails -> mark as failed state */
            if (xCreateResult != pdPASS)
            {
                pxTcb->xState = eStateTaskIdle;
                pxTcb->xHandle = NULL;
                return;
            }
            break;
        }

        case ePolicyCatchUp:
        {
            /* Release new job immediately, mark previous missed. Don't skip, don't kill but release a new job */
            if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
            {
                vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtOverrunCatchup, pxTcb->pxConfig->ulPeriodMs);
            }
            ucSkipUpdate = 1;
            pxTcb->xCatchUpPending++; /* Tell prvJobComplete that a new job is pending after the current one */
            break; // do not return!
        }
        }
    }
    /*-------------------------------------------------------------------------------------------------------------*/
    /* SECTION 4 : update release time, deadline, state and statistics */
    if (ucSkipUpdate == 0) // If catch-up policy was not executed, update these fields
    {
        /* Update release time and deadline for new job */
        pxTcb->xReleaseTime = xTaskGetTickCount();

        /* Update absolute deadline as absolute deadline = release time + relative deadline */
        if (pxTcb->pxConfig->ulDeadlineMs == 0)
        {
            /* If deadline is set to 0, use period instead */
            pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulPeriodMs);
        }
        else
        {
            pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulDeadlineMs);
        }

        /* Update state */
        pxTcb->xState = eStateTaskIdle; // task is not running yet
    }

    /* Update task statistics*/
    pxTcb->ulJobNumber++; // jobs number for this task
    pxTcb->ulTotalJobs++; // total jobs ever released

    /*-------------------------------------------------------------------------------------------------------------*/
    /* SECTION 5: log and resume */
    if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
    {
        vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtRelease, 0);
    }

    /* Send notification to task to execute a new job */
    /* If CATCH_UP is selected, new job will be pending */
    xTaskNotifyGive(pxTcb->xHandle);
}

/**
 * @brief Signal job completion, after a task finishes its job
 */
static void prvJobComplete(PTLTaskControlBlock_t *pxTcb)
{
    TickType_t xFinishTime;   // time when a job is finished
    TickType_t xResponseTime; // how long the job took to execute

    /* Safety check */
    if (pxTcb == NULL)
    {
        return;
    }

    /* Get finish time of task */
    xFinishTime = xTaskGetTickCount();

    /* Check deadline */
    if ((int32_t)(xFinishTime - pxTcb->xAbsoluteDeadline) > 0) // safe check to avoid unsigned integer overflow
    {
        /* Update deadline misses and log the deadline miss event */
        pxTcb->ulDeadlineMisses++;
        if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
        {
            vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtDeadlineMiss, pxTcb->xAbsoluteDeadline);
        }
    }

    /* Update statistics */
    pxTcb->ulCompletedJobs++;
    xResponseTime = (TickType_t)(xFinishTime - pxTcb->xReleaseTime);
    pxTcb->ulTotalResponseTime += xResponseTime;

    if (xResponseTime > pxTcb->ulWorstCaseResponse)
    {
        /* Update worst case response time (WCRT) */
        pxTcb->ulWorstCaseResponse = xResponseTime;
    }

    /* Update task state and log job completion*/
    if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
    {
        if (pxTcb->pxConfig->xType == eTracePeriodic)
        {
            vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtComplete, xResponseTime);
        }
        else if (pxTcb->pxConfig->xType == eTracePollingServer)
        {
            /* If polling server has finished working, its budget gets refilled */
            vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtServerRefill, xResponseTime);
        }
    }
    /* If a catch-up job is pending, update these fields now because new job will be released immediately without waiting for the next timer callback */
    if (pxTcb->xCatchUpPending > 0) 
    {
        /* Decrease number of pending jobs */
        pxTcb->xCatchUpPending--;

        /* Update release time for new job */
        pxTcb->xReleaseTime = xTaskGetTickCount();

        /* Update absolute deadline as absolute deadline = release time + relative deadline */
        if (pxTcb->pxConfig->ulDeadlineMs == 0)
        {
            /* If deadline is set to 0, use period instead */
            pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulPeriodMs);
        }
        else
        {
            pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulDeadlineMs);
        }
    }

    pxTcb->xState = eStateTaskComplete;
}

/*---------------------------------------------------------------------------------------------------*/
/*PTL CONFIGURATION HANDLING FUNCTIONS*/

/**
 * @brief Cleanup resources during initialization failure
 */
static void prvCleanupInit(uint8_t ucNumCreated)
{
    uint8_t ucIdx;

    for (ucIdx = 0; ucIdx < ucNumCreated; ucIdx++) // num of tasks successfully created
    {
        if (xGlobalPTLState.xTasks[ucIdx].xReleaseTimer != NULL) // check if timer exists (not NULL)
        {
            xTimerDelete(xGlobalPTLState.xTasks[ucIdx].xReleaseTimer, 0); // remove the timer, 0 = no wait
            xGlobalPTLState.xTasks[ucIdx].xReleaseTimer = NULL;           // set to null (marks as deleted)
        }

        if (xGlobalPTLState.xTasks[ucIdx].xHandle != NULL) // delete task
        {
            vTaskDelete(xGlobalPTLState.xTasks[ucIdx].xHandle);
            xGlobalPTLState.xTasks[ucIdx].xHandle = NULL; // set to null after the deletion
        }
    }

    memset(&xGlobalPTLState, 0, sizeof(xGlobalPTLState)); // zero out the entire gptl structure, resets all fields to 0/NULL
}

/**
 * @brief Initialize the Periodic Task Layer. It initializes all the tasks and timers but doesn't start them yet
 */
BaseType_t xPtlInit(const PTLSchedulerConfig_t *pxConfig) // pointer to user-provided configuration structure containing policy, enable flag, # of tasks, array of tasks
{
    uint8_t ucIdx;
    BaseType_t xResult;

    /* Safety check */
    if (pxConfig == NULL || pxConfig->ucNumTasks == 0 || pxConfig->ucNumTasks > PTL_MAX_TASKS)
    {
        return pdFAIL;
    }

    /* Initialize global state */
    memset(&xGlobalPTLState, 0, sizeof(xGlobalPTLState)); // set every byte in xGlobalPTLState to zero, &xGlobalPTLState address of structure, 0 = value to set
    xGlobalPTLState.pxConfig = pxConfig;                    // save pointer to user's configuration

    /* Create all tasks (all tasks are created suspended since they have to wait a notification) */
    for (ucIdx = 0; ucIdx < pxConfig->ucNumTasks; ucIdx++) // one for each configured task
    {
        PTLTaskControlBlock_t *pxTcb = &xGlobalPTLState.xTasks[ucIdx]; // save pxTcb in tasks array for the logger
        const PTLTaskConfig_t *pxTaskCfg = &pxConfig->xTasks[ucIdx];   // pointer to static configuration

        /* Safety check on period, since if a timer is initialized with period 0, FreeRTOS triggers an error */
        if (pxTaskCfg->ulPeriodMs == 0)
        {
            prvCleanupInit(ucIdx);
            return pdFAIL;
        }

        pxTcb->pxConfig = pxTaskCfg;    // link to static configuration
        pxTcb->xState = eStateTaskIdle; // initial state
        pxTcb->ulJobNumber = 0;         // no job released yet
        pxTcb->xCatchUpPending = 0;     // initialize to 0

        /* Create task */
        xResult = xTaskCreate(prvPeriodicTaskWrapper, pxTaskCfg->pcName,
                             pxTaskCfg->ulStackSize, (void *)pxTcb,
                             pxTaskCfg->uxPriority, &pxTcb->xHandle);

        /* Handle task creation failure */
        if (xResult != pdPASS)
        {
            prvCleanupInit(ucIdx); // delete tasks 0 to ucIdx-1
            return pdFAIL;
        }

        /* Create software timer for periodic release */
        pxTcb->xReleaseTimer = xTimerCreate(pxTaskCfg->pcName,
                                          pdMS_TO_TICKS(pxTaskCfg->ulPeriodMs),
                                          pdTRUE,      /* Auto-reload */
                                          (void *)pxTcb, // pass entire TCB as parameter
                                          prvReleaseTimerCallback);

        /* Handle timer creation failure*/
        if (pxTcb->xReleaseTimer == NULL)
        {
            prvCleanupInit(ucIdx + 1); // ucIdx + 1 because the task is already created
            return pdFAIL;
        }
    }

    /* Set state of the global PTL object to initialized and return success */
    xGlobalPTLState.xInitialized = pdTRUE;
    return pdPASS;
}

/**
 * @brief Start the Periodic Task Layer
 */
void vPtlStart(void)
{
    uint8_t ucIdx;

    /* Safety check */
    if (!xGlobalPTLState.xInitialized || xGlobalPTLState.xStarted) // check if ptl_init() was called or p_ptl was already started
    {
        return;
    }

    /* Define t0 */
    xGlobalPTLState.xTimeZero = xTaskGetTickCount();

    /* Start all timers with phase offsets */
    for (ucIdx = 0; ucIdx < xGlobalPTLState.pxConfig->ucNumTasks; ucIdx++)
    {
        PTLTaskControlBlock_t *pxTcb = &xGlobalPTLState.xTasks[ucIdx];             // get task control block
        TickType_t xPhaseTicks = pdMS_TO_TICKS(pxTcb->pxConfig->ulPhaseMs);   // converts phase in ticks
        TickType_t xPeriodTicks = pdMS_TO_TICKS(pxTcb->pxConfig->ulPeriodMs); // converts period in ticks

        /* Tasks start must be handled differently based on phase*/
        if (xPhaseTicks > 0)
        {
            /* If phase > 0, we don't need to send a notification immediately, first we set the timer to xPhaseTicks and the timer callbalck will handle the rest*/
            /* Change period to phase for first activation (xTimerChangePeriod also starts timer immediately) */
            xTimerChangePeriod(pxTcb->xReleaseTimer, xPhaseTicks, 0); // timer to modify, new period, time to wait
        }
        else
        {
            /* But if phase = 0 we need to start the task immediately */
            /* Update release time, abs deadline, job count and total numer of jobs*/

            pxTcb->xReleaseTime = xGlobalPTLState.xTimeZero;

            /* Update absolute deadline as absolute deadline = release time + relative deadline */
            if (pxTcb->pxConfig->ulDeadlineMs == 0)
            {
                /* If deadline is set to 0, use period instead */
                pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulPeriodMs);
            }
            else
            {
                pxTcb->xAbsoluteDeadline = pxTcb->xReleaseTime + pdMS_TO_TICKS(pxTcb->pxConfig->ulDeadlineMs);
            }

            pxTcb->ulJobNumber++;
            pxTcb->ulTotalJobs++;

            /* Log task release */
            if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
            {
                vTraceLog(pxTcb->pxConfig->pcName, pxTcb->ulJobNumber, pxTcb->pxConfig->xType, eEvtRelease, 0);
            }

            /* Start timer */
            xTimerStart(pxTcb->xReleaseTimer, 0);

            /* Instantly start the task by sending a task notification */
            xTaskNotifyGive(pxTcb->xHandle);
        }
        /* After first callback, timer will need to be reset to normal period */
        /* This is handled in the release callback by resetting period */
    }

    /* Change task state */
    xGlobalPTLState.xStarted = pdTRUE;
}

/**
 * @brief Stop the PTL and cleanup
 */
void vPtlStop(void)
{
    uint8_t ucIdx;
    PTLTaskStats_t xTaskStats[xGlobalPTLState.pxConfig->ucNumTasks];

    /* Safety check */
    if (!xGlobalPTLState.xInitialized)
    {
        return;
    }

    /* Stop and delete all tasks and timers */
    for (ucIdx = 0; ucIdx < xGlobalPTLState.pxConfig->ucNumTasks; ucIdx++)
    {
        /* Stop and delete timer */
        if (xGlobalPTLState.xTasks[ucIdx].xReleaseTimer != NULL)
        {
            xTimerStop(xGlobalPTLState.xTasks[ucIdx].xReleaseTimer, portMAX_DELAY);
            xTimerDelete(xGlobalPTLState.xTasks[ucIdx].xReleaseTimer, portMAX_DELAY);
        }

        /* Delete task */
        if (xGlobalPTLState.xTasks[ucIdx].xHandle != NULL)
        {
            vTaskDelete(xGlobalPTLState.xTasks[ucIdx].xHandle);
        }
    }

    /* After stopping PTL, if tracing is enabled, get and print all tasks statistics */
    if (xGlobalPTLState.pxConfig->xTraceEnabled == pdTRUE)
    {
        UBaseType_t uxFilledStatistics;
        uxFilledStatistics = uxPtlGetStats(xTaskStats, PTL_MAX_TASKS);
        vTraceStatsDump(xTaskStats, uxFilledStatistics);
    }

    /* Reset global PTL object state */
    xGlobalPTLState.xStarted = pdFALSE;
    xGlobalPTLState.xInitialized = pdFALSE;
}
/**
 * @brief fills an array with per-task statistics copied from internal TCBs.
 */
UBaseType_t uxPtlGetStats(PTLTaskStats_t *pxStatsOut, UBaseType_t uxMaxTasks)
{
    UBaseType_t uxIdx;
    UBaseType_t uxFilled = 0;

    /* Safety check in case there aren't statistics to get */
    if (pxStatsOut == NULL || uxMaxTasks == 0)
    {
        return 0U;
    }

    for (uxIdx = 0; uxIdx < xGlobalPTLState.pxConfig->ucNumTasks && uxIdx < uxMaxTasks; uxIdx++)
    {
        const PTLTaskControlBlock_t *const pxTCB = &xGlobalPTLState.xTasks[uxIdx];

        /* Take the statistics from the private TCB in order to expose them */
        pxStatsOut[uxIdx].pcName = pxTCB->pxConfig->pcName;
        pxStatsOut[uxIdx].ulTotalJobs = pxTCB->ulTotalJobs;
        pxStatsOut[uxIdx].ulCompletedJobs = pxTCB->ulCompletedJobs;
        pxStatsOut[uxIdx].ulDeadlineMisses = pxTCB->ulDeadlineMisses;
        pxStatsOut[uxIdx].ulOverruns = pxTCB->ulOverruns;
        pxStatsOut[uxIdx].xPeriodTicks = pdMS_TO_TICKS(pxTCB->pxConfig->ulPeriodMs);
        pxStatsOut[uxIdx].ulWorstCaseResponse = pxTCB->ulWorstCaseResponse;

        /* In case of more completed jobs --> calculate the average response time */
        if (pxTCB->ulCompletedJobs > 0)
        {
            pxStatsOut[uxIdx].ulAvgResponseTime = pxTCB->ulTotalResponseTime / pxTCB->ulCompletedJobs;
        }
        /* Otherwise it's 0 */
        else
        {
            pxStatsOut[uxIdx].ulAvgResponseTime = 0;
        }

        uxFilled++;
    }

    return uxFilled;
}

#endif /* configUSE_PTL_SCHEDULER */
