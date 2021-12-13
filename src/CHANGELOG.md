# Changelog

All notable changes by HENSOLDT Cyber GmbH to this 3rd party module included in
the TRENTOS SDK will be documented in this file.

For more details it is recommended to compare the 3rd party module at hand with
the previous versions of the TRENTOS SDK or the baseline version.

## [1.4]

### Changed

- Increase the size of `CLIENT_RX_BUFS` to the full size of `RX_BUFS`.
- Replace `ZF_LOGW()`, `LOG_ERROR()` and `LOG_INFO()` with the standard TRENTOS
  log macros found in lib_debug.
- Adapt the log level of log message related to the driver running out of empty
  buffers from ERROR to TRACE as this can be expected when dealing with high
  network traffic.

## [1.3]

### Fixed

- Fix an issue which in certain circumstances causes the rx_buffer to be
  duplicated.
- Add missing volatile qualifier to the `done_init` variable.

### Changed

- Format code.
- Adapt to TRENTOS header file changes.
- Send notifications via nic_event_hasData_emit() with every received data
  packet.
- Replace, in the RPC functions, OS_ERROR_GENERIC with more specific error
  codes.
- Adapt MAC handling to modified libethdriver config parameter handling.
- Use uncached DMA memory.

## [1.1]

### Fixed

- Correct mixed up parameters order in DMA function call.

### Changed

- Add dual NIC support.
- Add pool handling helper functions.
- Use `LOG_ERROR()` and `LOG_INFO()`.
- Remove unused code and improve comments.

### Added

- Start development based on commit 6cc96a of
  <https://github.com/seL4/global-components/blob/master/components/Ethdriver/src/ethdriver.c>.
