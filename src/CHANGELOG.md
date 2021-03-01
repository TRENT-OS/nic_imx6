# Changelog

All notable changes by HENSOLDT Cyber GmbH to this 3rd party module included in
the TRENTOS-M SDK will be documented in this file.

For more details it is recommended to compare the 3rd party module at hand with
the previous versions of the TRENTOS-M SDK or the baseline version.

## [1.3]

### Fixed

- Fix an issue which in certain circumstances causes the rx_buffer to be
duplicated.

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
