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

#ifndef PTL_CONFIG_H
#define PTL_CONFIG_H

#include "FreeRTOS.h"
#include "trace.h"
#include <stdint.h>

/* Configuration limits */
#define PTL_MAX_TASKS           10

/* Overrun policy types */
typedef enum
{
    ePolicyDefault  = -1,  /* Use global default policy (per-task sentinel) */
    ePolicySkip     = 0,   /* Skip new job, let current finish */
    ePolicyKill     = 1,   /* Kill current job, start new one */
    ePolicyCatchUp  = 2    /* Release new job immediately, mark previous missed */
} OverrunPolicy_t;

/* Task state enumeration */
typedef enum
{
    eStateTaskIdle = 0,       /* Task not running, waiting for release */
    eStateTaskRunning = 1,    /* Task is currently running */
    eStateTaskComplete = 2    /* Task completed current job */
} PTLTaskState_t;

/* Task entry function typedef */
typedef void (*PTLTaskFunction_t)(void *pvParameters);

/* Per-task configuration structure */
typedef struct
{
    const char *pcName;                 /* Task name */
    PTLTaskFunction_t pvEntryFunction;  /* Task entry function (job body) */
    void *pvEntryFunctionArgument;      /* Argument passed to task */
    uint32_t ulStackSize;               /* Stack size in words */
    UBaseType_t uxPriority;             /* FreeRTOS priority */
    uint32_t ulPeriodMs;                /* Period in milliseconds */
    uint32_t ulDeadlineMs;              /* Deadline in ms (0 = use period) */
    uint32_t ulPhaseMs;                 /* Phase offset in ms (0 = start at t0) */
    TraceTaskType_t xType;              /* Type of task (Periodic or Polling Server)*/
    OverrunPolicy_t xPolicy;            /* Per-task policy override (ePolicyDefault = use global) */
} PTLTaskConfig_t;

/* Global scheduler configuration structure */
typedef struct
{
    OverrunPolicy_t xPolicy;                /* Global overrun policy */
    BaseType_t xTraceEnabled;               /* Enable trace logging --> it records the event in trace buffer (defined at the beginning of the file): task release, task complete, task missed, overrun skip, overrun kill, overrun catch up */ 
    uint8_t ucNumTasks;                     /* Number of tasks in configuration */
    PTLTaskConfig_t xTasks[PTL_MAX_TASKS];  /* Array of task configurations */
} PTLSchedulerConfig_t;

#endif /* PTL_CONFIG_H */