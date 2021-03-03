#
# i.MX6 Sabre Lite board configuration
#
# Copyright (C) 2021, HENSOLDT Cyber GmbH
#
# SPDX-License-Identifier: BSD-3-Clause
#

cmake_minimum_required(VERSION 3.17)


NIC_IMX6_DeclareCAmkESComponent(
    NIC_IMX6
    C_FLAGS
        -DIMX6_PRIMARY_NIC
)
