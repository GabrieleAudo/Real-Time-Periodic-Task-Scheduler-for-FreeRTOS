#ifndef APERIODIC_TASKS_H
#define APERIODIC_TASKS_H

#include "FreeRTOS.h"
#include "queue.h"   
#include "task.h"    

#if ( configUSE_POLLING_SERVER == 1 )

/* Structure representing an aperiodic job.
 * An aperiodic job is not a FreeRTOS task but a function pointer
 * that can be submitted to the Polling Server for execution.
 * It carries the function to execute, its arguments, the time it
 * arrived in the queue, its soft deadline, and a priority value
 * used by the server to serve higher-priority jobs first.
 */

typedef struct {
    const char *pcName;            /* job name, used for trace logging */
    void (*pvFunction)(void*);     /* pointer to the function to execute */
    void *pvArgs;                  /* arguments passed to the function */
    TickType_t xArrivalTime;       /* tick at which the job was submitted to the queue */
    TickType_t xSoftDeadline;      /* max execution time in ticks from when the server starts it */
    uint8_t ucPriority;            /* job priority: higher value = served first (0 = lowest) */
} AperiodicJob_t;


/* Initialization aperiodic job queue.
 * Must be called once in main() before the scheduler starts.
 * Creates the internal FreeRTOS queue used to store incoming jobs.
 */
void vAperiodicInit( void );

/* Submits a new aperiodic job to the queue.
 * Can be called from both a task context and an ISR context.
 * Records the arrival time automatically at submission.
 * Jobs are ordered by priority: higher priority jobs are served first.
 *
 * @param pvFunction   pointer to the function to execute
 * @param pvArgs       arguments passed to the function (NULL if none)
 * @param pcName       job name, used for trace logging
 * @param xSoftDeadline max execution time in ticks from when the server starts it
 * @param ucPriority   job priority (0 = lowest, 255 = highest)
 *
 * @return pdTRUE if the job was added to the queue, pdFALSE if the queue is full
 */
BaseType_t xAperiodicSubmitJob( void (*pvFunction)(void*),
                                void *pvArgs,
                                const char *pcName,
                                TickType_t xSoftDeadline,
                                uint8_t ucPriority );


/* Returns the handle to the internal aperiodic job queue.
 * Intended to be used ONLY by the Polling Server to consume jobs.
 * Must not be called before vAperiodicInit().
 *
 * @return handle to the aperiodic job queue
 */
QueueHandle_t xAperiodicGetQueueHandle( void );

#endif /* configUSE_POLLING_SERVER */

#endif