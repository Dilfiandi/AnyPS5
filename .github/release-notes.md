Windows portable release based on the upstream revision synchronized on 13 October 2026 (`c2565f014912f1f43f7b289fbad02cbf8fab1d93`).

- Restores the native AnyPS5 GUI with input-format validation.
- Explicitly selects Windows output with the current cross-platform relinker.
- Automatically enables Intel compatibility on Intel CPUs.
- Supports extra guest-module directories, including Media/Modules and Media/Plugins.
- Includes the current relinker, patched system libraries, required MinGW runtime DLLs, and usage documentation in one portable ZIP.
- Restores prefix handling in the call-site resolver with its regression test while retaining the upstream cache and range checks.
- Includes build provenance and SHA-256 asset checksums.

Extract the complete AnyPS5-portable-v0.2.0.zip archive and open AnyPS5.exe. Read START-HERE.md before converting a game. This release requires plain ELF input. Successful conversion does not guarantee gameplay compatibility.

AI-assisted implementation. Validation results are recorded in the linked release workflow. Game compatibility and performance have not been tested on the user's laptop.
