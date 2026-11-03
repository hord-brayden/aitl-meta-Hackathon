# CYD43 (ESP32-4827S043, ESP32-S3) — known-good gadget firmware restore point

Captured 2026-11-03 from the working gadget build (branch `cyd43-gadget-stable`, commit 0921708:
force native landscape + discoverable BLE name + HID gating + 180 flip + backlight fix).
This is the image to flash to bring the CYD43 back to a known-good Musecorder gadget after
bridge-port experiments.

The `*.bin` files are gitignored (`firmware-backup/*.bin`) — the gadget image embeds the Muse
SDK token, so these stay LOCAL and are never committed or published.

## Easiest restore (rebuild from source)
    git checkout cyd43-gadget-stable        # or cyd43
    BOARD=4827 gadget/build.sh
    BOARD=4827 gadget/flash.sh /dev/cu.usbserial-210

## Restore from these exact bins (no rebuild)
Chip: ESP32-S3. Offsets:

    python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset \
      write-flash --flash-mode dio --flash-size keep --flash-freq 80m \
      0x0     gadget-4827-cyd43-stable-2026-11-03-bootloader.bin \
      0x10000 gadget-4827-cyd43-stable-2026-11-03-partition-table.bin \
      0x17000 gadget-4827-cyd43-stable-2026-11-03-ota_data_initial.bin \
      0x19000 gadget-4827-cyd43-stable-2026-11-03-phy_init_data.bin \
      0x20000 gadget-4827-cyd43-stable-2026-11-03-muse-gadget.bin

## SHA-256
    c0b396bd… bootloader.bin
    bb6de32c… partition-table.bin
    7d2c7ac4… ota_data_initial.bin
    6ab5d496… phy_init_data.bin
    dae1a884… muse-gadget.bin
