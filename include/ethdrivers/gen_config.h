//
// usually this file is generated automatically from CMake settings
//

#pragma once

// Number of RX descriptors in the descriptor ring for the driver
#define CONFIG_LIB_ETHDRIVER_RX_DESC_COUNT  128

// Number of TX descriptors in the descriptor ring for the driver
#define CONFIG_LIB_ETHDRIVER_TX_DESC_COUNT  128

// Number of preallocated DMA buffers. To avoid allocating and freeing buffers
// continuously the driver can preallocate a base amount internally.
// Seems this is not use in the driver, but only in the network stacks from the
// seL4 libs. THere is just a reference in a comment in the driver.
#define CONFIG_LIB_ETHDRIVER_NUM_PREALLOCATED_BUFFERS 32 // defaults to 512

// Size of preallocated DMA buffers that will be used for RX and TX allocation
// requests. This needs to be the maximum of the RX buffer size and the MTU.
// Currently the largest RX buffer of any of the implemented drivers is 2048,
// and the MTU is 1500"
#define LIB_ETHDRIVER_PREALLOCATED_BUF_SIZE     2048
