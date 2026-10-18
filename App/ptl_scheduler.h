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

#ifndef PTL_SCHEDULER_H
#define PTL_SCHEDULER_H

#include "ptl_config.h"
#include "ptl_types.h"
#include "FreeRTOS.h"
#include <stdint.h>

#if ( configUSE_PTL_SCHEDULER == 1 )

/**
 * @brief Initialize the Periodic Task Layer
 * 
 * Creates all tasks (suspended) and their release timers based on the 
 * provided configuration.
 * 
 * @param config Pointer to scheduler configuration
 * @return pdPASS on success, pdFAIL on error
 */
BaseType_t xPtlInit(const PTLSchedulerConfig_t *pxConfig);

/**
 * @brief Start the Periodic Task Layer
 * 
 * Defines t0, calculates first release times with phases, resumes all tasks,
 * and starts all release timers.
 */
void vPtlStart(void);

/**
 * @brief Stops the PTL and cleanup resources
 * 
 * Stops all timers and deletes all tasks.
 */
void vPtlStop(void);

/**
 * @brief Fills an array with per-task statistics copied from internal TCBs.
 * 
 * @param pxStatsOut Array to be filled with statistics
 * @param uxMaxTasks Maximum number of tasks to get statistics for
 * @return Number of tasks for which statistics were obtained
 */
UBaseType_t uxPtlGetStats(PTLTaskStats_t *pxStatsOut, UBaseType_t uxMaxTasks);

#endif /* configUSE_PTL_SCHEDULER */

#endif /* PTL_SCHEDULER_H */