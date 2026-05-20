/*
 * mem_monitor.c — Periodic thread-stack watermark reporter for ZMK / Zephyr
 *
 * Compiled only when CONFIG_ZMK_USB_LOGGING=y (see CMakeLists.txt).
 * Output is visible on the CDC-ACM serial port that ZMK opens for logging.
 *
 * Note: z_malloc_heap is a static symbol in this Zephyr version and cannot
 * be accessed externally.  Stack watermarks alone are sufficient to diagnose
 * which thread is overflowing and causing the keyboard to freeze.
 *
 * Required Kconfig (set in charybdis_right.conf):
 *   CONFIG_THREAD_MONITOR=y
 *   CONFIG_THREAD_NAME=y
 *   CONFIG_THREAD_STACK_INFO=y
 *   CONFIG_INIT_STACKS=y
 *   CONFIG_STACK_USAGE=y
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mem_monitor, LOG_LEVEL_INF);

/* ── Tunables ──────────────────────────────────────────────── */
/* First report fires this many ms after boot (let BLE settle). */
#define MEM_MONITOR_INITIAL_DELAY_MS   10000U
/* Subsequent reports every N ms. */
#define MEM_MONITOR_INTERVAL_MS         5000U

/* ── Work item ─────────────────────────────────────────────── */
static void mem_monitor_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(mem_monitor_work, mem_monitor_work_handler);

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
        size_t total = t->stack_info.size;
        size_t used  = total - unused_bytes;
        unsigned int pct = (unsigned int)((used * 100U) / total);

        /* Flag threads that have consumed > 80 % of their stack. */
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
    LOG_INF("--- Thread stacks (used / total) ---");
    k_thread_foreach(report_thread_cb, NULL);
    LOG_INF("====================================");

    k_work_reschedule(&mem_monitor_work, K_MSEC(MEM_MONITOR_INTERVAL_MS));
}

/* ── SYS_INIT entry point ──────────────────────────────────── */
static int mem_monitor_init(void)
{
    k_work_schedule(&mem_monitor_work, K_MSEC(MEM_MONITOR_INITIAL_DELAY_MS));
    return 0;
}

/*
 * APPLICATION priority 99 — runs after BLE/USB initialisers.
 */
SYS_INIT(mem_monitor_init, APPLICATION, 99);
