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

int fs_log_init(void) {
  int res = fs_mount(&mp);

  if (res == 0) {
    printk("Disk mounted.\n");
    /* Try to unmount and remount the disk */
    res = fs_unmount(&mp);
    if (res != 0) {
      printk("Error unmounting disk\n");
      return res;
    }
    res = fs_mount(&mp);
    if (res != 0) {
      printk("Error remounting disk\n");
      return res;
    }

    if (lsdir(disk_mount_pt) == 0) {
      printk("Directory listing successful.\n");
    } else {
      printk("Error listing directory.\n");
    }
  } else {
    printk("Error mounting disk.\n");
  }

  fs_unmount(&mp);

  return res;
}

#endif  // CONFIG_FILE_SYSTEM
