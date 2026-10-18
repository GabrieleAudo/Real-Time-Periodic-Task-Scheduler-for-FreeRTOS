#ifndef PTL_TYPES_H
#define PTL_TYPES_H

#include "FreeRTOS.h"
#include <stdint.h>

/*per-task structure used to expose statistics by the scheduler */
typedef struct {
    const char *pcName;
    uint32_t    ulTotalJobs;
    uint32_t    ulCompletedJobs;
    uint32_t    ulDeadlineMisses;
    uint32_t    ulOverruns;
    uint32_t    ulAvgResponseTime;    /* Average response time in ticks */
    uint32_t    ulWorstCaseResponse;  /* Worst case response time in ticks */
    TickType_t  xPeriodTicks;         /* Period in ticks */
} PTLTaskStats_t;

#endif /* PTL_TYPES_H */
