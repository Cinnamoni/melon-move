# melonDS for reMarkable Paper Pro Move

A Nintendo DS emulator packaged as an [AppLoad](https://github.com/asivery/rm-appload)
app for [XOVI](https://github.com/asivery/xovi). It uses the Linux-compatible
software-rendering core from [melonDS 1.1](https://github.com/melonDS-emu/melonDS/tree/1.1).
The QML frontend runs inside AppLoad and the emulator runs in a separate process.

The Marker acts as the DS stylus on the lower screen. Translucent D-pad, A/B/X/Y,
L/R, Start and Select buttons overlay the display. Tap **Dock** to put them below
the display when you need access to the entire lower screen. Tap **Overlay** to
move them back. **Pause** freezes emulation. **Library** closes the game and
flushes its cartridge save.

The first build targets Nintendo DS cartridge games and homebrew in `.nds` files.
It uses melonDS's FreeBIOS and generated DS firmware. Game Boy, Game Boy Color,
standalone Game Boy Advance games, DSiWare, audio output, microphone, wireless
play and save states are outside this build's scope.

## Install

You need a Paper Pro Move in Developer Mode, SSH access and a working XOVI setup
with qt-resource-rebuilder and an AppLoad version compatible with your firmware.
The supplied ARM64 build uses the official `chiappa` SDK for firmware **3.25.1.1**.
For other firmware versions, use your matching SDK and AppLoad release.

From a computer connected to the tablet over USB, copy the package and install it.
Replace `10.11.99.1` if your tablet has a different address.

```sh
scp melon-move-chiappa-3.25.1.1.tar.gz root@10.11.99.1:/tmp/
ssh root@10.11.99.1 '
  mkdir -p /home/root/xovi/exthome/appload /home/root/melon-move/roms
  tar -xzf /tmp/melon-move-chiappa-3.25.1.1.tar.gz -C /home/root/xovi/exthome/appload
'
scp "/path/to/your-game.nds" root@10.11.99.1:/home/root/melon-move/roms/
```

Open **melonDS** from AppLoad. Reload AppLoad or restart XOVI through your usual
launcher setup if the icon has yet to appear. Tap **Refresh** after copying more
games. In-game cartridge saves are kept under `/home/root/melon-move/saves/` with
the name `Game.nds.sav`. Keep the ROM filename stable to preserve the association.

To uninstall, remove the `melon-move` app folder under
`/home/root/xovi/exthome/appload/`. Your ROMs and saves are in the separate
`/home/root/melon-move/` directory.

## Build from source

Install the [official reMarkable SDK](https://developer.remarkable.com/links) for
**chiappa** (Paper Pro Move) that matches the tablet firmware and activate its
environment script. From the source directory on your Linux build host:

```sh
source /path/to/sdk/environment-setup-cortexa55-remarkable-linux
cmake -S . -B build-move -DCMAKE_BUILD_TYPE=Release -DSTATIC_CXX_RUNTIME=ON
cmake --build build-move --parallel
QT_RCC="$OECORE_NATIVE_SYSROOT/usr/libexec/rcc" ./tools/package.sh build-move dist
tar -C dist -czf melon-move-chiappa-3.25.1.1.tar.gz melon-move
```

Adjust the archive name when targeting a different firmware. CMake fetches the
pinned upstream commit. If you have the full source archive, add
`-DFETCHCONTENT_SOURCE_DIR_MELONDS="$PWD/upstream/melonDS"` to configure offline
against the included core. A native Linux build can use the same commands with
the SDK inactive and an installed host Qt 6 `rcc`. It can run in the AppLoad PC
host. Set `MELON_MOVE_DATA_DIR` in the host environment to change the ROM and save
root for desktop testing.

The emulator targets normal DS timing. The UI presents up to eight frames per
second to limit e-ink redraws. Fast games can be difficult to follow. The
application uses AppLoad's local socket for messages and temporary BMP files
for frames. It does not open network ports.

## Verification status

The ARM64 backend was cross-compiled with the official 3.25.1.1 Move SDK. The
smoke test passed for the native Linux and QEMU ARM64 builds and used a minimal synthetic DS ROM to exercise the library,
invalid-ROM handling, direct boot, frame output, pause/resume, input messages
and shutdown. A Qt 6 test loaded the packaged QML and exercised the library and
gameplay views. Physical Marker events, multitouch, e-ink latency, game
compatibility and actual AppLoad startup on a Move still need device testing.

## License

GPL-3.0-or-later. The pinned melonDS source contains the upstream credits,
component licenses and notices. The project and package contain no commercial
ROMs or Nintendo BIOS dumps.
