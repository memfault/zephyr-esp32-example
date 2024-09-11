# Memfault Zephyr + ESP32 Example

This sample app is based on the Zephyr `samples/net/wifi` example. It
demonstrates a Zephyr + ESP32 integration with the Memfault SDK. It has been
tested on the following boards:

- `esp32s3_devkitm/esp32s3/procpu` (default board)
- `esp32c3_devkitm`*

\* _NOTE: Currently does not have support for data upload. Export chunks to memfault with `mflt export` and upload via the [chunks debug log](https://mflt.io/chunks-debug).

## Getting Started

The Memfault Zephyr integration guide is an excellent reference, and documents
how the Memfault SDK was added to the base example app:

https://docs.memfault.com/docs/mcu/zephyr-guide

To try out this example app:

1. Set up the Zephyr prerequisites: https://docs.zephyrproject.org/latest/develop/getting_started/index.html
2. Create a zephyr workspace and set it up with this project:

   ```bash
   mkdir zephyr-workspace
   cd zephyr-workspace
   west init -m https://github.com/memfault/zephyr-esp32-example
   west update
   ```

3. Build the example app:

   ```bash
   west build zephyr-esp32-example --sysbuild
   ```

   `--sysbuild` will build the MCUboot image as well, which will also get
   flashed in the following step. To run sysbuild by default, you can configure
   your workspace with `west config --local build.sysbuild True`.

4. Flash the example app:

   ```bash
    west flash
   ```

5. Open a serial console and interact with the sample app shell. For example
   using PySerial:

   ```bash
   # select the correct serial port for your system
   pyserial-miniterm --raw /dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_1cd857b4d5a0eb11bf35cdacdf749906-if00-port0 115200

   uart:~$ mflt get_device_info
   [00:09:21.911,000] <inf> mflt: S/N: DEMOSERIAL
   [00:09:21.911,000] <inf> mflt: SW type: zephyr-app
   [00:09:21.911,000] <inf> mflt: SW version: 0.0.1727a85c
   [00:09:21.911,000] <inf> mflt: HW version: esp32c3_devkitm

   # connect wifi
   uart:~$ wifi connect -s "<ssid>" -k <key type> -p "<pw>"

   # test memfault export over uart
   uart:~$ mflt export
   MC:CAKnAgIDAQpqemVwaHlyLWFwcAltMS4wLjArZDU5MmRhOAZvZXNwMzJjM19kZXZraXRtC0Z1sxH2/usEogEABQAISg==:
   ```

## Testing OTA

The payload to upload to Memfault is:

```plaintext
build/zephyr-esp32-example/zephyr/zephyr.signed.confirmed.bin
```

This file is set as "confirmed", so it won't revert after 1 boot cycle (not safe
against bootloops!). See the [Zephyr documentation](https://docs.zephyrproject.org/apidoc/3.6.0/group__mcuboot__api.html#ga95ccc9e1c7460fec16b9ce9ac8ad7a72) for details.

Follow the normal Memfault OTA workflow:

1. build and flash an image with a version like `0.0.1` (see the
   [`VERSION`](VERSION) file for setting an application version).
2. rebuild with a newer version, `0.0.2`
3. upload the `zephyr.signed.confirmed.bin` file to a [Memfault
   Release](https://mflt.io/releases) named `0.0.2`.
4. deploy the `0.0.2` release to the cohort with the device (`default` unless
   changed)
5. on the device shell, connect wifi with `wifi connect <ssid> <pw>`, and
   trigger the OTA with `mflt get_latest_release`
6. when the device reboots, confirm the new version is set with `mflt get_device_info`

> [!NOTE]
>
> If this error occurs when applying the new image:
>
> ```plaintext
> [00:10:07.255,000] <dbg> mflt: memfault_platform_log: Error requesting upgrade, rv=-14
> [00:10:07.260,000] <err> mflt: Error upgrading firmware, rv=-1
> ```
>
> It may be necessary to run `mcuboot erase 2` one time before running the test,
> to wipe the secondary slot so it's ready to receive the new image.
