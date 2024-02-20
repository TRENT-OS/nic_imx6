#
# i.MX6 NIC driver, i.MX6sx Nitrogen board configuration
#
# Copyright (C) 2020-2024, HENSOLDT Cyber GmbH
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

cmake_minimum_required(VERSION 3.17)


NIC_IMX6_DeclareCAmkESComponent(
    NIC_IMX6
    C_FLAGS
        -DIMX6_PRIMARY_NIC
)

NIC_IMX6_DeclareCAmkESComponent(
    NIC_IMX6_port2
)
