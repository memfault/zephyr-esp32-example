//! @file
//!
//! @brief SNTP / real-time clock synchronization module.
//!
//! On each WiFi connect event a dedicated thread wakes up, waits 2 s for
//! DHCP/DNS to settle, then queries CONFIG_SNTP_TIME_SYNC_SERVER using the
//! Zephyr SNTP client API (sntp_init / sntp_query / sntp_close).  On success
//! the kernel real-time clock is set via sys_clock_settime(SYS_CLOCK_REALTIME)
//! so that CONFIG_LOG_TIMESTAMP_USE_REALTIME shows accurate UTC times in logs.
//!
//! The thread also re-syncs automatically every
//! CONFIG_SNTP_TIME_SYNC_RESYNC_INTERVAL_SECONDS while WiFi stays connected.
//!
//! Time persistence across soft resets (watchdog, crash, SW reset):
//!   Per the ESP32 hardware docs (espressif,esp32-rtc-timer): "Any reset/sleep
//!   mode, except for the power-up reset, will not stop or reset the RTC Timer.
//!   There is also no need to enable the RTC Timer node, it starts running from
//!   power-up."
//!
//!   We exploit this as follows:
//!   - On every SNTP sync, a (unix_sec, counter_ticks) anchor is written into a
//!     struct placed in RTC slow RAM via RTC_NOINIT_ATTR (esp_attr.h section
//!     ".rtc_noinit").  RTC slow RAM is a separate hardware region that is NOT
//!     cleared on soft resets, and is not part of the .data/.bss segment, so it
//!     is immune to FOTA updates changing the main RAM layout.
//!   - On boot, if the magic sentinel in the anchor is intact (soft reset
//!   path),
//!     we read the current counter ticks, compute elapsed seconds, and restore
//!     the kernel clock before the first SNTP sync fires.
//!   - On a power-on reset, RTC slow RAM IS cleared → magic is 0 → we skip
//!     restoration and wait for SNTP.  The counter itself is already running
//!     from power-up so the first SNTP sync will immediately produce a valid
//!     anchor.
//!
//! Required Kconfig:
//!   CONFIG_SNTP=y
//!   CONFIG_NET_UDP=y          (SNTP runs over UDP)
//!   CONFIG_DNS_RESOLVER=y     (for zsock_getaddrinfo)
//!   CONFIG_COUNTER=y          (counter API for RTC timer)
//!   CONFIG_SNTP_TIME_SYNC_ENABLE=y   (this module, default y)

#include <esp_attr.h>  // RTC_NOINIT_ATTR – places data in .rtc_noinit (RTC slow RAM)
#include <time.h>  // struct tm, struct timespec, gmtime_r
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/clock.h>

#include "memfault/components.h"

LOG_MODULE_REGISTER(sntp_time_sync, LOG_LEVEL_DBG);

#if defined(CONFIG_SNTP_TIME_SYNC_ENABLE)

// Heartbeat counter: successful SNTP syncs in the current period
static uint32_t s_sync_count = 0;

// Clock delta from the most recent sync where the pre-sync clock was valid.
// Signed: positive = system was behind NTP, negative = system was ahead.
static int32_t s_last_sync_delta_s = 0;
static bool s_sync_delta_valid = false;

// ----------------------------------------------------------------------------
// RTC anchor: persists time across soft resets via RTC slow RAM.
// RTC_NOINIT_ATTR (from esp_attr.h) places the struct in the .rtc_noinit
// linker section, which maps to the ESP32 RTC slow memory region.  This region
// is NOT cleared on soft resets (watchdog, crash, SW reset), and is entirely
// separate from the main .data/.bss segment, so it is unaffected by FOTA
// updates that change the normal RAM layout.  A magic sentinel detects fresh
// power-on resets where RTC slow RAM IS cleared by hardware.
// ----------------------------------------------------------------------------

#define RTC_ANCHOR_MAGIC UINT64_C(0xB00710E700C10C1F)

struct rtc_anchor {
  uint64_t magic;
  int64_t unix_sec;  // Unix seconds recorded at last SNTP sync
  uint64_t ticks;    // Counter ticks at that moment
};

// RTC_NOINIT_ATTR: placed in RTC slow RAM (.rtc_noinit section).
// Survives soft resets; cleared only on power-on reset.
static RTC_NOINIT_ATTR struct rtc_anchor s_rtc_anchor;

#if DT_NODE_HAS_STATUS(DT_NODELABEL(rtc_timer), okay)
static const struct device* const s_rtc_counter_dev =
    DEVICE_DT_GET(DT_NODELABEL(rtc_timer));
#endif

// WiFi connection state – written by net_mgmt callback, read by the thread
static volatile bool s_wifi_connected = false;

// Semaphore given each time WiFi connects; wakes the sync thread.
// Max count 1 so rapid connect/disconnect events collapse to one wakeup.
static K_SEM_DEFINE(s_wifi_connected_sem, 0, 1);

// ----------------------------------------------------------------------------
// WiFi event handler
// ----------------------------------------------------------------------------

static struct net_mgmt_event_callback s_wifi_mgmt_cb;

static void prv_wifi_event_handler(struct net_mgmt_event_callback* cb,
                                   uint64_t mgmt_event, struct net_if* iface) {
  switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT: {
      const struct wifi_status* status = (const struct wifi_status*)cb->info;
      if (status->status == 0) {
        s_wifi_connected = true;
        k_sem_give(&s_wifi_connected_sem);
      }
      break;
    }
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
      s_wifi_connected = false;
      break;
    default:
      break;
  }
}

// ----------------------------------------------------------------------------
// SNTP query helper
// ----------------------------------------------------------------------------

static int prv_sntp_sync(void) {
  // Resolve the NTP server hostname to an IPv4 address
  struct zsock_addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_DGRAM,
  };
  struct zsock_addrinfo* res = NULL;

  int rc = zsock_getaddrinfo(CONFIG_SNTP_TIME_SYNC_SERVER, "123", &hints, &res);
  if (rc != 0 || res == NULL) {
    LOG_ERR("DNS lookup for '%s' failed: %d", CONFIG_SNTP_TIME_SYNC_SERVER, rc);
    return -EHOSTUNREACH;
  }

  struct sntp_ctx ctx;
  rc = sntp_init(&ctx, res->ai_addr, res->ai_addrlen);
  zsock_freeaddrinfo(res);
  if (rc < 0) {
    LOG_ERR("sntp_init failed: %d", rc);
    return rc;
  }

  struct sntp_time sntp_time;
  rc = sntp_query(&ctx, 4 * MSEC_PER_SEC, &sntp_time);
  sntp_close(&ctx);

  if (rc < 0) {
    LOG_WRN("SNTP query failed: %d", rc);
    return rc;
  }

  // If the system clock is already valid (> Jan 1 2023), record the delta
  // between the current kernel time and the SNTP-provided time.  We exclude
  // the initial sync from epoch so the metric only reflects genuine drift.
#define UNIX_JAN_2023 1672531200LL
  struct timespec ts_before;
  if (sys_clock_gettime(SYS_CLOCK_REALTIME, &ts_before) == 0 &&
      ts_before.tv_sec > UNIX_JAN_2023) {
    s_last_sync_delta_s =
        (int32_t)((int64_t)sntp_time.seconds - (int64_t)ts_before.tv_sec);
    s_sync_delta_valid = true;
    LOG_DBG("SNTP clock delta: %ds", s_last_sync_delta_s);
  }

  // Set the kernel real-time clock
  struct timespec ts = {
      .tv_sec = (time_t)sntp_time.seconds,
      .tv_nsec = 0,
  };
  int set_rc = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
  if (set_rc != 0) {
    LOG_WRN("sys_clock_settime failed: %d", set_rc);
  }

#if DT_NODE_HAS_STATUS(DT_NODELABEL(rtc_timer), okay)
  // Save a (unix_sec, ticks) anchor so time survives a soft reset.
  // counter_get_value_64 gives full 64-bit precision, avoiding 32-bit
  // wraparound.
  if (device_is_ready(s_rtc_counter_dev)) {
    uint64_t ticks;
    if (counter_get_value_64(s_rtc_counter_dev, &ticks) == 0) {
      s_rtc_anchor.unix_sec = (int64_t)sntp_time.seconds;
      s_rtc_anchor.ticks = ticks;
      s_rtc_anchor.magic = RTC_ANCHOR_MAGIC;
      LOG_DBG("RTC anchor saved (unix=%lld ticks=%llu)", s_rtc_anchor.unix_sec,
              s_rtc_anchor.ticks);
    }
  } else {
    LOG_WRN("RTC counter device not ready");
  }
#endif

  // Log the synced time as human-readable UTC
  struct tm utc;
  gmtime_r(&ts.tv_sec, &utc);
  LOG_INF("SNTP sync OK - UTC: %04d-%02d-%02d %02d:%02d:%02d",
          utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
          utc.tm_min, utc.tm_sec);

  s_sync_count++;
  return 0;
}

// ----------------------------------------------------------------------------
// Sync thread
// ----------------------------------------------------------------------------

static void prv_sntp_sync_thread(void* a, void* b, void* c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  while (1) {
    // Block until a WiFi-connect event fires or the re-sync interval elapses
    k_sem_take(&s_wifi_connected_sem,
               K_SECONDS(CONFIG_SNTP_TIME_SYNC_RESYNC_INTERVAL_SECONDS));

    if (!s_wifi_connected) {
      LOG_DBG("WiFi not connected, skipping SNTP sync");
      continue;
    }

    // Brief delay to let DHCP and DNS server assignment settle
    k_sleep(K_SECONDS(2));

    prv_sntp_sync();
  }
}

#define SNTP_THREAD_STACK_SIZE 2048
#define SNTP_THREAD_PRIORITY 6

K_THREAD_DEFINE(sntp_sync_thread, SNTP_THREAD_STACK_SIZE, prv_sntp_sync_thread,
                NULL, NULL, NULL, SNTP_THREAD_PRIORITY, 0, 0);

// ----------------------------------------------------------------------------
// Metrics collection helper – called from heartbeat_metrics.c
// ----------------------------------------------------------------------------

void sntp_time_sync_metrics_collect(void) {
  int rc = memfault_metrics_heartbeat_add(MEMFAULT_METRICS_KEY(sntp_sync_count),
                                          s_sync_count);
  if (rc != 0) {
    LOG_ERR("Failed to add sntp_sync_count metric: %d", rc);
  } else {
    LOG_DBG("sntp_sync_count += %u", s_sync_count);
  }
  s_sync_count = 0;

  if (s_sync_delta_valid) {
    rc = memfault_metrics_heartbeat_set_signed(
        MEMFAULT_METRICS_KEY(sntp_sync_delta_seconds), s_last_sync_delta_s);
    if (rc != 0) {
      LOG_ERR("Failed to set sntp_sync_delta_seconds metric: %d", rc);
    } else {
      LOG_DBG("sntp_sync_delta_seconds = %d", s_last_sync_delta_s);
    }
    s_sync_delta_valid = false;
  }
}

// ----------------------------------------------------------------------------
// Initialisation
// ----------------------------------------------------------------------------

static int sntp_time_sync_init(void) {
  LOG_INF("SNTP time sync initialized (server: %s, resync interval: %ds)",
          CONFIG_SNTP_TIME_SYNC_SERVER,
          CONFIG_SNTP_TIME_SYNC_RESYNC_INTERVAL_SECONDS);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(rtc_timer), okay)
  // Restore time from the RTC slow RAM anchor if the magic is intact (meaning
  // this is a soft reset, not a power-on reset).  Read the current counter
  // value, compute elapsed seconds using counter_get_frequency, and set the
  // kernel real-time clock so log timestamps are correct before SNTP first
  // fires.
  if (device_is_ready(s_rtc_counter_dev) &&
      s_rtc_anchor.magic == RTC_ANCHOR_MAGIC) {
    uint64_t ticks_now;
    if (counter_get_value_64(s_rtc_counter_dev, &ticks_now) == 0) {
      uint32_t freq = counter_get_frequency(s_rtc_counter_dev);
      int64_t elapsed_sec =
          (int64_t)((ticks_now - s_rtc_anchor.ticks) / (uint64_t)freq);
      int64_t current_unix = s_rtc_anchor.unix_sec + elapsed_sec;
      // Sanity: reject anything before Jan 1 2020
      if (current_unix > 1577836800LL) {
        struct timespec ts = {.tv_sec = (time_t)current_unix, .tv_nsec = 0};
        int rc = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
        if (rc == 0) {
          struct tm utc;
          gmtime_r(&ts.tv_sec, &utc);
          LOG_INF(
              "Restored time from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC "
              "(elapsed %llds)",
              utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
              utc.tm_min, utc.tm_sec, (long long)elapsed_sec);
        } else {
          LOG_WRN("sys_clock_settime from RTC anchor failed: %d", rc);
        }
      } else {
        LOG_DBG("RTC anchor time implausible - waiting for SNTP");
        s_rtc_anchor.magic = 0;  // invalidate so we don't use it again
      }
    }
  }
#endif

  net_mgmt_init_event_callback(
      &s_wifi_mgmt_cb, prv_wifi_event_handler,
      NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&s_wifi_mgmt_cb);

  return 0;
}

SYS_INIT(sntp_time_sync_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_SNTP_TIME_SYNC_ENABLE */
