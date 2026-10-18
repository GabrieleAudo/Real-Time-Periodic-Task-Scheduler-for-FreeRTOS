/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef POLLING_SERVER_H
#define POLLING_SERVER_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#if ( configUSE_POLLING_SERVER == 1 )

/* Policy applied when an aperiodic job exceeds its soft deadline */
typedef enum {
    PS_POLICY_OVERRUN = 0,  /* let the job finish, log the miss */
    PS_POLICY_KILL    = 1   /* kill the job immediately on deadline miss */
} PSAperiodicPolicy_t;

/* Configuration structure for the Polling Server.
 * Passed once to xPollingServer_Init() before the scheduler starts.
 */
typedef struct {
    UBaseType_t uxPriority;             /* FreeRTOS priority of the server task */
    uint32_t ulStackSize;               /* stack size in words */
    uint32_t ulPeriodMs;                /* server activation period in milliseconds */
    uint32_t ulCapacityMs;              /* budget per activation in milliseconds */
    PSAperiodicPolicy_t xPolicy;        /* policy applied on aperiodic job deadline miss */
} PSConfig_t;

/* Initialises the Polling Server.
 * Creates the internal worker task and the synchronisation semaphores.
 * Must be called once in main() before the scheduler starts.
 *
 * @param pxConfig pointer to the server configuration
 * @return pdPASS on success, pdFAIL if resources could not be created
 */
BaseType_t xPollingServer_Init( const PSConfig_t *pxConfig );

/* Polling Server task body.
 * At each activation, drains the aperiodic job queue within the budget.
 * Applies the configured policy on soft deadline miss.
 * Logs all events via vTraceLog.
 * Must be registered as a periodic task in the PTL configuration.
 */
void vPollingServer_Task( void *pvParameters );

#endif /* configUSE_POLLING_SERVER */

#endif /* POLLING_SERVER_H */
