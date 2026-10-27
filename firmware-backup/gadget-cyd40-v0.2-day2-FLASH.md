# Day-2 gadget firmware backup (Hosyond 4.0" ESP32-32E, BOARD=cyd40, tag v0.2-day2)

The four `gadget-cyd40-v0.2-day2-*.bin` files next to this note are gitignored on purpose: the app image embeds the
SDK token from `.env`. Keep them local (iCloud folder only), never in a repo or a release. Checksums are in
`CHECKSUMS.txt`. Flash with esptool alone, from this folder; pairing and stored applets on the board survive:

```sh
python -m esptool --chip esp32 -p /dev/cu.usbserial-110 -b 460800 --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-size keep --flash-freq 40m \
  0x1000  gadget-cyd40-v0.2-day2-bootloader.bin \
  0x10000 gadget-cyd40-v0.2-day2-partition-table.bin \
  0x16000 gadget-cyd40-v0.2-day2-phy_init_data.bin \
  0x20000 gadget-cyd40-v0.2-day2-muse-gadget.bin
```

Rebuilding from source instead: checkout tag `v0.2-day2`, `BOARD=cyd40 gadget/build.sh` (needs ESP-IDF v6.0.1 and
`MUSE_GADGET_SDK_TOKEN` in `.env`), then `gadget/flash.sh`.
