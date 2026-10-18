/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "PollingServer.h"
#include "aperiodic_tasks.h"
#include "semphr.h"
#include "trace.h"

#if ( configUSE_POLLING_SERVER == 1 )

/* Handle of the worker task that physically executes aperiodic jobs */
static TaskHandle_t xWorkerHandle = NULL;

/* Semaphore used by the server to signal the worker to start a job */
static SemaphoreHandle_t xWorkerStartSem = NULL;

/* Semaphore used by the worker to signal the server that the job is done */
static SemaphoreHandle_t xWorkerDoneSem = NULL;

/* Copy of the server configuration, saved at init time */
static PSConfig_t xServerConfig;

/* The job currently being executed by the worker */
static AperiodicJob_t xCurrentJob;

/* Tracks the polling server's activation count for tracing */
static uint32_t ulPsJobCount = 0;

/* Worker task body.
 * Waits indefinitely for the server to signal a new job via xWorkerStartSem.
 * Executes the job function, then signals completion via xWorkerDoneSem.
 * Runs in an infinite loop — never returns.
 */
static void prvWorkerTask(void *pvParams) {
  (void)pvParams;

  while (1) {
    /* Wait for the server to assign a new job */
    xSemaphoreTake(xWorkerStartSem, portMAX_DELAY);

    /* Execute the job if the function pointer is valid */
    if (xCurrentJob.pvFunction != NULL) {
      xCurrentJob.pvFunction(xCurrentJob.pvArgs);
    }

    /* Signal the server that the job is complete */
    xSemaphoreGive(xWorkerDoneSem);
  }
}

/* Destroys the current worker and creates a fresh one.
 * Yields to the idle task so FreeRTOS can free the old stack,
 * then flushes both semaphores to prevent stale tokens.
 *
 * @return pdPASS on success, pdFAIL if the new task could not be created
 */
static BaseType_t prvRecreateWorker(void) {
  /* Kill the running worker */
  if (xWorkerHandle != NULL) {
    vTaskDelete(xWorkerHandle);
    xWorkerHandle = NULL;
  }

  /* Yield so the idle task can free the deleted task's stack memory.
   * Without this, repeated kill+create cycles exhaust the heap. */
  taskYIELD();

  /* Create a fresh worker */
  BaseType_t xResult =
      xTaskCreate(prvWorkerTask, "PS_Worker", xServerConfig.ulStackSize, NULL,
                  xServerConfig.uxPriority, &xWorkerHandle);

  if (xResult != pdPASS) {
    xWorkerHandle = NULL;
    return pdFAIL;
  }

  /* Flush any stale tokens left in the semaphores by the old worker.
   * A binary semaphore can hold at most one token; a non-blocking
   * take drains it if present, or does nothing if already empty. */
  xSemaphoreTake(xWorkerStartSem, 0);
  xSemaphoreTake(xWorkerDoneSem, 0);

  return pdPASS;
}

/* Initialises the Polling Server.
 * Saves the configuration, initialises the aperiodic queue,
 * creates the synchronisation semaphores and the worker task.
 *
 * @param pxConfig pointer to the server configuration
 * @return pdPASS on success, pdFAIL if resources could not be created
 */
BaseType_t xPollingServer_Init(const PSConfig_t *pxConfig) {
  /* Save configuration for later use in vPollingServer_Task */
  xServerConfig = *pxConfig;

  /* Create the binary semaphores for server-worker synchronisation */
  xWorkerStartSem = xSemaphoreCreateBinary();
  xWorkerDoneSem = xSemaphoreCreateBinary();

  if ((xWorkerStartSem == NULL) || (xWorkerDoneSem == NULL)) {
    return pdFAIL;
  }

  /* Create the worker task that will physically execute aperiodic jobs */
  BaseType_t xResult = xTaskCreate(prvWorkerTask, "PS_Worker", pxConfig->ulStackSize,
                                  NULL, pxConfig->uxPriority, &xWorkerHandle);

  return xResult;
}

/* Polling Server task body.
 * Called at every periodic activation by the PTL.
 * Drains the aperiodic job queue within the available budget.
 * For each job, waits for completion with a timeout equal to the
 * minimum between the job soft deadline and the remaining budget.
 * Applies the configured policy on soft deadline miss.
 * Logs all events via vTraceLog.
 *
 * @param pvParameters unused
 */
void vPollingServer_Task(void *pvParameters) {
  (void)pvParameters;

  AperiodicJob_t xJob;
  QueueHandle_t xQueue = xAperiodicGetQueueHandle();

  /* Record the activation time to track budget consumption */
  TickType_t xActivationTime = xTaskGetTickCount();
  TickType_t xCapacityTicks = pdMS_TO_TICKS(xServerConfig.ulCapacityMs);

  if (xQueue == NULL) {
    return;
  }

  ulPsJobCount++;

  vTraceLog("PollSrv", ulPsJobCount, eTracePollingServer, eEvtServerRefill,
            xCapacityTicks);

  /* No initial flush is required. Since we enforce a mandatory KILL
   * on budget exhaustion, no worker is ever left running in the
   * background to produce stale synchronization tokens. */

  /* Drain the queue while budget remains and jobs are available */
  while (1) {
    /* Calculate remaining budget */
    TickType_t xElapsed = xTaskGetTickCount() - xActivationTime;
    TickType_t xRemaining =
        (xCapacityTicks > xElapsed) ? (xCapacityTicks - xElapsed) : 0;

    /* Stop if budget is exhausted */
    if (xRemaining == 0) {
      vTraceLog("PollSrv", ulPsJobCount, eTracePollingServer, eEvtServerExhaust,
                0);
      break;
    }

    /* Stop if no more jobs in queue */
    if (xQueueReceive(xQueue, &xJob, 0) != pdTRUE) {
      break;
    }

    /* Set the current job so the worker can access it */
    xCurrentJob = xJob;

    /* Timeout is the minimum between soft deadline and remaining budget */
    TickType_t xTimeout =
        (xJob.xSoftDeadline < xRemaining) ? xJob.xSoftDeadline : xRemaining;

    /* Log job release and signal the worker to start */
    vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtRelease, 0);
    xSemaphoreGive(xWorkerStartSem);

    /* Wait for the worker to finish within the timeout */
    if (xSemaphoreTake(xWorkerDoneSem, xTimeout) == pdTRUE) {
      /* SCENARIO A: Job completed within both soft deadline and budget */
      vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtComplete, 0);
    } else {
      /* Timeout expired. Check whether budget or soft deadline hit first. */
      TickType_t xNowElapsed = xTaskGetTickCount() - xActivationTime;

      if (xNowElapsed >= xCapacityTicks) {
        /* SCENARIO B: Budget Exhausted.
         * Hard limit reached. To prevent single-worker deadlocks and
         * PTL violations, any tolerant policy is overridden with a KILL. */
        vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtAperiodicKill,
                  xJob.xSoftDeadline);
        vTraceLog("PollSrv", ulPsJobCount, eTracePollingServer,
                  eEvtServerExhaust, 0);

        prvRecreateWorker();
        break;
      } else {
        /* SCENARIO C: Soft Deadline missed, but remaining budget > 0. */
        if (xServerConfig.xPolicy == PS_POLICY_KILL) {
          /* Immediate kill based on the configured policy */
          vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtAperiodicKill,
                    xJob.xSoftDeadline);
          prvRecreateWorker();
        } else {
          /* OVERRUN policy: Grant a final completion window using the remaining
           * budget */
          vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtAperiodicOverrun,
                    xJob.xSoftDeadline);

          TickType_t xExtraTime = xCapacityTicks - xNowElapsed;

          if (xSemaphoreTake(xWorkerDoneSem, xExtraTime) == pdTRUE) {
            /* Successfully completed by consuming the extra budget */
            vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtComplete, 0);
          } else {
            /* Extra budget exhausted. Hard limit reached -> KILL */
            vTraceLog("PollSrv", ulPsJobCount, eTracePollingServer,
                      eEvtServerExhaust, 0);
            vTraceLog(xJob.pcName, 1, eTraceAperiodic, eEvtAperiodicKill,
                      xJob.xSoftDeadline);

            prvRecreateWorker();
            break;
          }
        }
      }
    }
  }
}

#endif /* configUSE_POLLING_SERVER */
