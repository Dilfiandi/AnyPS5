# Windows portable release

1. Extract the complete `AnyPS5-portable-v0.2.0.zip` archive to a writable folder.
2. Open `AnyPS5.exe`. Keep it beside `relinker.exe` and the `libs` folder.
3. Select a plain ELF game executable. SELF containers and unsupported inputs are rejected.
4. Keep the output executable beside the game's resources, normally in its `app0` folder.
5. Keep **Intel compatibility** selected on an Intel CPU. It is selected automatically on Intel processors.
6. If the title uses `Media/Modules` or `Media/Plugins`, include these in **Extra module folders**. The file picker detects them automatically. Separate custom folders with a semicolon; relative paths are resolved beside the input executable.
7. Keep **Copy portable runtime libraries beside output** enabled, then click **Convert to Windows**.
8. After a successful conversion, run the generated executable. Keep the converted guest modules and the original title modules in their directories.

All conversions explicitly select `--windows`. Windows dependency diagnostics are enabled by default. A generated console window is retained by default so startup errors remain visible. **Generate game as GUI app** hides that console; it does not affect this converter interface.

The log shows the command and conversion output. Successful conversion does not establish that a game is playable. Compatibility depends on the title, GPU driver and runtime support. The package contains no games, firmware or decryption keys. AnyPS5 requires plain ELF input and does not decrypt SELF files.

For Intel i7-10750H, 32 GB RAM and RTX 2070: use Intel compatibility, select the NVIDIA GPU for the generated executable in Windows graphics settings, and connect the laptop to power. No FPS guarantee is made.

This release includes the relinker and system libraries built from the same source revision. `BUILD-INFO.json` records the version and commit; `SHA256SUMS.txt` on the release page records asset checksums. Do not mix its libraries with the older v0.1.1 package. See `docs/USAGE.md` and `docs/INPUT_MAPPING.md` for module layout and controls.
