#include "timer.h"
#include "io.h"
#include "pic.h"
#include "../kernel/irq.h"
#include "../kernel/task.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182

static volatile uint32_t ticks = 0;

static void timer_irq() {
    ticks++;
}

void timer_init() {
    uint32_t divisor = PIT_BASE_HZ / TIMER_HZ;
    outb(PIT_COMMAND, 0x36);                 /* ch0, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, timer_irq);
    pic_unmask_irq(0);
}

uint32_t timer_ticks() { return ticks; }

uint32_t timer_uptime_ms() { return ticks * TIMER_MS_PER_TICK; }

void timer_sleep_ms(uint32_t ms) {
    /* +1 because we may start part-way through a tick */
    uint32_t wait = (ms + TIMER_MS_PER_TICK - 1) / TIMER_MS_PER_TICK + 1;
    if (task_scheduler_active()) {
        task_sleep_ticks(wait);
        return;
    }
    uint32_t start = ticks;
    while ((int32_t)(ticks - start) < (int32_t)wait) {
        asm volatile("sti; hlt");            /* sti shadow: no lost wake-up */
    }
}
