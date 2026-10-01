# Windows GUI

The portable Windows release includes `AnyPS5.exe`, a native Win32 front end for the relinker.

1. Extract the complete portable ZIP. Do not move `AnyPS5.exe` away from `relinker.exe` and the `libs` folder.
2. Open `AnyPS5.exe`.
3. Choose the PS5 executable and an output `.exe` path.
4. Select optional conversion features when needed.
5. Keep **Copy portable runtime libraries beside output** enabled for the easiest runtime layout.
6. Click **Convert to Windows**.

The GUI displays the relinker output in its log pane. It does not include game files, firmware, keys, or proprietary libraries. Game resources still need to be placed in the expected `app0` layout next to the generated executable.

## Main options

- **Intel compatibility** enables `--to-intel` for supported AMD-only instruction lowering.
- **Generate game as GUI app** enables the Windows GUI subsystem for the converted executable.
- **Windows dependency diagnostics** adds dependency diagnostics to the generated executable.
- **Write call registry JSON** writes the relinker's registry file.
- **Lazy symbol binding** enables lazy binding.
- **Skip syscall validation** and **Skip sce_module processing** are advanced troubleshooting options.
- **Unused NID filter** exposes the relinker's filter levels 0, 1 and 2.
