#include "panic.h"
#include "crash.h"

void panic(const char *message)
{
    rsod_show("KERNEL PANIC", message, 0);
}
