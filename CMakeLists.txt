#
# Copyright 2020, Hensoldt Cyber GmbH
#
# All rights reserved.
#

cmake_minimum_required(VERSION 3.17)

#-------------------------------------------------------------------------------
# global component setup

set(
    CAMKES_GLOBAL_COMPS_DIR
    "${SEL4_CAMKES_SDK_DIR}/libs/sel4_global_components")

include("${CAMKES_GLOBAL_COMPS_DIR}/global-connectors.cmake")

foreach(
    comp IN ITEMS
        fdt-bind-driver
        single-threaded
)
    add_subdirectory(
        "${CAMKES_GLOBAL_COMPS_DIR}/components/modules/${comp}"
        "sel4_global_components/${comp}")
endforeach()


#-------------------------------------------------------------------------------
# the actual ethernet driver.

CAmkESAddCPPInclude(
    "include"
    "include/${PLATFORM}"
)

set(NIC_IMX6_LIB_ETH_DRV_SRC
    ${UTIL_LIBS_DIR}/libethdrivers/src/helpers.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/imx6.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/enet.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/ocotp_ctrl.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/mx6qsabrelite.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/mxc_gpio.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/fec_mxc.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/phy.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/miiphyutil.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/atheros.c
    ${UTIL_LIBS_DIR}/libethdrivers/src/plat/imx6/uboot/micrel.c
)

set(NIC_IMX6_LIB_ETH_DRV_INC
    ${UTIL_LIBS_DIR}/libethdrivers/include
    ${UTIL_LIBS_DIR}/libethdrivers/plat_include/imx6
)

DeclareCAmkESComponent(
    NIC_IMX6
    SOURCES
        src/ethdriver.c
        ${NIC_IMX6_LIB_ETH_DRV_SRC}
    INCLUDES
        include
        #include/${PLATFORM}
        ${NIC_IMX6_LIB_ETH_DRV_INC}
    C_FLAGS
        -DIMX6_PRIMARY_NIC

    LIBS
        os_core_api
        #muslc
        #ethdrivers_Config
        #sel4_autoconf
)

DeclareCAmkESComponent(
    NIC_IMX6_port2
    SOURCES
        src/ethdriver.c
        ${NIC_IMX6_LIB_ETH_DRV_SRC}
    INCLUDES
        include
        #include/${PLATFORM}
        ${NIC_IMX6_LIB_ETH_DRV_INC}
    C_FLAGS
        #-DIMX6_SECONDARY_NIC
    LIBS
        os_core_api
)
