/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "aperiodic_tasks.h"

#if ( configUSE_POLLING_SERVER == 1 )

/* Maximum number of jobs that can be stored in the queue at the same time */
#define APERIODIC_QUEUE_LEN     10
#define APERIODIC_ITEM_SIZE     sizeof( AperiodicJob_t )

/* Internal queue handle, accessible only within this file */
static QueueHandle_t xQueueHandle = NULL;

/* Internal buffer used to sort jobs by priority.
 * When a new job is submitted, all jobs are extracted, sorted
 * by priority (highest first), and re-inserted into the queue.
 */
static AperiodicJob_t xSortBuffer[ APERIODIC_QUEUE_LEN ];

/* Sorts the queue by priority (highest value served first).
 * Extracts all jobs into a local buffer, performs insertion sort,
 * then refills the queue in sorted order.
 * Must be called only from task context, not from ISR.
 */
static void prvSortQueueByPriority( void )
{
    int32_t lCount = 0;

    /* Extract all jobs from the queue into the buffer */
    while( xQueueReceive( xQueueHandle, &xSortBuffer[ lCount ], 0 ) == pdTRUE )
    {
        lCount++;
    }

    /* Insertion sort by priority, descending (highest priority first) */
    for( int32_t lIdx = 1; lIdx < lCount; lIdx++ )
    {
        AperiodicJob_t xKey = xSortBuffer[ lIdx ];
        int32_t lPos = lIdx - 1;

        while( lPos >= 0 && xSortBuffer[ lPos ].ucPriority < xKey.ucPriority )
        {
            xSortBuffer[ lPos + 1 ] = xSortBuffer[ lPos ];
            lPos--;
        }
        xSortBuffer[ lPos + 1 ] = xKey;
    }

    /* Re-insert sorted jobs back into the queue */
    for( int32_t lIdx = 0; lIdx < lCount; lIdx++ )
    {
        xQueueSend( xQueueHandle, &xSortBuffer[ lIdx ], 0 );
    }
}

/* Initialises the aperiodic job queue.
 * Must be called once in main() before the scheduler starts.
 */
void vAperiodicInit( void )
{
    xQueueHandle = xQueueCreate( APERIODIC_QUEUE_LEN, APERIODIC_ITEM_SIZE );
}

/* Submits a new aperiodic job to the queue.
 * Records arrival time automatically.
 * After insertion, sorts the queue by priority if called from task context.
 *
 * @return pdTRUE if job was added, pdFALSE if queue is full or not initialised
 */
BaseType_t xAperiodicSubmitJob( void (*pvFunction)(void*),
                                void *pvArgs,
                                const char *pcName,
                                TickType_t xSoftDeadline,
                                uint8_t ucPriority )
{
    if( xQueueHandle == NULL )
    {
        return pdFALSE;
    }

    AperiodicJob_t xNewJob;
    xNewJob.pvFunction    = pvFunction;
    xNewJob.pvArgs        = pvArgs;
    xNewJob.pcName        = pcName;
    xNewJob.xSoftDeadline = xSoftDeadline;
    xNewJob.ucPriority    = ucPriority;

    /* ISR context and task context need different FreeRTOS API calls */
    if( xPortIsInsideInterrupt() )
    {
        /* From ISR: use FromISR variant, no sorting possible here */
        xNewJob.xArrivalTime = xTaskGetTickCountFromISR();

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        BaseType_t xResult = xQueueSendFromISR( xQueueHandle,
                                               &xNewJob,
                                               &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
        return xResult;
    }
    else
    {
        /* From task: record arrival time, insert, then sort by priority */
        xNewJob.xArrivalTime = xTaskGetTickCount();

        BaseType_t xResult = xQueueSend( xQueueHandle, &xNewJob, 0 );

        if( xResult == pdTRUE )
        {
            prvSortQueueByPriority();
        }

        return xResult;
    }
}

/* Returns the handle to the internal aperiodic job queue.
 * Intended to be used ONLY by the Polling Server.
 *
 * @return handle to the aperiodic job queue
 */
QueueHandle_t xAperiodicGetQueueHandle( void )
{
    return xQueueHandle;
}

#endif /* configUSE_POLLING_SERVER */
