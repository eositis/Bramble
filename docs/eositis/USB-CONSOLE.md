# USB CDC console (MegaFlash diagnostic terminal)

**Moved** to sibling **[megaflash-vm](../../../megaflash-vm/)**.

- Stock Bramble: Pico emulator + `-usb-console` / `-spi-flash*` host I/O.
- megaflash-vm: runner scripts, firmware, flash images, operator docs.

```bash
cd ../megaflash-vm
./scripts/run-megaflash-usb-console.sh
# other terminal:
./scripts/connect-usb-console.sh
```

See [megaflash-vm/docs/USB-CONSOLE.md](../../../megaflash-vm/docs/USB-CONSOLE.md) and [TIO-CONSOLE.md](../../../megaflash-vm/docs/TIO-CONSOLE.md).
