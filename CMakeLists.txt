#
# i.MX6 NIC driver
#
# Copyright (C) 2020-2024, HENSOLDT Cyber GmbH
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

cmake_minimum_required(VERSION 3.17)


if(SDK_USE_CAMKES)

    os_sdk_import_from_global_components(
        "components/modules/fdt-bind-driver"
        "components/modules/single-threaded"
    )

endif()  # SDK_USE_CAMKES


#-------------------------------------------------------------------------------
function(NIC_IMX6_DeclareCAmkESComponent name)

    cmake_parse_arguments(
        PARSE_ARGV
        1
        NIC_IMX6
        "" # Option arguments
        "" # Single arguments
        "C_FLAGS" # Multiple arguments
    )

    DeclareCAmkESComponent(
        ${name}
        SOURCES
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/ethdriver.c
        INCLUDES
            include
        C_FLAGS
            ${NIC_IMX6_C_FLAGS}
        LIBS
            os_core_api
            ethdrivers
    )

endfunction()



#-------------------------------------------------------------------------------
# declare CAmkES components for all NICs of a platform
function(DeclareCAmkESComponents_for_NICs)

    set(dir_plat "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/plat/${PLATFORM}")

    if(NOT IS_DIRECTORY "${dir_plat}")
        message(FATAL_ERROR "unsupported platform: '${PLATFORM}'")
    endif()

    CAmkESAddCPPInclude("${dir_plat}")
    include("${dir_plat}/plat_nic_imx6.cmake")

endfunction()
