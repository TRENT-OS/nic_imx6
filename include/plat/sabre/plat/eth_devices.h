/*
 * Copyright 2020, Hensoldt Cyber GmbH
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL2.0+
 */

#pragma once

import <std_connector.camkes>;
import <global-connectors.camkes>;

#include <camkes-fdt-bind-driver.h>


#define HARDWARE_ETHERNET_EXTRA_IMPORTS     /* nothing needed */
#define HARDWARE_ETHERNET_COMPONENT         /* nothing needed */


#define HARDWARE_ETHERNET_INTERFACES                                         \
    consumes Dummy enet;                                                     \
    consumes Dummy ocotp;                                                    \
    consumes Dummy iomux;                                                    \
    consumes Dummy iomux_gpr;                                                \
    consumes Dummy ccm;                                                      \
    consumes Dummy analog;                                                   \
    consumes Dummy gpio2;                                                    \
    consumes Dummy gpio3;                                                    \
    consumes Dummy gpio5;                                                    \
    consumes Dummy gpio6;                                                    \
    emits    Dummy dummy_source;                                             \
    fdt_bind_drivers_interfaces(["/soc/aips-bus@2100000/ethernet@2188000"]);


#define DTB_ETH_HW_MAPPING_CONNECTION(_dst_)  \
    connection seL4DTBHardwareThreadless conn_eth_ ## _dst_( \
        from dummy_source, \
        to   _dst_)

#define HARDWARE_ETHERNET_COMPOSITION         \
    DTB_ETH_HW_MAPPING_CONNECTION(enet);      \
    DTB_ETH_HW_MAPPING_CONNECTION(ocotp);     \
    DTB_ETH_HW_MAPPING_CONNECTION(iomux);     \
    DTB_ETH_HW_MAPPING_CONNECTION(iomux_gpr); \
    DTB_ETH_HW_MAPPING_CONNECTION(ccm);       \
    DTB_ETH_HW_MAPPING_CONNECTION(analog);    \
    DTB_ETH_HW_MAPPING_CONNECTION(gpio3);     \
    DTB_ETH_HW_MAPPING_CONNECTION(gpio6);     \
    DTB_ETH_HW_MAPPING_CONNECTION(gpio2);     \
    DTB_ETH_HW_MAPPING_CONNECTION(gpio5);     \
    fdt_bind_driver_connections();


#define HARDWARE_ETHERNET_CONFIG                                              \
    enet.generate_interrupts = 1;                                             \
    enet.dtb      = dtb({"path":"/soc/aips-bus@2100000/ethernet@2188000"});   \
    ocotp.dtb     = dtb({"path":"/soc/aips-bus@2100000/ocotp@21bc000"});      \
    iomux.dtb     = dtb({"path":"/soc/aips-bus@2000000/iomuxc@20e0000"});     \
    iomux_gpr.dtb = dtb({"path":"/soc/aips-bus@2000000/iomuxc-gpr@20e4000"}); \
    ccm.dtb       = dtb({"path":"/soc/aips-bus@2000000/ccm@20c4000"});        \
    analog.dtb    = dtb({"path":"/soc/aips-bus@2000000/anatop@20c8000"});     \
    gpio2.dtb     = dtb({"path":"/soc/aips-bus@2000000/gpio@20a0000"});       \
    gpio3.dtb     = dtb({"path":"/soc/aips-bus@2000000/gpio@20a4000"});       \
    gpio5.dtb     = dtb({"path":"/soc/aips-bus@2000000/gpio@20ac000"});       \
    gpio6.dtb     = dtb({"path":"/soc/aips-bus@2000000/gpio@20b0000"});
