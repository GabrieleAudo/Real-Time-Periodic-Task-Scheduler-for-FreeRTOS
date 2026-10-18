# EOS Project 3 — Priority-Based Scheduler with Polling Server for Periodic Tasks in FreeRTOS

## Table of Contents

- [Team & Work Division](#team--work-division)
- [1. Scheduler (Periodic Task Layer)](#1-scheduler-periodic-task-layer)
- [2. Aperiodic Task Management](#2-aperiodic-task-management)
- [3. Polling Server](#3-polling-server)
- [4. Trace and Monitoring System](#4-trace-and-monitoring-system)
- [5. Configurator and Automated Test Runner](#5-configurator-and-automated-test-runner)
- [6. Test Suite](#6-test-suite)

---

## Team & Work Division

- Gabriele Audo (s349636): 
    - **Competencies**: Periodic Task Layer Scheduler 
    - **Files**: ptl_scheduler.c, ptl_scheduler.h, ptl_config.h
- Pietro Ala (s352318):
    - **Competencies**: Aperiodic job submission and queue management
    - **Files**: aperiodic_tasks.c, aperiodic_tasks.h
- Gabriele Loguercio Polosa (s348818):
  - **Competencies**: Polling Server
  - **Files**: PollingServer.c, PollingServer.h
- Andrea De Vito (s349609):
    - **Competencies**: Trace and monitoring system
    - **Files**: ptl_types.h, trace.h, trace.c
- Ivan Maria Puzelli (s348538):
    - **Competencies**: Configurator, Testing auto run and definition, Polling Server  debugging, Overhead calculation. 
    - **Files**: configurator.py, test*.c, PollingServer.c , main.c
- Davide Di Pasquale (s348493):
    - **Competencies**: Test the entire system
    - **Files**: test*.c

---

## 1. Scheduler (Periodic Task Layer)

### 1. Introduction
- This project aims to create a real-time periodic scheduling layer built on top of the default FreeRTOS scheduler. This Periodic Task Layer (PTL) enforces periodicity and deadlines of tasks and impelements many handling policies for overruns. 
- Morevoer, it includes a Polling Server, a special periodic task which handles aperiodic event-driven tasks, without violating the temporal constraints of the other real-time periodic tasks. 
- The project also comprehends a Trace Logger which logs all events happening inside the PTL with precise timestamps, useful to observe the scheduler behavior. 
- Additionally, an high-level configuration framework was developed, which enables the user to easily configure a scenario by specifying all task parameters and let the PTL do the rest.
- Finally, the project is completed by an Automated Testing Suite which aims to check the scheduler behavior against a predefined set of test scenarios to ensure that timing constraints are met in all possible conditions.

### 2. Periodic Task Layer
- The **Periodic Task Layer (PTL)** is a compatibility layer for periodic tasks built on top of the priority-based default FreeRTOS scheduler. It manages syncronization, deadline miss and overrun handling with different policies and statistics collection about tasks.
#### 2.1 Data Structures and types
##### *PTLTaskConfig_t*

This data structure contains the user-provided configuration for a single task, which is then used to create the periodic task inside the PTL

| Type              | Name                    | Description |
| ----------------- | ----------------------- | ----------- |
| const char *      | pcName                  | The task name|
| PTLTaskFunction_t | pvEntryFunction         | The user-defined function that implements the task logic |
| void *            | pvEntryFunctionArgument | Pointer to the user-defined argument passed to task|
| uint32_t          | ulStackSize             | Stack size in words |
| UBaseType_t       | uxPriority              | FreeRTOS priority |
| uint32_t          | ulPeriodMs              | Period in milliseconds |
| uint32_t          | ulDeadlineMs            | Deadline in ms (if equals 0, deadline will be equal to period) |
| uint32_t          | ulPhaseMs               | Phase offset in ms (if equals 0, task will start at t0) |
| TraceTaskType_t   | xType                   | Type of task (Periodic or Polling Server) |
| OverrunPolicy_t   | xPolicy                 | Per-task policy override (if equal to ePolicyDefault, global default policy will be used) |

##### *PTLSchedulerConfig_t*

This data structure contains the PTL scheduler configuration, which comprehends all info needed to initialize and start the PTL, including the global policy and the array of task configurations of the periodic tasks the user wants to create

| Type            | Name                  | Description |
| --------------- | --------------------- | ----------- |
| OverrunPolicy_t | xPolicy               | The global default overrun policy|
| BaseType_t      | xTraceEnabled         | Enables trace logging of task events |
| uint8_t         | ucNumTasks            | Number of tasks in the configuration |
| PTLTaskConfig_t | xTasks[PTL_MAX_TASKS] | Array of tasks configurations |

##### *PTLTaskControlBlock_t*

This data structure represent the **Task Control Block (TCB)** that will be used by the PTL to keep all information about the task. It will be addressed anytime a function needs to obtain information about that task. It contains also tasks statistics that will be used to evaluate the performance of the scheduler.

| Type                    | Name                | Description |
| ----------------------- | ------------------- | ----------- |
| TaskHandle_t            | xHandle             | The FreeRTOS task handle |
| TimerHandle_t           | xReleaseTimer       | Software timer used to implement task release |
| const PTLTaskConfig_t * | pxConfig            | Pointer to the task configuration provided by the user |
| TickType_t              | xReleaseTime        | Time (in ticks) of when task was released |
| TickType_t              | xAbsoluteDeadline   | Time (in ticks) of the next deadline on this task |
| uint32_t                | ulJobNumber         | The current job iteration of the task |
| PTLTaskState_t          | xState              | The current state of the task (idle, running or complete) |
| uint32_t                | xCatchUpPending     | A counter indicating how many catch-up jobs are currently pending|
| uint32_t                | ulTotalJobs         | Number of started job iterations |
| uint32_t                | ulCompletedJobs     | Number of completed job iterations |
| uint32_t                | ulDeadlineMisses    | Number of missed deadlines |
| uint32_t                | ulOverruns          | Number of period overruns events |
| uint32_t                | ulTotalResponseTime | Total sum of the response time of the task |
| uint32_t                | ulWorstCaseResponse | Worst case response time of the task (WCRT) |

##### *globalPTLState* 

The data structure which contains the PTL current state. It is used to keep track of all the tasks created in the PTL to allow for statistics extraction and correct task initialization, start and stop.

| Type                         | Name                  | Description |
| ---------------------------- | --------------------- | ----------- |
| const PTLSchedulerConfig_t * | pxConfig              | Pointer to the PTL configuration |
| PTLTaskControlBlock_t        | xTasks[PTL_MAX_TASKS] | Array containing all tasks TCBs |
| TickType_t                   | xTimeZero             | The ticks corresponding to ```t0```, the syncronization start time for all tasks |
| BaseType_t                   | xInitialized          | Flag indicating if the PTL has been initialized |
| BaseType_t                   | xStarted              | Flag indicating if the PTL has been started |

#### 2.2 Exposed APIs

##### *xPtlInit*

```BaseType_t xPtlInit(const PTLSchedulerConfig_t *pxConfig);```
- Description: This function initializes the PTL. Creates all tasks suspended and their release timers based on the provided configuration

- Parameters: ```pxConfig``` the scheduler configuration

- Returns: ```pdPASS``` on success, ```pdFAIL``` on error 

##### *vPtlStart*

```void vPtlStart(void);```
- Description: This function defines ```t0```, calculates first release times with phases, resumes all tasks and starts all release timers

- Parameters: ```None``` 

- Returns: ```None``` 


##### *vPtlStop*

```void vPtlStop(void);```
- Description: Stops the PTL. Stops all timers and release all resources by deleting all created timers and tasks. Also extracts all task statistics and calls function to print them if tracing is enabled

- Parameters: ```None``` 

- Returns: ```None``` 

#### 2.3 Periodic Scheduler Functions


##### *prvPeriodicTaskWrapper* 

```static void prvPeriodicTaskWrapper(void *pvParameters);```
- Description: a FreeRTOS task which implements a simple wrapper that wraps the user provided function in a infinite loop to implement a periodic task. Waits each time for a task notification before releasing next job

- Parameters: ```pvParameters``` a pointer to the parameter to use in the FreeRTOS task. In this case it's a pointer to the Task's TCB.

- Returns: ```None```

##### *prvJobComplete* 

```static void prvJobComplete(PTLTaskControlBlock_t *pxTcb);```
- Description: the function called at the end of each job by the periodic task wrapper. It checks for deadline misses and computes task statistics. Also handles deferred release time and absolute deadline updates in case a catch-up job is pending.

- Parameters: ```pxTcb``` a pointer to the current Task Control Block

- Returns: ```None```

##### *prvReleaseTimerCallback* 

```static void prvReleaseTimerCallback(TimerHandle_t xTimer);```
- Description: the function called when the release timer fires. It detects overruns and handles them depending on the handling policy. Then updates task release time, deadline and task statistics

- Parameters: ```xTimer``` the period timer used to implement overrun detection and handling

- Returns: ```None```

##### *prvCleanupInit* 

```static void prvCleanupInit(uint8_t ucNumCreated);```
- Description: a cleanup function called by xPtlInit() in case PTL initialization fails. It is used to free all allocated resources by deleting tasks and timers which were successfully created

- Parameters: ```ucNumCreated``` number of tasks successfully created

- Returns: ```None```

##### *uxPtlGetStats*

```UBaseType_t uxPtlGetStats(PTLTaskStats_t *pxStatsOut, UBaseType_t uxMaxTasks);```
- Description: Provides collected statistics for all tasks present in the PTL.

- Parameters: ```pxStatsOut``` array provided by caller where to save all tasks statistics,  ```uxMaxTasks``` size of the provided array

- Returns: number of tasks for which statistics have been provided  

#### 2.4 Overrun Policy implementation

- For all policies, the overrun detection happens through a software timer which triggers the vReleaseTimerCallback function, which is the function implementing the policies. Moreover, job release is handled with task notifications, which provide much flexibility while remaining lightweight.
- For the task notifications, the max number of queued notifications that can be held is set to 5 inside the FreeRTOSConfig.h file:
```
#define configUSE_TASK_NOTIFICATIONS             1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    5
```

##### *SKIP*
- To implement the SKIP policy, when the release timer callback is invoked, the function returns before a new job iteration is released. In this way the next job is successfully skipped: 
```
case ePolicySkip:{
            /* Skip this release, let current job finish and ignore the new release */
...

            return; // return immediately, so new job is not released
}
```

##### *CATCH UP*
- CATCH UP policy is implemented by simply releasing a new job while the previous is still running. We also need to tell vJobComplete that a catch-up job is pending so that it can update release time and absolute deadline before new job is immediately released:
```
case ePolicyCatchUp:{
            /* Release new job immediately, mark previous missed. Don't skip, don't kill but release a new job */
...
            skip_update = 1;
            tcb->xCatchUpPending = pdTRUE; /* Tell vJobComplete that a new job is pending after the current one */
            break; // do not return!
}
...

/* Send notification to task to execute a new job */
/* If CATCH_UP is selected, new job will be pending */
xTaskNotifyGive(tcb->xHandle);
```

##### *KILL*
- KILL policy is implemented by deleting the old task and immediately creating a new one:
```
case ePolicyKill:
        {
            /* Kill current job and start new one */
            TaskHandle_t old_handle; // save the old task handle (need it for deletion)
            BaseType_t create_result;
...

            /* Delete old task */
            old_handle = tcb->xHandle;
            tcb->xHandle = NULL;
            tcb->xState = eStateTaskIdle;
            vTaskDelete(old_handle);

            /* Create new task */
            create_result = xTaskCreate(vPeriodicTaskWrapper, tcb->pxConfig->pcName,
                                        tcb->pxConfig->ulStackSize, (void *)tcb,
                                        tcb->pxConfig->uxPriority, &tcb->xHandle);
...
        }
```
- Then, a new job is immediately released:
```
    xTaskNotifyGive(tcb->xHandle);
```

---

## 2. Aperiodic Task Management

### 1. Introduction
- This document describes the **Aperiodic Task Management module** developed for the polling server infrastructure of the Periodic Task Layer (PTL).
- The aperiodic task module is used to represent, store, and submit **aperiodic jobs** that do not follow a fixed period and must be served by the polling server.
- The module provides a small abstraction over a FreeRTOS queue: producers can submit jobs, while the polling server later extracts and executes them according to its own budget and policy.
- This separation keeps aperiodic job submission independent from the scheduling logic implemented inside the polling server.

### 2. Aperiodic Task Management System
The aperiodic task management system is a lightweight job-queue layer built on top of FreeRTOS.

Its main goals are:
- to provide a **common representation** for aperiodic jobs,
- to store pending aperiodic jobs inside a **bounded FreeRTOS queue**,
- to support submissions both from normal task context and interrupt context,
- and to expose the queue handle to the polling server so that jobs can be served when server capacity is available.

#### 2.1 Stored Information
Each aperiodic job stores the minimum information required by the polling server to execute and monitor it:
- job name,
- function pointer to the job body,
- optional function parameters,
- arrival tick,
- and soft deadline.

The module does not execute jobs directly. It only creates and enqueues job descriptors. Actual execution is performed by the polling server infrastructure, usually through its worker task.

#### 2.2 Design Principles
- **Separation of concerns**: the aperiodic task module only manages job submission and storage; the polling server decides when and how to execute jobs.
- **Bounded memory usage**: pending jobs are stored in a fixed-length FreeRTOS queue.
- **FreeRTOS integration**: the module uses FreeRTOS queues and tick counters.
- **ISR compatibility**: job submission automatically selects the correct queue API depending on whether it is called from a task or from an ISR.
- **Simple job abstraction**: an aperiodic job is represented as a function pointer plus metadata, instead of being created as an independent FreeRTOS task.

#### 2.3 Operating Flow
The aperiodic task system is typically used in the following way:
1. The module is initialized with `vAperiodicInit()`.
2. A periodic task or an interrupt source submits a new aperiodic job using `xAperiodicSubmitJob()`.
3. The job descriptor is inserted into the internal FreeRTOS queue.
4. The polling server obtains the queue handle through `xAperiodicGetQueueHandle()`.
5. When the polling server is released and has available capacity, it dequeues one or more jobs and executes them through its worker task.
6. The polling server is responsible for enforcing soft deadlines, budget limits, kill/overrun policies, and trace logging.

### 3. Files Documentation

#### 3.1 `aperiodic_tasks.h`
This header defines the public interface of the aperiodic task module and the structure used to describe one aperiodic job.

##### 3.1.1 `AperiodicJob_t`
This data structure contains all metadata needed to submit and later execute one aperiodic job.

| Type | Name | Description |
| --- | --- | --- |
| `const char *` | `pcJobName` | Human-readable job name used for debugging and trace output. |
| `void (*)(void *)` | `pvTaskCode` | Function pointer to the code that must be executed for this aperiodic job. |
| `void *` | `pvTaskParams` | Optional parameter passed to the job function when it is executed. |
| `TickType_t` | `xArrivalTime` | Tick value captured when the job is submitted. |
| `TickType_t` | `xSoftDeadline` | Relative soft deadline or maximum tolerated execution window for the job. |

##### 3.1.2 Purpose of the structure
- The structure acts as a **job descriptor**.
- It does not create a FreeRTOS task by itself.
- It allows the polling server to know which function must be executed, when the job arrived, and how urgent it is.
- It keeps the aperiodic workload independent from the periodic task set.

##### 3.1.3 Exposed APIs

###### `vAperiodicInit`
```c
void vAperiodicInit(void);
```
- Initializes the aperiodic task module.
- Creates the internal FreeRTOS queue used to store pending jobs.
- Should be called before any job is submitted.
- In the current system, it is called during polling server initialization.

###### `xAperiodicSubmitJob`
```c
BaseType_t xAperiodicSubmitJob(void (*pvTaskCode)(void *),
                               void *pvTaskParams,
                               const char *pcJobName,
                               TickType_t xSoftDeadline);
```
- Parameters:
    - `pvTaskCode`: function pointer to the aperiodic job body.
    - `pvTaskParams`: optional parameters passed to the job body.
    - `pcJobName`: name associated with the job.
    - `xSoftDeadline`: soft deadline assigned to the job.
- Builds an `AperiodicJob_t` descriptor.
- Captures the current tick as the job arrival time.
- Inserts the descriptor into the internal queue.
- Returns `pdPASS` if the job is successfully enqueued, otherwise returns a FreeRTOS failure code.

###### `xAperiodicGetQueueHandle`
```c
QueueHandle_t xAperiodicGetQueueHandle(void);
```
- Returns the handle of the internal aperiodic job queue.
- Used by the polling server to receive pending jobs.
- The function does not remove jobs from the queue; it only exposes the queue handle.

#### 3.2 `aperiodic_tasks.c`
This source file contains the implementation of the aperiodic job queue and the job-submission API.

##### 3.2.1 Internal data structures
The implementation uses the following internal state:
- `xAperiodicQueue`: static FreeRTOS queue handle that stores pending `AperiodicJob_t` items.
- `APERIODIC_QUEUE_LEN`: maximum number of pending jobs that can be stored.
- `APERIODIC_ITEM_SIZE`: size of one queue item, equal to `sizeof(AperiodicJob_t)`.

##### 3.2.2 `vAperiodicInit()`
Implementation details:
- Creates the queue with `xQueueCreate()`.
- The queue length is fixed by `APERIODIC_QUEUE_LEN`.
- Each queue element stores one full `AperiodicJob_t` structure.

Behavioral note:
- The queue is bounded, so if too many aperiodic jobs are submitted before the polling server can consume them, new submissions may fail.
- This is intentional for embedded systems, because it avoids unbounded dynamic memory growth.

##### 3.2.3 `xAperiodicSubmitJob()`
Implementation details:
- Creates a local `AperiodicJob_t` variable.
- Fills the job name, function pointer, parameters, arrival time, and soft deadline.
- Uses `xTaskGetTickCount()` to timestamp the arrival of the job.
- Checks whether the caller is currently inside an interrupt context.
- If the caller is inside an ISR, it uses `xQueueSendFromISR()`.
- If the caller is a normal FreeRTOS task, it uses `xQueueSend()`.

Behavioral note:
- The function is safe to call both from tasks and from interrupt service routines.
- This is important because aperiodic work may be generated by normal tasks, external interrupts, timers, buttons, sensors, or communication peripherals.
- From an ISR, the function cannot use a potentially blocking queue operation; therefore, it must use the FreeRTOS `FromISR` API variant.

##### 3.2.4 `xAperiodicGetQueueHandle()`
Implementation details:
- Returns the static queue handle stored by the module.
- Does not allocate memory.
- Does not modify the queue content.

Behavioral note:
- The polling server uses this function to access the queue and dequeue pending jobs.
- This keeps the queue itself private to the module, while still allowing the polling server to consume its contents.

### 4. Interaction with the Polling Server
The aperiodic task module does not schedule or execute jobs by itself. It only provides pending work to the polling server.

The interaction is as follows:
1. A producer calls `xAperiodicSubmitJob()`.
2. The job is placed in the aperiodic queue.
3. The polling server wakes up according to its own period.
4. The polling server checks the queue.
5. If jobs are available and budget remains, the polling server extracts a job.
6. The polling server gives the job to the worker task.
7. The worker task executes the job function stored in `pvTaskCode`.
8. The polling server waits for completion with a timeout based on residual capacity and soft deadline.
9. If the job completes in time, the polling server records completion.
10. If the job does not complete in time, the polling server applies the configured aperiodic policy.

---

## 3. Polling Server

### 1. Introduction

- This document describes the **Polling Server** module developed for the Periodic Task Layer (PTL).
- The polling server is a special periodic task that is activated at regular intervals to execute aperiodic (event-driven) jobs without violating the temporal constraints of hard real-time periodic tasks.
- It provides a **predictable and bounded** execution window for aperiodic work, exposing a configurable policy to handle soft deadline misses.
- All relevant events — job releases, completions, deadline overruns, kills, budget refills, and exhaustions — are logged via the trace module with tick-level precision.
- The entire module is conditionally compiled via `configUSE_POLLING_SERVER`: when the macro is set to `0` in `FreeRTOSConfig.h`, all types, functions, and internal state are excluded from the build.

### 2. Architecture Overview

The polling server is built on top of FreeRTOS using a **server-worker split**:

- The **server task** (`vPollingServer_Task`) is registered as a periodic task in the PTL configuration. At each activation it drains the aperiodic job queue within its budget.
- The **worker task** (`prvWorkerTask`) is an internal helper that physically executes each aperiodic job. It is controlled by the server through two binary semaphores.

This separation is necessary because the server must enforce time limits on running jobs. A job that exceeds its allowed time cannot be interrupted from within the same task; the server terminates the worker task and recreates it via `prvRecreateWorker()`, leaving the server itself unaffected.

#### 2.1 Synchronisation Primitives

Two binary semaphores coordinate the server and the worker:

- `xWorkerStartSem`: given by the server to signal the worker to start a new job.
- `xWorkerDoneSem`: given by the worker to signal the server that the job has completed.

The server waits on `xWorkerDoneSem` with a bounded timeout. If the timeout expires, the server interprets the situation as a deadline or budget violation and acts accordingly.

#### 2.2 Operating Flow

1. `xPollingServer_Init()` is called once in `main()` before the scheduler starts. It saves the configuration, creates the semaphores, and spawns the worker task.
2. At each periodic activation, the PTL calls `vPollingServer_Task()`.
3. The server logs a **server refill** event and starts draining the aperiodic job queue.
4. For each dequeued job, the server computes a timeout equal to the minimum between the job's soft deadline and the remaining budget, signals the worker, and waits.
5. Depending on the outcome, one of three scenarios applies (see Section 4.5.1).
6. When the queue is empty or the budget is exhausted, the server logs a **server exhaust** event (if applicable) and returns.

### 3. Files Documentation

#### 3.1 `PollingServer.h`

This header defines the public interface of the polling server, including the configuration structure, the policy enumeration, and the two functions that external code must call. The entire content (except the include guards) is conditionally compiled under `#if ( configUSE_POLLING_SERVER == 1 )`.

##### 3.1.1 `PSAperiodicPolicy_t`

This enumeration selects the policy applied when an aperiodic job exceeds its soft deadline while budget is still available.

| Value | Name | Description |
| --- | --- | --- |
| `PS_POLICY_OVERRUN` | Overrun | The job is allowed to continue. The deadline miss is logged and the remaining budget is granted as an extra completion window. |
| `PS_POLICY_KILL` | Kill | The job is terminated immediately on soft deadline miss. The worker is recreated and the server moves to the next queued job. |

Note: regardless of the configured policy, a budget exhaustion always forces a kill. See Section 4.5.1 for the full decision logic.

##### 3.1.2 `PSConfig_t`

This structure contains all parameters needed to initialise the polling server. It is passed once to `xPollingServer_Init()` and stored internally for the lifetime of the server. Field names follow the FreeRTOS naming convention: `ux` prefix for `UBaseType_t`, `ul` for `uint32_t`, `x` for enumerations.

| Type | Name | Description |
| --- | --- | --- |
| `UBaseType_t` | `uxPriority` | FreeRTOS priority assigned to both the server task and the worker task. |
| `uint32_t` | `ulStackSize` | Stack size in words, used for both the server and the worker task. |
| `uint32_t` | `ulPeriodMs` | Server activation period in milliseconds. Must match the period declared in the PTL configuration. |
| `uint32_t` | `ulCapacityMs` | Budget granted to the server at each activation, in milliseconds. |
| `PSAperiodicPolicy_t` | `xPolicy` | Policy applied when an aperiodic job misses its soft deadline but budget remains. |

##### 3.1.3 Exposed APIs

###### `xPollingServer_Init`

```c
BaseType_t xPollingServer_Init( const PSConfig_t *pxConfig );
```

- Initialises the polling server subsystem.
- Saves the configuration, creates the two binary semaphores, and spawns the worker task.
- Must be called once in `main()` before `vTaskStartScheduler()`.
- Returns `pdPASS` on success, `pdFAIL` if any resource allocation fails.

###### `vPollingServer_Task`

```c
void vPollingServer_Task( void *pvParameters );
```

- Body of the polling server, called at each periodic activation by the PTL.
- Drains the aperiodic job queue within the available budget and applies the configured policy on soft deadline miss.
- Must be registered as a periodic task in the PTL configuration with the same period and priority specified in `PSConfig_t`.
- The `pvParameters` argument is unused.

### 4. `PollingServer.c`

This source file contains the full implementation of the polling server. All code is enclosed in `#if ( configUSE_POLLING_SERVER == 1 )` so it is entirely excluded from the build when the polling server is disabled.

#### 4.1 Internal State

The module maintains the following static variables. Names follow the FreeRTOS convention: `x` prefix for handles and task-related types, `ul` for `uint32_t`.

| Variable | Type | Description |
| --- | --- | --- |
| `xWorkerHandle` | `TaskHandle_t` | Handle of the worker task currently executing aperiodic jobs. |
| `xWorkerStartSem` | `SemaphoreHandle_t` | Binary semaphore used by the server to signal the worker to start. |
| `xWorkerDoneSem` | `SemaphoreHandle_t` | Binary semaphore used by the worker to signal job completion to the server. |
| `xServerConfig` | `PSConfig_t` | Copy of the configuration passed at init time. |
| `xCurrentJob` | `AperiodicJob_t` | The job currently assigned to the worker. |
| `ulPsJobCount` | `uint32_t` | Activation counter used as job number in trace records. |

#### 4.2 `prvWorkerTask()` — Internal Worker

```c
static void prvWorkerTask(void *pvParams);
```

This is a private task (prefixed `prv` following FreeRTOS conventions) that runs in an infinite loop. It waits indefinitely on `xWorkerStartSem`, executes the function stored in `xCurrentJob.pvFunction`, and signals completion via `xWorkerDoneSem`. It never returns.

The worker is kept separate from the server so that it can be forcibly terminated (via `vTaskDelete`) when a timeout expires, without affecting the server task itself.

#### 4.3 `prvRecreateWorker()` — Internal Helper

```c
static BaseType_t prvRecreateWorker(void);
```

This private function terminates the current worker and creates a fresh replacement. It is called whenever a running job must be killed.

Implementation details:
- Calls `vTaskDelete(xWorkerHandle)` to terminate the old worker.
- Calls `taskYIELD()` to give the idle task a chance to free the deleted task's stack memory. Without this yield, repeated kill-and-create cycles would gradually exhaust the heap.
- Creates a new worker task with `xTaskCreate`, using the priority and stack size from `xServerConfig`.
- Drains both semaphores with non-blocking takes (`timeout = 0`) to discard any stale tokens left by the killed worker.
- Returns `pdPASS` on success, `pdFAIL` if the new task cannot be created.

#### 4.4 `xPollingServer_Init()`

Implementation details:
- Copies the `PSConfig_t` structure into the internal `xServerConfig` variable so the configuration remains available across all future activations.
- Creates `xWorkerStartSem` and `xWorkerDoneSem` as binary semaphores. Returns `pdFAIL` immediately if either allocation fails.
- Creates the worker task using `uxPriority` and `ulStackSize` from the configuration.

Behavioral note:
- This function must be called before the FreeRTOS scheduler starts. Calling it after `vTaskStartScheduler()` is undefined behavior.

#### 4.5 `vPollingServer_Task()`

This is the core of the polling server. It is activated periodically by the PTL and implements the full budget-based job dispatch loop.

Implementation details:
- Records the activation tick with `xTaskGetTickCount()` into `xActivationTime` and converts `ulCapacityMs` to ticks into `xCapacityTicks`. These establish the time reference for all budget calculations within the current activation.
- Increments `ulPsJobCount` and logs a `eEvtServerRefill` event.
- Enters a loop that repeats as long as budget remains and jobs are available in the queue.
- At each iteration, computes the remaining budget as `xCapacityTicks - xElapsed`, where `xElapsed` is the time elapsed since `xActivationTime`.
- Dequeues the next job with a non-blocking `xQueueReceive`. If the queue is empty, the loop exits silently.
- Computes the per-job timeout `xTimeout` as the minimum between the job's `xSoftDeadline` and `xRemaining`, to ensure that neither constraint is exceeded.
- Logs an `eEvtRelease` event and signals the worker via `xWorkerStartSem`.
- Waits on `xWorkerDoneSem` for `xTimeout` ticks.

##### 4.5.1 Execution Scenarios

The outcome of `xSemaphoreTake(xWorkerDoneSem, xTimeout)` determines which scenario applies:

**Scenario A — Job completed in time**

The semaphore is taken successfully before the timeout. The server logs `eEvtComplete` and proceeds to the next job.

**Scenario B — Budget exhausted**

The timeout expires and `xNowElapsed >= xCapacityTicks`. The budget hard limit has been reached. Regardless of the configured policy, the server logs `eEvtAperiodicKill` and `eEvtServerExhaust`, calls `prvRecreateWorker()` to terminate the running job, and exits the loop.

This override is mandatory: allowing a job to keep running after budget exhaustion would cause the server task to overrun its own period, violating the PTL's timing model and potentially starving higher-priority periodic tasks.

**Scenario C — Soft deadline missed, budget still available**

The timeout expires but `xNowElapsed < xCapacityTicks`. The configured policy is applied:

- **`PS_POLICY_KILL`**: the server logs `eEvtAperiodicKill` and calls `prvRecreateWorker()`. The loop continues with the next job.
- **`PS_POLICY_OVERRUN`**: the server logs `eEvtAperiodicOverrun` and grants the job a final completion window `xExtraTime = xCapacityTicks - xNowElapsed`. If the job completes within this window, `eEvtComplete` is logged. If it does not, the budget hard limit is reached and Scenario B applies: `eEvtServerExhaust`, `eEvtAperiodicKill`, `prvRecreateWorker()`, and loop exit.

##### 4.5.2 Why No Initial Semaphore Flush Is Needed

The server does not drain the semaphores at the start of each activation. This is safe because `prvRecreateWorker()` always flushes both semaphores after recreating the worker. Since every kill path goes through `prvRecreateWorker()`, no stale tokens can accumulate between activations.

#### 4.6 Trace Events Emitted

The following trace events are logged by the polling server module:

| Event | Condition |
| --- | --- |
| `eEvtServerRefill` | Logged at the start of every activation. |
| `eEvtRelease` | Logged when a job is dequeued and the worker is signalled. |
| `eEvtComplete` | Logged when the worker finishes within the allowed time. |
| `eEvtAperiodicOverrun` | Logged when a soft deadline is missed and `PS_POLICY_OVERRUN` is active. |
| `eEvtAperiodicKill` | Logged when a job is killed due to a soft deadline miss or budget exhaustion. |
| `eEvtServerExhaust` | Logged when the budget is exhausted, either mid-job or at loop exit. |

---

## 4. Trace and Monitoring System

### 1. Introduction
- This document describes the **Trace and Monitoring System** developed for the Periodic Task Layer (PTL) and the polling server infrastructure.
- The trace module is used for **test, validation, and debugging** purposes: it records scheduler-related events with **tick-level precision** so that the runtime behavior of the system can be inspected after execution.
- The trace output makes it possible to analyze **task releases, completions, deadline misses, overruns, polling server activity, and aperiodic task handling** in a deterministic and repeatable way.
- In addition to event logging, the monitoring component also provides **summary statistics** collected by the PTL and printed in a human-readable form.

### 2. Trace and Monitoring System
The trace and monitoring system is a lightweight logging layer built on top of FreeRTOS.

Its main goals are:
- to keep a **chronological record** of relevant scheduler events,
- to support **automated testing** by exposing event counters and final traces,
- to provide **post-mortem analysis** through UART dumps,
- and to store enough context to understand what happened without relying on live debugging.

#### 2.1 Logged Information
The assignment requires the trace module to collect information such as:
- task start and end ticks,
- deadline misses or forced terminations,
- CPU idle time,
- and other scheduler events useful for timing analysis.

In the current implementation, the trace module logs a richer event set, including:
- job release,
- job completion,
- deadline miss,
- overrun handling decisions for SKIP, KILL, and CATCH_UP,
- aperiodic overrun handling,
- aperiodic kill on soft deadline miss,
- polling server refill and exhaustion events.

#### 2.2 Design Principles
- **Tick-level precision**: every record stores the tick value returned by the scheduler at the moment of logging.
- **Chronological order**: events are stored in the order in which they happen.
- **Bounded memory usage**: the trace buffer is a fixed-size circular buffer.
- **Thread safety**: logging, buffer resets, index snapshots, and event counting are protected with FreeRTOS critical sections.
- **Defensive lookup**: event IDs and task types are resolved to strings through a range-checked lookup, so out-of-range identifiers cannot index the lookup tables out of bounds.
- **Post-run readability**: the trace can be dumped to UART in a tabular format.

#### 2.3 Operating Flow
The trace system is typically used in the following way:
1. The trace buffer is initialized with `vTraceInit()`.
2. PTL and polling-server code call `vTraceLog()` whenever a relevant event occurs.
3. At the end of the execution, or when the system is idle, `vTraceDump()` prints the full trace.
4. `vTraceStatsDump()` prints the per-task statistics returned by the PTL.
5. Automated tests can call `ulTraceGetEventCount()` to verify how many times a given event occurred.

### 3. Files Documentation

#### 3.1 `ptl_types.h`
This header defines the public statistics structure used by the PTL to expose execution results to the trace system and to the final reporting layer.

##### 3.1.1 `PTLTaskStats_t`
This data structure contains the summary statistics collected for one periodic task.

| Type | Name | Description |
| --- | --- | --- |
| `const char *` | `pcName` | Task name used for reporting and trace output. |
| `uint32_t` | `ulTotalJobs` | Total number of jobs released for the task. |
| `uint32_t` | `ulCompletedJobs` | Total number of completed jobs. |
| `uint32_t` | `ulDeadlineMisses` | Number of missed deadlines detected by the PTL. |
| `uint32_t` | `ulOverruns` | Number of period overruns detected at release time. |
| `uint32_t` | `ulAvgResponseTime` | Average response time measured in ticks. |
| `uint32_t` | `ulWorstCaseResponse` | Worst-case response time measured in ticks. |
| `TickType_t` | `xPeriodTicks` | Period of the task expressed in ticks. |

##### 3.1.2 Purpose of the structure
- The PTL fills this structure when task execution ends or when statistics are requested.
- The trace module prints these values in a dedicated statistics dump.
- The structure is intentionally compact so it can be copied and reported efficiently.

#### 3.2 `trace.h`
This header defines the public interface of the trace subsystem together with the data types used to classify recorded events.

##### 3.2.1 `TRACE_BUFFER_SIZE`
```c
#define TRACE_BUFFER_SIZE ( 512U )
```
- Defines the number of trace records stored in memory.
- The buffer behaves as a **circular buffer**, so old records are overwritten when the capacity is exceeded.
- The fixed size keeps the implementation deterministic and suitable for embedded systems.

##### 3.2.2 `TraceTaskType_t`
This enumeration identifies the kind of entity that generated the trace event.

| Value | Name | Description |
| --- | --- | --- |
| `eTracePeriodic` | Periodic task | Event generated by a periodic PTL task. |
| `eTraceAperiodic` | Aperiodic task | Event generated by an aperiodic job managed by the polling server. |
| `eTracePollingServer` | Polling server | Event generated by the polling server itself. |

##### 3.2.3 `TraceEventID_t`
This enumeration classifies the trace event being recorded.

| Value | Name | Description |
| --- | --- | --- |
| `eEvtRelease` | Release | A new job has been released. |
| `eEvtComplete` | Complete | A job completed successfully. |
| `eEvtDeadlineMiss` | Deadline miss | A job completed after its absolute deadline. |
| `eEvtOverrunSkip` | Overrun skip | Overrun handled with the SKIP policy. |
| `eEvtOverrunKill` | Overrun kill | Overrun handled with the KILL policy. |
| `eEvtOverrunCatchup` | Overrun catch-up | Overrun handled with the CATCH_UP policy. |
| `eEvtAperiodicOverrun` | Aperiodic overrun | Soft deadline exceeded, but the job is allowed to continue. |
| `eEvtAperiodicKill` | Aperiodic kill | Aperiodic job terminated on soft deadline miss. |
| `eEvtServerRefill` | Server refill | Polling server budget replenished at the beginning of a new period. |
| `eEvtServerExhaust` | Server exhaust | Polling server budget exhausted before the end of the period. |

##### 3.2.4 `TraceRecord_t`
This structure stores one complete trace entry.

| Type | Name | Description |
| --- | --- | --- |
| `TickType_t` | `xTimestamp` | Tick value captured when the event is logged. |
| `char[configMAX_TASK_NAME_LEN]` | `pcTaskName` | Defensive copy of the task or server name. |
| `TraceTaskType_t` | `xTaskType` | Type of entity that generated the event. |
| `uint32_t` | `ulJobNumber` | Current job iteration of the task. |
| `TraceEventID_t` | `xEventID` | Event identifier. |
| `uint32_t` | `ulData` | Additional numerical payload associated with the event. |

##### 3.2.5 Why the task name is copied
- The module does not store a raw pointer to the task name because the pointed-to memory may disappear or become invalid when the trace is dumped later.
- Copying the name into the record avoids dangling-pointer issues and makes the buffer self-contained.

##### 3.2.6 Exposed APIs

###### `vTraceInit`
```c
void vTraceInit(void);
```
- Initializes the trace subsystem.
- Clears the circular buffer and resets internal indices.
- Should be called before the PTL starts generating events.

###### `vTraceLog`
```c
void vTraceLog(const char *pcTaskName,
               uint32_t ulJobNumber,
               TraceTaskType_t xType,
               TraceEventID_t xEvent,
               uint32_t ulData);
```
- Stores one event inside the trace buffer.
- Captures the current tick count and records the event metadata.
- Must not be called from an ISR.

###### `vTraceStatsDump`
```c
void vTraceStatsDump(const PTLTaskStats_t *pxStats, UBaseType_t uxNumTasks);
```
- Parameters:
    - `pxStats`: array filled by `uxPtlGetStats()` and containing the per-task statistics to print.
    - `uxNumTasks`: number of valid entries stored in `pxStats`.
- Prints the statistics collected by the PTL.
- Produces a table showing total jobs, completed jobs, deadline misses, deadline-miss percentage, overruns, average response time, and worst-case response time.
- Returns no value.

###### `vTraceDump`
```c
void vTraceDump(void);
```
- Prints the full trace buffer in chronological order.
- Intended for post-execution analysis or when the system is otherwise idle.

###### `ulTraceGetEventCount`
```c
uint32_t ulTraceGetEventCount(TraceEventID_t xEvent);
```
- Parameters:
    - `xEvent`: trace event identifier to count in the current buffer.
- Returns the number of times the requested event appears in the current trace buffer.
- Useful for automated tests and assertions.

#### 3.3 `trace.c`
This source file contains the implementation of the trace and monitoring system.

##### 3.3.1 Internal data structures
The implementation uses the following internal state:
- `xTraceBuffer[TRACE_BUFFER_SIZE]`: fixed-size circular buffer that stores all trace records.
- `ulHead`: index of the next slot to be written (declared `volatile`, since it is shared across execution contexts).
- `ulTotalWritten`: total number of records written since initialization (declared `volatile`, since it is shared across execution contexts).

##### 3.3.2 Event name tables
The implementation keeps two constant lookup tables:
- one for event names,
- one for trace entity types.

These arrays are used when printing trace records so that the dump is readable and does not expose raw numeric identifiers.

##### 3.3.3 `vTraceInit()`
Implementation details:
- Enters a FreeRTOS critical section.
- Clears the whole trace buffer with `memset`.
- Resets `ulHead` and `ulTotalWritten` to zero.
- Prints a confirmation message on UART.

Behavioral note:
- The function is deterministic and leaves the module in a clean state for a new run.

##### 3.3.4 `vTraceLog()`
Implementation details:
- Reads the current tick count before entering the critical section to keep the protected region short.
- Copies the task name into the selected buffer slot with bounded string handling.
- Stores the type, job number, event ID, and optional payload.
- Advances the circular-buffer head with wrap-around.
- Increments the total record counter.

Behavioral note:
- The critical section prevents concurrent writers from corrupting the same buffer slot.
- The fixed buffer makes the module suitable for embedded test runs where dynamic allocation is undesirable.

##### 3.3.5 `vTraceDump()`
Implementation details:
- Takes an atomic snapshot of the buffer indices inside a critical section.
- Computes how many valid records are available and where the oldest record is located.
- Prints a header and then iterates through the valid portion of the buffer in chronological order.
- Resolves event IDs and task types into human-readable strings through a bounded, range-checked lookup: identifiers outside the known range fall back to `"unknown event"` / `"unknown type"` instead of indexing the lookup tables out of bounds.
- Formats the output into aligned columns before sending it to UART.

Behavioral note:
- The function is intentionally expensive and should be used only after execution or during idle periods.

##### 3.3.6 `vTraceStatsDump()`
Implementation details:
- Checks for empty or invalid input and prints a diagnostic message if no data is available.
- Prints a compact table with per-task statistics.
- Computes the deadline-miss percentage from total jobs and missed deadlines.
- Formats the output for easy comparison across test runs.

Behavioral note:
- This function provides the monitoring part of the system, complementing the event trace with aggregate results.

##### 3.3.7 `ulTraceGetEventCount()`
Implementation details:
- Computes how many entries are currently valid in the buffer.
- Scans the valid portion of the trace inside a FreeRTOS critical section and counts matches for the requested event ID.
- Returns the final count to the caller.

Behavioral note:
- This function is especially useful in automated validation, because it turns trace data into a compact test oracle.

---

## 5. Configurator and Automated Test Runner

### 1. Introduction
- This document describes the **MDE Configurator & Runner**, an automated configuration and testing framework developed in Python.
- The tool acts as a high-level interface to configure the RTOS architecture, generate the `main.c` file automatically, and seamlessly run simulations inside the QEMU emulator.
- It provides functionality to calculate CPU utilization, configure periodic tasks, aperiodic tasks, and the polling server interactively or via a configuration file (`config.json`).
- Additionally, it implements an automated test suite runner that executes a predefined set of 21 tests, parses their output, and collects results in a unified report.

### 2. Configurator Architecture
The configurator is a standalone Python script (`configurator.py`) that interacts with the C codebase. 

Its main features include:
- **Interactive Menu**: A user-friendly CLI to define task parameters.
- **JSON Configuration Support**: Loads and saves architectures from/to `config.json` for persistence.
- **Schedulability Analysis**: Computes CPU utilization to prevent hardware overload before compilation.
- **Code Generation**: Translates high-level specifications into a functional `main.c` file.
- **Automated Validation**: Runs the entire test suite, intercepts QEMU UART outputs, and evaluates pass/fail verdicts.

#### 2.1 Supported Configurations
The configurator allows defining:
- **Periodic Tasks**: Specified by name, period, compute time, deadline, and phase offset.
- **Polling Server**: Optional component configured with a period and capacity budget.
- **Aperiodic Tasks**: Specified by name, compute time, soft deadline, and priority. Can be injected as a block at startup or randomly at runtime.
- **Global Overrun Policy**: Choice between SKIP, KILL, and CATCH_UP policies.

### 3. Tool Functionalities

#### 3.1 Code Generation (`main.c`)
When an architecture is defined, the `create_main_c()` function constructs the C code:
- **Headers**: Includes FreeRTOS and PTL dependencies.
- **Task Bodies**: Generates task bodies with busy-wait loops (`xTaskGetTickCount`) mimicking the compute time.
- **Aperiodic Job Injection**: Depending on user choice, jobs are either pre-submitted via `xAperiodicSubmitJob` directly in `main`, or a `vRandomSpawnerTask` is generated to inject them dynamically using `rand()`.
- **Initialization**: Automatically invokes `xPtlInit()`, `vAperiodicInit()`, and `xPollingServer_Init()`, then starts the scheduler.

#### 3.2 Schedulability Analysis
Before generating the code, the tool performs a two-step mathematical validation to ensure the architecture is feasible under the **Rate Monotonic (RM)** algorithm.

1. **Hardware Overload Check (Necessary Condition)**:
   Computes the global CPU utilization $U$:
   $$ U = \sum \frac{C_i}{T_i} + \frac{C_{ps}}{T_{ps}} $$
   If $U > 100\%$, the configurator aborts the generation, warning the user about the impossible hardware constraints.

2. **Hyperbolic Bound Check (Sufficient Condition)**:
   This check is executed **only if all tasks have Implicit Deadlines** ($D_i = T_i$). If any task has a constrained deadline ($D_i < T_i$), the tool skips this check and warns that a full Response Time Analysis (RTA) is required.
   For implicit deadline sets, the script applies the **Hyperbolic Bound** for mixed task sets (Periodic + Polling Server). The system is mathematically guaranteed to be schedulable if:
   $$ \prod_{i=1}^{n} (U_i + 1) \le \frac{2}{U_{ps} + 1} $$
   If the bound is violated (or if RTA is needed), the user is warned but can choose to proceed anyway, as the bound is sufficient but not strictly necessary for specific phase alignments.

#### 3.3 Test Suite Runner
The `run_self_assessment_test()` function automates the execution of the 21 test scenarios:
1. Backups the current `main.c`.
2. Iterates over `Tests/test1.c` to `Tests/test21.c`.
3. Compiles the kernel for each test using `make`.
4. Spawns QEMU in a subprocess with a timeout.
5. Captures UART output, looking for `[VERDICT] PASS` or `[VERDICT] FAIL`.
6. Aggregates all traces into `Test_Suite_Report.txt`.
7. Restores the original `main.c`.

### 4. File Structure Integration

#### 4.1 `configurator.py`
The main Python script. Relies on standard libraries (`json`, `sys`, `os`, `subprocess`, `time`, `shutil`) to ensure maximum portability across operating systems without requiring external dependencies like `pip`.

#### 4.2 `config.json`
A lightweight configuration file generated and read by the configurator. 
- Stores the global policy, an array of periodic tasks, and the polling server configuration (including aperiodic tasks and the random injection flag).
- Ensures that complex test scenarios can be reproduced without manual input.

#### 4.3 `Test_Suite_Report.txt`
An auto-generated artifact created when running the test suite. 
- Contains the complete UART dumps of all executed tests.
- Used for deep post-mortem analysis of failed test cases without re-running the entire suite.

#### 4.4 `Simulation_Report.txt`
An auto-generated artifact created when running a custom simulation (Option 1 or Option 2).
- Contains the captured UART dump of the custom task set execution.
- Includes the full chronological Trace Log (`vTraceDump`) and the complete Task Statistics Table (`vTraceStatsDump`).
- Generated automatically when the user terminates the simulation with `Ctrl+C` (which sends a termination byte `q` over UART, triggering the high-priority `vReporterTask` to print the final reports before exiting).

### 5. User Manual / How to Use

To launch the MDE Configurator & Runner, run the script from your terminal:
```bash
python3 configurator.py
```
*(Make sure to execute this from the `group9/App` directory)*

Once launched, the interactive CLI menu presents **3 options**:

---

#### Option 1: Load from `config.json`
- Loads a pre-defined architecture configuration directly from [config.json](file:group9/App/config.json).
- Automatically calculates CPU utilization, runs the Rate Monotonic hyperbolic bound checks, generates the updated `main.c`, compiles, and launches the QEMU emulator.
- The simulation runs live in the console; press **`Ctrl+C`** at any time to stop it and generate `Simulation_Report.txt`.

---

#### Option 2: Create Tasks Manually
Allows you to define a brand-new RTOS architecture interactively through step-by-step CLI prompts:
1. **Periodic Tasks Setup:** 
   - Input names, periods, compute times, relative deadlines (defaults to Period), and phase offsets (defaults to 0).
   - Enter `q` to stop adding periodic tasks.
2. **Choose Global Overrun Policy:**
   - **SKIP (1):** Skips new activations until the current overrunning job finishes.
   - **KILL (2):** Kills the overrunning job at the period boundary and starts a fresh instance.
   - **CATCH_UP (3):** Runs jobs sequentially, logging deadline misses but keeping the pace.
3. **Optional Polling Server (PS):**
   - Choose whether to enable a Polling Server.
   - If enabled, input the server's period and capacity budget.
   - Add any associated Aperiodic Tasks (specify name, compute time, soft deadline, and priority).
   - Decide whether to inject them randomly at runtime (creates a background spawner task using `rand()`) or to submit them at boot time.
4. **Validation and Execution:** The tool performs the schedulability checks. If utilization is $\le 100\%$, it saves the inputs to `config.json`, constructs the new `main.c`, compiles it, and starts QEMU automatically. Press **`Ctrl+C`** to stop it and generate `Simulation_Report.txt`.

---

#### Option 3: Run Test Suite (1-21)
- Automates the compilation and QEMU-based verification of the 21 regression test scenarios.
- The script automatically handles:
  1. Backing up the active `main.c`.
  2. Copying each test source file from `Tests/` into `main.c`.
  3. Re-compiling the project with `make clean && make all`.
  4. Executing QEMU with a 15-second timeout safeguard.
  5. Parsing the serial stdout for `[VERDICT] PASS`/`FAIL`.
  6. Aggregating all output logs into `Test_Suite_Report.txt` and restoring your original `main.c`.

---

## 6. Test Suite

This document describes each system test (test1.c ... test21.c) verifying real-time scheduling, deadline miss handling, overrun management, queueing, and stress scenarios.

### Test Index

| Test File       | Scenario Tested                                                      |
|-----------------|---------------------------------------------------------------------|
| test1.c         | Normal periodic execution                                           |
| test2.c         | Deadline miss scenario                                              |
| test3.c         | Period overrun - SKIP policy                                        |
| test4.c         | Period overrun - KILL policy                                        |
| test5.c         | Period overrun - CATCH-UP policy                                    |
| test6.c         | Aperiodic tasks (Polling Server) normal execution                   |
| test7.c         | Polling Server: Overrun on soft deadline miss                       |
| test8.c         | Polling Server: Kill on soft deadline miss                          |
| test9.c         | Periodic + aperiodic combo execution                                |
| test10.c        | Periodic tasks with phase offsets                                   |
| test11.c        | Multiple periodic: mixed deadline miss/normally completing          |
| test12.c        | Polling Server: Overrun/Kill with budget exhaustion                 |
| test13.c        | Stress test w/ concurrent overruns: SKIP/KILL/CATCH-UP             |
| test14.c        | Minimal phase gap edge case                                         |
| test15.c        | Preemption and timing consistency                                   |
| test16.c        | High-frequency timing stability                                     |
| test17.c        | Polling Server backlog: overruns/exhaustion stress                  |
| test18.c        | Polling Server: max aperiodic queue w/ KILL policy                  |
| test19.c        | Round-robin scheduling at equal priority                            |
| test20.c        | Maximum system tasks (PTL_MAX_TASKS=10) and overhead limit          |
| test21.c        | Aperiodic queue full: rejection + correct job processing            |

---

### Test Descriptions

#### test1.c: Normal periodic execution
- **Scenario:** 2 tasks, always finish within period and deadline. No errors expected.
- **Passes if:** 0 deadline misses, 0 overruns.

---

#### test2.c: Deadline miss scenario
- **Scenario:** Single task, 3rd activation takes longer than deadline (causes single miss), otherwise normal.
- **Passes if:** 1 deadline miss, 0 overruns.

---

#### test3.c: Period overrun with SKIP policy
- **Scenario:** Period overrun; job on 3rd release is skipped (not run) as previous activation overruns next period.
- **Passes if:** 1 deadline miss, 2 overruns, 13 completions.

---

#### test4.c: Period overrun with KILL policy
- **Scenario:** Overrunning task instance is killed and replaced on period boundary.
- **Passes if:** 1 overrun, 0 misses, 14 completions.

---

#### test5.c: Period overrun with CATCH-UP policy
- **Scenario:** Overrun results in missed period, all jobs eventually run ("catch-up").
- **Passes if:** 1 deadline miss, 1 overrun, 15 completions.

---

#### test6.c: Aperiodic tasks - normal execution (Polling Server)
- **Scenario:** Polling server releases, two aperiodic jobs both finish within soft deadline & budget.
- **Passes if:** 0 aperiodic errors (misses, kills, overruns, budget exhausts).

---

#### test7.c: Polling Server - OVERRUN on soft deadline miss
- **Scenario:** Job overruns soft deadline; allowed to complete.
- **Passes if:** 1 aperiodic overrun, 0 kills, server is stable.

---

#### test8.c: Polling Server - KILL on soft deadline miss
- **Scenario:** Overrun job is killed at deadline, next job executes normally.
- **Passes if:** 1 aperiodic kill, 0 overruns, 0 server errors.

---

#### test9.c: Periodic + Aperiodic combo
- **Scenario:** Periodic and polling server run simultaneously, all jobs complete successfully, no errors.
- **Passes if:** 0 errors, 15 periodic and 2 aperiodic completions.

---

#### test10.c: Periodic tasks with phase offsets
- **Scenario:** 2 tasks, same period, offset phases. Verifies staggered starts.
- **Passes if:** 10 completions per task, 0 errors.

---

#### test11.c: Multiple periodic tasks, mix of deadline miss and success
- **Scenario:** TaskA intentionally misses a deadline. TaskB always meets its deadline.
- **Passes if:** TaskA: 1 miss/15 completions. TaskB: 0 misses/10 completions.

---

#### test12.c: Polling server, Overrun and Kill with budget exhaustion
- **Scenario:** Three aperiodic jobs, each overruns deadline; budget exhaustion triggers kill for last job.
- **Passes if:** 3 overruns, 1 kill, 1 server exhaust event.

---

#### test13.c: Stress test with SKIP/KILL/CATCH-UP
- **Scenario:** 3 tasks, each with different overrun policy, all overrun simultaneously.
- **Passes if:** Each policy shows expected trace, all jobs complete multiples, at least 1 miss/overrun each (0 misses for KILL policy).

---

#### test14.c: Minimal phase gap edge case
- **Scenario:** 3 tasks, 1ms phase separation. Verifies support for minimal possible offset.
- **Passes if:** 10 completions each, 0 misses and 0 overruns.

---

#### test15.c: Preemption and timing consistency
- **Scenario:** High-priority jobs preempt low-priority jobs, both always meet deadlines.
- **Passes if:** High: 30 completions/0 errors, Low: 10 completions/0 errors.

---

#### test16.c: High-frequency timing stability
- **Scenario:** Three tightly-scheduled tasks, 130 total activations in 3s. Should all complete on time.
- **Passes if:** All jobs complete, 0 deadline misses/overruns.

---

#### test17.c: Polling Server stress - backlog + budget exhaustion
- **Scenario:** Six jobs submitted immediately. Some overrun, some killed, only some complete.
- **Passes if:** 2 overruns, 2 killed, 4 completions, 2 server exhaust events.

---

#### test18.c: Polling Server - max queue, repeated KILL
- **Scenario:** Fill queue with alternating long/short jobs, inducing maximum number of "kills"; intentional corner-case job S3.
- **Passes if:** 6 kills, 4 completions, 1 server exhaust, 0 overruns.

---

#### test19.c: Round-robin at equal priority
- **Scenario:** All tasks at identical priorities—should observe time-slicing and parallel completions.
- **Passes if:** 20 completions each, 0 errors, worst-case response time ≤ 35ms for all.

---

#### test20.c: Maximum tasks (PTL_MAX_TASKS=10) + scheduler overhead
- **Scenario:** Schedules exactly 10 tasks at CPU utilization = 71.4%. Checks for off-by-one errors and verifies per-task scheduler overhead ≤ 10%.
- **Passes if:** Each of 10 tasks completed 20 times with 0 errors, overhead ≤ 10%.

---

#### test21.c: Aperiodic queue full - graceful rejection
- **Scenario:** More jobs submitted than maximum queue length; rejected jobs must not cause crashes or corruption.
- **Passes if:** 10 jobs accepted, 2 rejected, 9 completed, 1 killed at budget boundary.

---
