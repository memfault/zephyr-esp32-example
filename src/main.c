//! @file

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "app_version.h"
#include "memfault/components.h"
#include "memfault/ports/zephyr/core.h"
#include "memfault/ports/zephyr/http.h"

LOG_MODULE_REGISTER(mflt_app, LOG_LEVEL_DBG);

// This will be available in the memfault/ports/zephyr/core.h header in a later
// Memfault SDK release
extern const char* memfault_zephyr_get_device_id(void);

static const char* prv_get_software_version(void) {
  static char s_software_version[64];

  // CI builds will often set git sha = "". in that case, omit that portion of
  // the software version string
  if (ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1[0] == '\0') {
    return CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_VERSION;
  }

  // Otherwise, append it to demonstrate how to include git sha in the software
  // version string
  snprintf(s_software_version, sizeof(s_software_version), "%s+%s",
           CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_VERSION,
           ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1);
  return s_software_version;
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo* info) {
  // override the software version only to demonstrate inserting the GIT SHA
  *info = (sMemfaultDeviceInfo){
      .device_serial = memfault_zephyr_get_device_id(),
      .software_type = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_TYPE,
      .software_version = prv_get_software_version(),
      .hardware_version = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_HARDWARE_VERSION,
  };
}

int main(void) {
  LOG_INF("Memfault Demo App! Board %s\n", CONFIG_BOARD);

  printk("\n" MEMFAULT_BANNER_COLORIZED);

  memfault_device_info_dump();
  memfault_zephyr_port_install_root_certs();

  // Initiate auto connection on the wifi interface
  struct net_if* iface = net_if_get_wifi_sta();
  int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

  if (rc) {
    printk(
        "an error occurred when trying to auto-connect to a network. err: %d",
        rc);
  }

  return 0;
}
