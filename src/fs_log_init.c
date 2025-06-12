//! @file

#if defined(CONFIG_FILE_SYSTEM)

  #include <zephyr/device.h>
  #include <zephyr/fs/fs.h>
  #include <zephyr/kernel.h>
  #include <zephyr/logging/log.h>
  #include <zephyr/storage/disk_access.h>

LOG_MODULE_REGISTER(fs_log_init, LOG_LEVEL_INF);

  #include <ff.h>

  /*
   *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
   *  in ffconf.h
   */
  #if defined(CONFIG_DISK_DRIVER_MMC)
    #define DISK_DRIVE_NAME "SD2"
  #else
    #define DISK_DRIVE_NAME "SD"
  #endif

  #define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"

_Static_assert(FR_OK == 0, "FR_OK must be 0");

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
  .type = FS_FATFS,
  .fs_data = &fat_fs,
  .mnt_point = DISK_MOUNT_PT,
};

static void prv_ioctl_test(void) {
  do {
    static const char *disk_pdrv = DISK_DRIVE_NAME;
    uint64_t memory_size_mb;
    uint32_t block_count;
    uint32_t block_size;

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_INIT, NULL) != 0) {
      LOG_ERR("Storage init ERROR!");
      break;
    }

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
      LOG_ERR("Unable to get sector count");
      break;
    }
    LOG_INF("Block count %u", block_count);

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
      LOG_ERR("Unable to get sector size");
      break;
    }
    printk("Sector size %u\n", block_size);

    memory_size_mb = (uint64_t)block_count * block_size;
    printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
      LOG_ERR("Storage deinit ERROR!");
      break;
    }
  } while (0);
}

int fs_log_init(void) {
  prv_ioctl_test();

  LOG_INF("ioctl_test completed.\n");

  LOG_INF("Mounting disk at '%s'", DISK_MOUNT_PT);
  int res = fs_mount(&mp);

  if (res == 0) {
    LOG_INF("Disk mounted.\n");
  }

  // fs_unmount(&mp);

  return 0;  // res;
}

#endif  // CONFIG_FILE_SYSTEM
