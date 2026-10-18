#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "ptl_config.h"
#include "ptl_scheduler.h"


static void prvTask1Body(void *pvArgs) {
  (void)pvArgs;
  vUartPrintf("[RTOS] >> Task1 wake up.\n");
  TickType_t xStart = xTaskGetTickCount();
  while((xTaskGetTickCount() - xStart) < pdMS_TO_TICKS(50UL)) {}
}

static void prvTask2Body(void *pvArgs) {
  (void)pvArgs;
  vUartPrintf("[RTOS] >> Task2 wake up.\n");
  TickType_t xStart = xTaskGetTickCount();
  while((xTaskGetTickCount() - xStart) < pdMS_TO_TICKS(49UL)) {}
}


static PTLTaskConfig_t xTasks[2] = {
  {"Task1", prvTask1Body, NULL, 512U, tskIDLE_PRIORITY+10, 100U, 100U, 0U, eTracePeriodic, ePolicySkip},
  {"Task2", prvTask2Body, NULL, 512U, tskIDLE_PRIORITY+10, 100U, 100U, 0U, eTracePeriodic, ePolicySkip},
};

static PTLSchedulerConfig_t xAppConfig = {
  .xPolicy = ePolicySkip,
  .xTraceEnabled = pdTRUE,
  .ucNumTasks = 2
};

int main(void) {
  vUartInit();
  vUartPrintf("\n=== MDE RTOS Auto-Generated ===\n\n");
  for(uint8_t ucIdx=0; ucIdx<2; ucIdx++) xAppConfig.xTasks[ucIdx] = xTasks[ucIdx];
  if (xPtlInit(&xAppConfig) != pdPASS) for(;;);
  vPtlStart();
  vTaskStartScheduler();
  for(;;);
}
