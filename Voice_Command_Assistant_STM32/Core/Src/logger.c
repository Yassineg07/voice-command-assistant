#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include "cmsis_os.h"

extern osMessageQueueId_t LogQueueHandle;

void printl(const char *fmt, ...)
{
    char buffer[128];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    osMessageQueuePut(LogQueueHandle, buffer, 0, 10);
}
