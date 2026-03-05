//! @file
//!
//! @brief WiFi watchdog module that monitors WiFi connectivity and reboots
//! if a known network is available but not connected.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

#include "memfault/components.h"

LOG_MODULE_REGISTER(wifi_watchdog, LOG_LEVEL_DBG);

#if defined(CONFIG_WIFI_WATCHDOG_ENABLE)

// Track the maximum number of WiFi scan results per heartbeat
static uint32_t s_scan_result_count_max = 0;
static uint32_t s_last_scan_result_count = 0;

// WiFi state tracking
static bool s_wifi_connected = false;
static bool s_wifi_scanning = false;
static uint64_t s_last_wifi_ok_timestamp_ms = 0;

// Scan result buffer - store SSIDs of APs seen during the most recent scan
#define MAX_SCAN_RESULTS 32
static char s_scanned_ssids[MAX_SCAN_RESULTS][WIFI_SSID_MAX_LEN + 1];
static uint8_t s_scanned_ssid_lens[MAX_SCAN_RESULTS];
static uint8_t s_scan_result_count = 0;

// Network management event handler
static struct net_mgmt_event_callback s_wifi_mgmt_cb;
static struct k_work s_scan_work;

// Forward declarations
static void prv_wifi_scan_work_handler(struct k_work* work);

// Callback context for checking if any stored credential matches scan results
typedef struct {
  bool found;
} sCredScanMatchCtx;

static void prv_check_cred_ssid_cb(void* cb_arg, const char* ssid,
                                   size_t ssid_len) {
  sCredScanMatchCtx* ctx = (sCredScanMatchCtx*)cb_arg;
  if (ctx->found) {
    return;  // Already found a match
  }
  for (int i = 0; i < s_scan_result_count; i++) {
    if (s_scanned_ssid_lens[i] == ssid_len &&
        memcmp(s_scanned_ssids[i], ssid, ssid_len) == 0) {
      LOG_DBG("Scan result matches stored credential SSID: %.*s", (int)ssid_len,
              ssid);
      ctx->found = true;
      return;
    }
  }
}

// Check if scan results contain any network matching stored credentials
static bool prv_scan_contains_known_network(void) {
  if (s_scan_result_count == 0) {
    return false;
  }
  sCredScanMatchCtx ctx = {.found = false};
  wifi_credentials_for_each_ssid(prv_check_cred_ssid_cb, &ctx);
  return ctx.found;
}

// Handle a single scan result event (cb->info points to wifi_scan_result)
static void prv_handle_scan_result(struct net_mgmt_event_callback* cb) {
  const struct wifi_scan_result* entry =
      (const struct wifi_scan_result*)cb->info;

  if (s_scan_result_count < MAX_SCAN_RESULTS) {
    size_t ssid_len = MIN(entry->ssid_length, WIFI_SSID_MAX_LEN);
    memcpy(s_scanned_ssids[s_scan_result_count], entry->ssid, ssid_len);
    s_scanned_ssids[s_scan_result_count][ssid_len] = '\0';
    s_scanned_ssid_lens[s_scan_result_count] = ssid_len;
    s_scan_result_count++;
  }
}

// Handle scan-done event: finalize count and update metric
static void prv_handle_scan_done(void) {
  s_wifi_scanning = false;
  s_last_scan_result_count = s_scan_result_count;

  if (s_scan_result_count > s_scan_result_count_max) {
    s_scan_result_count_max = s_scan_result_count;
  }

  LOG_DBG("WiFi scan completed, found %d networks (max this heartbeat: %d)",
          s_scan_result_count, s_scan_result_count_max);
}

// WiFi network management event handler
static void prv_wifi_event_handler(struct net_mgmt_event_callback* cb,
                                   uint64_t mgmt_event, struct net_if* iface) {
  switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT: {
      const struct wifi_status* status = (const struct wifi_status*)cb->info;
      if (status->status == 0) {
        LOG_INF("WiFi connected");
        s_wifi_connected = true;
        s_last_wifi_ok_timestamp_ms = k_uptime_get();
      } else {
        LOG_WRN("WiFi connection failed: %d", status->status);
        s_wifi_connected = false;
      }
      break;
    }

    case NET_EVENT_WIFI_DISCONNECT_RESULT: {
      LOG_INF("WiFi disconnected");
      s_wifi_connected = false;
      break;
    }

    case NET_EVENT_WIFI_SCAN_RESULT:
      prv_handle_scan_result(cb);
      break;

    case NET_EVENT_WIFI_SCAN_DONE:
      LOG_DBG("WiFi scan done");
      prv_handle_scan_done();
      break;

    default:
      break;
  }
}

// Work handler to trigger WiFi scan
static void prv_wifi_scan_work_handler(struct k_work* work) {
  struct net_if* iface = net_if_get_wifi_sta();
  if (!iface) {
    LOG_ERR("WiFi interface not found");
    return;
  }

  if (s_wifi_scanning) {
    LOG_DBG("WiFi scan already in progress");
    return;
  }

  LOG_DBG("Starting WiFi scan");
  s_wifi_scanning = true;
  s_scan_result_count = 0;
  const struct wifi_scan_params params = {0};
  int ret =
      net_mgmt(NET_REQUEST_WIFI_SCAN, iface, (void*)&params, sizeof(params));
  if (ret) {
    LOG_ERR("Failed to start WiFi scan: %d", ret);
    s_wifi_scanning = false;
  }
}

// WiFi watchdog check thread
static void prv_wifi_watchdog_thread(void* arg1, void* arg2, void* arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  // Wait for system to stabilize
  k_sleep(K_SECONDS(10));

  // Initialize timestamp
  s_last_wifi_ok_timestamp_ms = k_uptime_get();

  while (1) {
    // Always scan to populate the metric, regardless of connection state
    k_work_submit(&s_scan_work);

    // Wait for scan to complete before evaluating results
    k_sleep(K_SECONDS(5));

    // Reset the connected timestamp while WiFi is up
    if (s_wifi_connected) {
      s_last_wifi_ok_timestamp_ms = k_uptime_get();
    } else {
      // Check if we should trigger the watchdog
      uint64_t now = k_uptime_get();
      uint64_t time_since_ok = now - s_last_wifi_ok_timestamp_ms;
      uint32_t timeout_ms = CONFIG_WIFI_WATCHDOG_TIMEOUT_MINUTES * 60 * 1000;

      LOG_DBG("WiFi not connected for %lld ms (timeout: %u ms)", time_since_ok,
              timeout_ms);

      // Try retriggering connection
      struct net_if* iface = net_if_get_wifi_sta();
      int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
      if (rc) {
        LOG_ERR("Error retriggering WiFi connection: %d", rc);
      }

      if (time_since_ok > timeout_ms) {
        // Reboot only if a stored credential matches a visible AP;
        // wifi_credentials_for_each_ssid handles the "creds configured" check.
        bool known_network_present = prv_scan_contains_known_network();

        LOG_WRN("WiFi watchdog timeout: known_network_present=%d",
                known_network_present);

        if (known_network_present) {
          LOG_ERR("WiFi watchdog triggered - rebooting!");

          // Trigger Memfault reboot with custom reason
          MEMFAULT_REBOOT_MARK_RESET_IMMINENT(kMfltRebootReason_WifiWatchdog);

          // Reboot the system
          memfault_platform_reboot();
        }
      }
    }

    // Sleep for check interval
    k_sleep(K_SECONDS(CONFIG_WIFI_WATCHDOG_CHECK_INTERVAL_SECONDS));
  }
}

// Thread stack and definition
#define WIFI_WATCHDOG_STACK_SIZE 2048
#define WIFI_WATCHDOG_PRIORITY 5

K_THREAD_DEFINE(wifi_watchdog_thread, WIFI_WATCHDOG_STACK_SIZE,
                prv_wifi_watchdog_thread, NULL, NULL, NULL,
                WIFI_WATCHDOG_PRIORITY, 0, 0);

// Initialize WiFi watchdog
static int wifi_watchdog_init(void) {
  LOG_INF("WiFi watchdog initialized (timeout: %d minutes)",
          CONFIG_WIFI_WATCHDOG_TIMEOUT_MINUTES);

  // Initialize work item for WiFi scanning
  k_work_init(&s_scan_work, prv_wifi_scan_work_handler);

  // Register WiFi event callbacks
  net_mgmt_init_event_callback(
      &s_wifi_mgmt_cb, prv_wifi_event_handler,
      NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |
          NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
  net_mgmt_add_event_callback(&s_wifi_mgmt_cb);

  return 0;
}

// Memfault metrics collection callback
void wifi_watchdog_metrics_collect(void) {
  // Set the maximum scan result count for this heartbeat period
  int rc = memfault_metrics_heartbeat_set_unsigned(
      MEMFAULT_METRICS_KEY(wifi_scan_result_count_max),
      s_scan_result_count_max);

  if (rc != 0) {
    LOG_ERR("Failed to set wifi_scan_result_count_max metric: %d", rc);
  } else {
    LOG_DBG("Set wifi_scan_result_count_max metric to %u",
            s_scan_result_count_max);
  }

  // Reset the max counter for the next heartbeat period
  s_scan_result_count_max = 0;
}

SYS_INIT(wifi_watchdog_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_WIFI_WATCHDOG_ENABLE */
