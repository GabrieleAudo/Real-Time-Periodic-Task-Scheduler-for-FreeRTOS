#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include "trace.h"
#include "uart.h"


static const char * const pcEventNames[] =
{
    "release",  
    "complete",
    "deadline miss",
    "overrun -> skip",
    "overrun -> kill",
    "overrun -> catchup",
    "aperiodic overrun",
    "aperiodic kill",
    "server refill",
    "server exhaust"
}; /* considering the index mapping with TraceEventID_t enumeration 
        so release -->0, complete-->1, and so on... */

static const char * const pcTypeNames[] =
{
    "Periodic",
    "Aperiodic",
    "PollingServer"
}; /* considering the index mapping with TraceTaskType_t enumaration */

static TraceRecord_t xTraceBuffer[ TRACE_BUFFER_SIZE ];

/* index of the next slot to be written */
static volatile uint32_t ulHead = 0U;

/* total number of written records*/
static volatile uint32_t ulTotalWritten = 0U;

void vTraceInit( void ) {

    taskENTER_CRITICAL();
    {
        memset( xTraceBuffer, 0, sizeof( xTraceBuffer ) ); /* xTraceBuffer contents set to zero*/
        ulHead         = 0U;
        ulTotalWritten = 0U;
    }
    taskEXIT_CRITICAL();

    vUartPrintf( "[Trace] Initialised.\r\n" );
}

void vTraceLog( const char *pcTaskName, uint32_t ulJobNumber, TraceTaskType_t xType,
                TraceEventID_t  xEvent, uint32_t ulData ) {
    
    /* Acquire timestamp before entering critical section to keep it smaller as possible. */
    TickType_t xNow = xTaskGetTickCount();

    taskENTER_CRITICAL();
    {
        TraceRecord_t* const pxSlot = &xTraceBuffer[ulHead];

        pxSlot->xTimestamp = xNow;

        strncpy(pxSlot->pcTaskName, pcTaskName, configMAX_TASK_NAME_LEN - 1U);
        pxSlot->pcTaskName[configMAX_TASK_NAME_LEN - 1U] = '\0';

        pxSlot->xTaskType = xType;
        pxSlot->ulJobNumber = ulJobNumber;
        pxSlot->xEventID  = xEvent;
        pxSlot->ulData    = ulData;

        ulHead = (ulHead + 1U) % TRACE_BUFFER_SIZE; /* circular buffer */
        ulTotalWritten++;
    }
    taskEXIT_CRITICAL();
}

void vTraceDump( void ) {
    uint32_t ulHeadSnap;  /* snapshot of ulHead*/
    uint32_t ulTotalSnap; /* snapshot of ulTotalWritten*/
    uint32_t ulCount;     /* used to indicate how many valid records have to be dumped*/
    uint32_t ulStart;     /* used to indicate from which record the dump has to start*/
    uint32_t ulIter;
    char cLine[128];      /* temporary buffer used for the snprintf to pass to the uart*/

    /* atomic snapshot of indices to avoid any corruption during the vUartPrintf,
       it can be slower than other log operations, messing up with indices */
    taskENTER_CRITICAL();
    {
        ulHeadSnap  = ulHead;
        ulTotalSnap = ulTotalWritten;
    }
    taskEXIT_CRITICAL();

    /* determine how many valid records exist and where the oldest one is*/
    if(ulTotalSnap >= TRACE_BUFFER_SIZE)
    {
        /* circular buffer --> one circle has been completed 
           every slot is valid, oldest is at ulHead */
        ulCount = TRACE_BUFFER_SIZE;
        ulStart = ulHeadSnap;
    }
    else
    {
        /* valid records are from 0 to ulHead */
        ulCount = ulTotalSnap;
        ulStart = 0U;
    }

    vUartPrintf( "\r\n=== TRACE DUMP START ===\r\n" );

    snprintf( cLine, sizeof(cLine), "Total records: %lu\r\n", (unsigned long) ulCount );
    vUartPrintf(cLine);

    vUartPrintf( "Tick     Type            Task             Iteration    Event                  Data\r\n" );
    vUartPrintf( "----------------------------------------------------------------------------------\r\n" );

    /*print records in chronological order */
    for(ulIter = 0; ulIter < ulCount; ulIter++) {
        uint32_t ulBufIdx = (ulStart + ulIter) % TRACE_BUFFER_SIZE; /* to handle circular buffer turn-around*/
        const TraceRecord_t* const pxRec = &xTraceBuffer[ulBufIdx]; /* const pointer to the record (as const)*/
        const char *pcEvtName;
        const char *pcTypeName;

        /* safe lookup for event name */
        /* the check here is executed at run-time */
        if( (uint32_t) pxRec->xEventID < (sizeof(pcEventNames) / sizeof(pcEventNames[0])) ){
            pcEvtName = pcEventNames[pxRec->xEventID];
        }
        else {
            pcEvtName = "unknown event";
        }

        /* safe lookup for type name */
        if( (uint32_t) pxRec->xTaskType < (sizeof(pcTypeNames) / sizeof(pcTypeNames[0])) ){
            pcTypeName = pcTypeNames[pxRec->xTaskType];
        }
        else {
            pcTypeName = "unknown type";
        }

        snprintf( cLine, sizeof(cLine),
                  "%-8lu %-15s %-16s %-12lu %-22s %lu\r\n", /*visually formatting the string*/
                  (unsigned long) pxRec->xTimestamp,
                  pcTypeName,
                  pxRec->pcTaskName,
                  (unsigned long) pxRec->ulJobNumber,
                  pcEvtName,
                  (unsigned long) pxRec->ulData );

        vUartPrintf(cLine);
    }

    vUartPrintf( "=== TRACE DUMP END ===\r\n\r\n" );
}

void vTraceStatsDump(const PTLTaskStats_t *pxStats, UBaseType_t uxNumTasks)
{
    UBaseType_t uxIdx;
    char cLine[128];

    /*check to see if there are not statistics to print*/
    if(pxStats == NULL || uxNumTasks == 0U) {
        vUartPrintf( "[Trace] No statistics available.\r\n" );
        return;
    }

    vUartPrintf( "\r\n=== STATISTICS DUMP START ===\r\n" );
    vUartPrintf( "Task            Jobs  Done  DL Miss  DL Miss%  Overruns  AvgResp  WCRT         \r\n" );
    vUartPrintf( "-------------------------------------------------------------------------------\r\n" );

    for(uxIdx = 0U; uxIdx < uxNumTasks; uxIdx++) {
        const PTLTaskStats_t * const pxS = &pxStats[uxIdx];

        /* deadline miss percentage*/
        uint32_t ulMissPct = 0U;
        if(pxS->ulTotalJobs > 0U) {
            ulMissPct = (pxS->ulDeadlineMisses * 100U) / pxS->ulTotalJobs;
        }

        snprintf(cLine, sizeof(cLine),
                  "%-16s %-5lu %-5lu %-8lu %-9lu %-9lu %-8lu %-5lu \r\n", //formatting the string to print it following the pattern determined above
                  pxS->pcName,
                  (unsigned long) pxS->ulTotalJobs,
                  (unsigned long) pxS->ulCompletedJobs,
                  (unsigned long) pxS->ulDeadlineMisses,
                  (unsigned long) ulMissPct,
                  (unsigned long) pxS->ulOverruns,
                  (unsigned long) pxS->ulAvgResponseTime,
                  (unsigned long) pxS->ulWorstCaseResponse
                );

        vUartPrintf(cLine);
    }

    vUartPrintf( "=== STATISTICS DUMP END ===\r\n\r\n" );
}

uint32_t ulTraceGetEventCount(TraceEventID_t xEvent)
{
    uint32_t ulCount = 0;
    uint32_t ulIdx;
    uint32_t ulTotal = (ulTotalWritten < TRACE_BUFFER_SIZE) ? ulTotalWritten : TRACE_BUFFER_SIZE;
    
    taskENTER_CRITICAL();
    for(ulIdx = 0; ulIdx < ulTotal; ulIdx++) {
        if(xTraceBuffer[ulIdx].xEventID == xEvent) {
            ulCount++;
        }
    }
    taskEXIT_CRITICAL();
    return ulCount;
}

