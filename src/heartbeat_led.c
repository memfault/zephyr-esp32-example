//! @file
//!
//! @brief Heartbeat LED module.
//!
//! Blinks an LED at a 1-second interval to indicate the device is running.
//!
//! On boards with a WS2812 RGB LED (CONFIG_HEARTBEAT_LED_STRIP):
//!   - GREEN when WiFi is connected
//!   - RED when WiFi is not connected
//!
//! On boards with a simple GPIO LED (CONFIG_HEARTBEAT_LED_GPIO):
//!   - Blinks unconditionally at a 1-second interval

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat_led, LOG_LEVEL_DBG);

// Blink half-period: 500ms on + 500ms off = 1 second total
#define HEARTBEAT_BLINK_HALF_PERIOD_MS 500

#define HEARTBEAT_LED_STACK_SIZE 1024
#define HEARTBEAT_LED_PRIORITY 7

#if defined(CONFIG_HEARTBEAT_LED_STRIP)

#include <zephyr/drivers/led_strip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

static const struct device* s_led_strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static bool s_wifi_connected = false;
static struct net_mgmt_event_callback s_wifi_led_mgmt_cb;

// WiFi event handler to track connection state
static void prv_wifi_event_handler(struct net_mgmt_event_callback* cb,
                                   uint64_t mgmt_event, struct net_if* iface) {
  ARG_UNUSED(iface);
  if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
    const struct wifi_status* status = (const struct wifi_status*)cb->info;
    s_wifi_connected = (status->status == 0);
    LOG_DBG("Heartbeat LED: WiFi %s",
            s_wifi_connected ? "connected" : "failed");
  } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
    s_wifi_connected = false;
    LOG_DBG("Heartbeat LED: WiFi disconnected");
  }
}

static void prv_heartbeat_led_thread(void* arg1, void* arg2, void* arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  net_mgmt_init_event_callback(
      &s_wifi_led_mgmt_cb, prv_wifi_event_handler,
      NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&s_wifi_led_mgmt_cb);

  if (!device_is_ready(s_led_strip)) {
    LOG_ERR("LED strip device not ready");
    return;
  }

  bool led_on = false;
  while (1) {
    led_on = !led_on;

    struct led_rgb pixel;
    if (led_on) {
      if (s_wifi_connected) {
        // Green: WiFi connected
        pixel = (struct led_rgb){.r = 0, .g = 32, .b = 0};
      } else {
        // Red: WiFi not connected
        pixel = (struct led_rgb){.r = 32, .g = 0, .b = 0};
      }
    } else {
      // Off
      pixel = (struct led_rgb){.r = 0, .g = 0, .b = 0};
    }

    int rc = led_strip_update_rgb(s_led_strip, &pixel, 1);
    if (rc != 0) {
      LOG_ERR("LED strip update failed: %d", rc);
    }

    k_sleep(K_MSEC(HEARTBEAT_BLINK_HALF_PERIOD_MS));
  }
}

#elif defined(CONFIG_HEARTBEAT_LED_GPIO)

#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec s_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(heartbeat_led), gpios);

static void prv_heartbeat_led_thread(void* arg1, void* arg2, void* arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  if (!gpio_is_ready_dt(&s_led)) {
    LOG_ERR("Heartbeat LED GPIO not ready");
    return;
  }

  int rc = gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);
  if (rc != 0) {
    LOG_ERR("Failed to configure heartbeat LED GPIO: %d", rc);
    return;
  }

  while (1) {
    gpio_pin_toggle_dt(&s_led);
    k_sleep(K_MSEC(HEARTBEAT_BLINK_HALF_PERIOD_MS));
  }
}

#endif /* CONFIG_HEARTBEAT_LED_STRIP / CONFIG_HEARTBEAT_LED_GPIO */

#if defined(CONFIG_HEARTBEAT_LED_STRIP) || defined(CONFIG_HEARTBEAT_LED_GPIO)

K_THREAD_DEFINE(heartbeat_led_thread, HEARTBEAT_LED_STACK_SIZE,
                prv_heartbeat_led_thread, NULL, NULL, NULL,
                HEARTBEAT_LED_PRIORITY, 0, 0);

#endif /* CONFIG_HEARTBEAT_LED_STRIP || CONFIG_HEARTBEAT_LED_GPIO */
