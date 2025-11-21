//! @file

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_version.h"
#include "memfault/components.h"
#include "memfault/ports/zephyr/core.h"
#include "memfault/ports/zephyr/http.h"

LOG_MODULE_REGISTER(mflt_app, LOG_LEVEL_DBG);

// This will be available in the memfault/ports/zephyr/core.h header in a later
// Memfault SDK release
extern const char* memfault_zephyr_get_device_id(void);

void memfault_platform_get_device_info(sMemfaultDeviceInfo* info) {
  // override the software version only to demonstrate inserting the GIT SHA
  *info = (sMemfaultDeviceInfo){
    .device_serial = memfault_zephyr_get_device_id(),
    .software_type = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_TYPE,
    .software_version =
      CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_SOFTWARE_VERSION "+" ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1,
    .hardware_version = CONFIG_MEMFAULT_BUILTIN_DEVICE_INFO_HARDWARE_VERSION,
  };
}

int main(void) {
  LOG_INF("Memfault Demo App! Board %s\n", CONFIG_BOARD);

  printk("\n" MEMFAULT_BANNER_COLORIZED);

  memfault_device_info_dump();
  memfault_zephyr_port_install_root_certs();

  return 0;
}
