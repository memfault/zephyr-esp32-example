//! @file
//!
//! @brief Central Memfault heartbeat metrics collection point.
//!
//! The Memfault SDK calls memfault_metrics_heartbeat_collect_data() once per
//! heartbeat period.  This single implementation delegates to every module
//! that exports a *_metrics_collect() helper, guarded by the module's own
//! CONFIG option so the linker eliminates unused code automatically.

#include <zephyr/logging/log.h>

#include "memfault/components.h"

LOG_MODULE_REGISTER(heartbeat_metrics, LOG_LEVEL_DBG);

// Forward declarations – each module provides a collector when enabled.
// The declarations are guarded so this file carries no build dependency on
// disabled modules.
#if defined(CONFIG_WIFI_WATCHDOG_ENABLE)
extern void wifi_watchdog_metrics_collect(void);
#endif

#if defined(CONFIG_SNTP_TIME_SYNC_ENABLE)
extern void sntp_time_sync_metrics_collect(void);
#endif

void memfault_metrics_heartbeat_collect_data(void) {
#if defined(CONFIG_WIFI_WATCHDOG_ENABLE)
  wifi_watchdog_metrics_collect();
#endif

#if defined(CONFIG_SNTP_TIME_SYNC_ENABLE)
  sntp_time_sync_metrics_collect();
#endif
}
