# External Firmware Dependencies (Submodules)

Place upstream git submodules in this directory so third-party/upstream code and licenses stay cleanly decoupled from first-party Unipad firmware and hardware files.

## Expected Submodules

- **`bluepad32`** (`firmware/external/bluepad32`):
  ```bash
  git submodule add https://github.com/ricardoquesada/bluepad32.git firmware/external/bluepad32
  git submodule update --init --recursive
  ```
