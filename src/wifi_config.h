#pragma once

struct wifi_config {
  char ssid[32];
  char password[64];
};

int wifi_config_get(struct wifi_config *config);
