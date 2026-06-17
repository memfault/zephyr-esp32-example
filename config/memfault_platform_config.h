#pragma once

//! @file
//!
//! @brief
//! Platform overrides for the default configuration settings in the memfault-firmware-sdk.
//! Default configuration settings can be found in "memfault/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_MEMFAULT_SOC_FAMILY_ESP32)
  #define ZEPHYR_DATA_REGION_START _data_start
  #define ZEPHYR_DATA_REGION_END _data_end
#endif

// Override the TLS sec_tag list with only the GTS Root R4 CA cert (sec_tag 2000).
// This replaces the default DigiCert/Amazon list so only the ECDSA proxy chain is trusted.
// Chain: leaf (EC P-256) -> Google WE1 (EC P-256) -> GTS Root R4 (EC P-384, self-signed).
// The cert is installed by memfault_install_ecdsa_certs() in main.c.
#define MEMFAULT_PLATFORM_ROOT_CERTS_ID_LIST 2000,

#ifdef __cplusplus
}
#endif
