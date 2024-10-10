//! @file

#include <lfs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include "memfault/components.h"

extern int __real_flash_area_write(const struct flash_area *fa, off_t off, const void *src,
                                   size_t len);

int __wrap_flash_area_write(const struct flash_area *fa, off_t off, const void *src, size_t len) {
  MEMFAULT_METRIC_ADD(flash_write_bytes, len);
  return __real_flash_area_write(fa, off, src, len);
}

extern int __real_flash_area_erase(const struct flash_area *fa, off_t off, size_t len);

int __wrap_flash_area_erase(const struct flash_area *fa, off_t off, size_t len) {
  // We are using an assumption that LFS is erasing in single blocks at a time.
  // Better would be to stash the page size by running
  // flash_get_page_info_by_offs() (on init?) and then use that to calculate the
  // number of pages being erased.
  MEMFAULT_METRIC_ADD(flash_erase_count, 1);

  return __real_flash_area_erase(fa, off, len);
}

extern int __real_flash_area_flatten(const struct flash_area *fa, off_t off, size_t len);

int __wrap_flash_area_flatten(const struct flash_area *fa, off_t off, size_t len) {
  MEMFAULT_METRIC_ADD(flash_erase_count, 1);
  return __real_flash_area_flatten(fa, off, len);
}
