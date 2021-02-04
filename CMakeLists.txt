#
# Copyright (C) 2020-2021, HENSOLDT Cyber GmbH
# SPDX-License-Identifier: BSD-3-Clause
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

DeclareCAmkESComponent(
    NIC_IMX6
    SOURCES
        src/ethdriver.c
    INCLUDES
        include
    C_FLAGS
        -DIMX6_PRIMARY_NIC
    LIBS
        os_core_api
        ethdrivers
)

DeclareCAmkESComponent(
   NIC_IMX6_port2
    SOURCES
        src/ethdriver.c
    INCLUDES
        include
    LIBS
        os_core_api
        ethdrivers
)
