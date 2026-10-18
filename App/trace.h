#ifndef TRACE_H
#define TRACE_H

#include "FreeRTOS.h"
#include "task.h"
#include "ptl_types.h"  /*for PTLTaskStats_t*/

#define TRACE_BUFFER_SIZE   ( 512U ) /*U to indicate unsigned to be coherent with uint32_t comparisons*/

typedef enum {
    eTracePeriodic          = 0,
    eTraceAperiodic         = 1,
    eTracePollingServer     = 2
} TraceTaskType_t;

typedef enum {
    /* Periodic task events */
    eEvtRelease         = 0,  /* job released (when new period starts) */
    eEvtComplete        = 1,  /* job completed within its deadline */
    eEvtDeadlineMiss    = 2,  /* job finished after its absolute deadline */
    eEvtOverrunSkip     = 3,  /* for period overrun SKIP policy */
    eEvtOverrunKill     = 4,  /* for period overrun KILL policy */
    eEvtOverrunCatchup  = 5,  /* for period overrun CATCH_UP policy */

    /* Aperiodic task events */
    eEvtAperiodicOverrun = 6, /* soft deadline exceeded, but allowed to continue (OVERRUN policy) */
    eEvtAperiodicKill    = 7, /* killed on soft deadline miss (KILL policy) */

    /* Polling server events */
    eEvtServerRefill    = 8,  /* server budget replenished at new period */
    eEvtServerExhaust   = 9,  /* server budget exhausted before period end */
} TraceEventID_t;

typedef struct {
    TickType_t      xTimestamp; /* timestamp at log time*/
    
    char            pcTaskName[configMAX_TASK_NAME_LEN]; /* instead of using pointer,
                                                              defensive way to copy the name */
    /*
        Reason why defensive copy and not using the pointer:
            when we do vTraceDump then the pointer points to deallocated 
            portion of memory --> dangling pointer.
            This could cause a system fault in a non-deterministic way.    
    */

    TraceTaskType_t xTaskType;  /* periodic, aperiodic or polling server*/
    uint32_t ulJobNumber;       /* current job iteration of the task*/
    TraceEventID_t  xEventID;   /* periodic event, aperiodic event or polling server event */
    uint32_t        ulData;     /* numerical information which is related to the specific event traced 
                                   (it can be used for period, response_time, deadline...) */
} TraceRecord_t;

/* Initialises the trace system. */
void vTraceInit(void);

/** Logs the event using the circular buffer.
 * Thread-safe --> protected by a FreeRTOS critical section.
 *                 Otherwise two different tasks could write information on the same
 *                 block of the buffer losing information about the previous task.
 * 
 * Must NOT be called from an ISR.
 * The use of enter/exit critical section disable ISR and if it's called from an ISR,
 * it will be disabled and there will be a system fault.
 *
 * @param pcTaskName  Name of the task/server generating the event
 * @param xType       Entity kind (periodic / aperiodic / server)
 * @param xEvent      Event identifier
 * @param ulData      Optional payload (e.g. absolute deadline tick on a miss,
 *                    remaining budget on exhaust, 0 if unused).
 */
void vTraceLog( const char     *pcTaskName,
                uint32_t        ulJobNumber,
                TraceTaskType_t xType,
                TraceEventID_t  xEvent,
                uint32_t        ulData );


/**
 * prints per-task statistics from the scheduler
 *
 * @param pxStats     array filled by uxPtlGetStats
 * @param uxNumTasks  number of valid entries in pxStats
 */
void vTraceStatsDump(const PTLTaskStats_t *pxStats, UBaseType_t uxNumTasks);


/* Prints the entire trace buffer to UART in chronological order.
 * Expensive --> intended to be called only when the system is idle 
 * or after scheduler ended.
 */
void vTraceDump(void);

/* Returns the number of times a specific event was logged in the current buffer.
 * Useful for automated test evaluation of aperiodic and polling server events.
 */
uint32_t ulTraceGetEventCount(TraceEventID_t xEvent);

#endif /*TRACE_H*/
