/*
 * mem_monitor.c — Periodic heap & thread-stack reporter for ZMK / Zephyr
 *
 * Compiled only when CONFIG_ZMK_USB_LOGGING=y (see CMakeLists.txt).
 * Output is visible on the CDC-ACM serial port that ZMK opens for logging.
 *
 * Required Kconfig (already set in charybdis_right.conf):
 *   CONFIG_SYS_HEAP_RUNTIME_STATS=y
 *   CONFIG_THREAD_MONITOR=y
 *   CONFIG_THREAD_NAME=y
 *   CONFIG_THREAD_STACK_INFO=y
 *   CONFIG_INIT_STACKS=y
 *   CONFIG_STACK_USAGE=y
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

LOG_MODULE_REGISTER(mem_monitor, LOG_LEVEL_INF);

/* ── Tunables ──────────────────────────────────────────────── */
/* First report fires this many ms after boot (let BLE settle). */
#define MEM_MONITOR_INITIAL_DELAY_MS   10000U
/* Subsequent reports every N ms.  30 s is unobtrusive. */
#define MEM_MONITOR_INTERVAL_MS        5000U

/* ── Zephyr internal malloc heap ───────────────────────────── */
/*
 * z_malloc_heap is the backing store for k_malloc() / CONFIG_HEAP_MEM_POOL_SIZE.
 * It is declared in kernel/lib/alloca.c; we extern it here rather than
 * pulling in a private header.
 */
extern struct sys_heap z_malloc_heap;

/* ── Work item ─────────────────────────────────────────────── */
static void mem_monitor_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(mem_monitor_work, mem_monitor_work_handler);

/* ── Heap report ───────────────────────────────────────────── */
static void report_heap(void)
{
    struct sys_memory_stats stats;
    int rc = sys_heap_runtime_stats_get(&z_malloc_heap, &stats);

    if (rc == 0) {
        LOG_INF("HEAP  allocated=%6u B  free=%6u B  peak_allocated=%6u B",
                (unsigned int)stats.allocated_bytes,
                (unsigned int)stats.free_bytes,
                (unsigned int)stats.max_allocated_bytes);
    } else {
        LOG_WRN("HEAP  stats unavailable (err %d)", rc);
    }
}

/* ── Per-thread stack report ───────────────────────────────── */
static void report_thread_cb(const struct k_thread *t, void *unused_arg)
{
    ARG_UNUSED(unused_arg);

    size_t unused_bytes = 0;
    int rc = k_thread_stack_space_get((struct k_thread *)t, &unused_bytes);

    const char *name = k_thread_name_get((struct k_thread *)t);
    if (name == NULL || name[0] == '\0') {
        name = "(unnamed)";
    }

    if (rc == 0) {
        size_t total  = t->stack_info.size;
        size_t used   = total - unused_bytes;
        unsigned int pct = (unsigned int)((used * 100U) / total);

        /* Flag threads that have used > 80 % of their stack. */
        if (pct >= 80U) {
            LOG_WRN("STACK [!!] %-24s used=%4u / %4u B  (%3u %%)",
                    name, (unsigned int)used, (unsigned int)total, pct);
        } else {
            LOG_INF("STACK      %-24s used=%4u / %4u B  (%3u %%)",
                    name, (unsigned int)used, (unsigned int)total, pct);
        }
    } else {
        LOG_INF("STACK      %-24s (info unavailable, err %d)", name, rc);
    }
}

/* ── Main work handler ─────────────────────────────────────── */
static void mem_monitor_work_handler(struct k_work *work)
{
    LOG_INF("========== Memory Report ==========");
    report_heap();
    LOG_INF("--- Thread stacks ---");
    k_thread_foreach(report_thread_cb, NULL);
    LOG_INF("===================================");

    /* Reschedule for the next interval. */
    k_work_reschedule(&mem_monitor_work, K_MSEC(MEM_MONITOR_INTERVAL_MS));
}

/* ── SYS_INIT entry point ──────────────────────────────────── */
static int mem_monitor_init(void)
{
    k_work_schedule(&mem_monitor_work, K_MSEC(MEM_MONITOR_INITIAL_DELAY_MS));
    return 0;
}

/*
 * APPLICATION priority so BLE / USB stacks are already initialised.
 * Priority 99 runs after all other APPLICATION initialisers.
 */
SYS_INIT(mem_monitor_init, APPLICATION, 99);
