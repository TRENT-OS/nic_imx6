/*
 * Copyright (C) 2020-2023, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#define ZF_LOG_LEVEL ZF_LOG_VERBOSE

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <camkes.h>
#include <camkes/dataport.h>
#include <camkes/dma.h>
#include <camkes/io.h>
#include <camkes/irq.h>
#include <camkes/virtqueue.h>

#include <platsupport/io.h>
#include <platsupport/irq.h>
#include <ethdrivers/raw.h>
#include <ethdrivers/helpers.h>
#include <ethdrivers/plat/eth_plat.h>
#include <sel4utils/sel4_zf_logif.h>
#include <sel4utils/util.h>

#define DMA_BUF_SIZE 2048
#define RX_BUFS 256
#define TX_BUFS 128

#define ENCODE_DMA_ADDRESS(buf) ({ \
    dataport_ptr_t wrapped_ptr = dataport_wrap_ptr(buf); \
    assert(wrapped_ptr.id < (1 << 8) && wrapped_ptr.offset < (1 << 24)); \
    void *new_buf = (void *)(((uintptr_t)wrapped_ptr.id << 24) | ((uintptr_t)wrapped_ptr.offset)); \
    new_buf; })

#define DECODE_DMA_ADDRESS(buf) ({\
    dataport_ptr_t wrapped_ptr = {.id = ((uintptr_t)buf >> 24), .offset = (uintptr_t)buf & MASK(24)}; \
    void *ptr = dataport_unwrap_ptr(wrapped_ptr); \
    ptr; })

typedef struct
{
    // The marker stating wether the initialization was successful must be
    // flagged volatile when strictly following the C rules. A hypothetical highly
    // advanced optimizer could turn global variable accesses into constants, if it
    // concludes the global state is always well known. Also, there is no rule in C
    // that global variables must be synced with memory content on function entry
    // and exit - it is just something that happen due to practical reasons. There
    // is not even a rule that functions must be preserved and can't be inlined,
    // which would eventually allow caching global variables easily. Furthermore, C
    // also does not know threads nor concurrent execution of functions, but both
    // have a string impact on global variables.
    // Using volatile here guarantees at least, that accesses to global variables
    // are accessing the actual memory in the given order stated in the program and
    // there is no caching or constant folding that removes memory accesses. That is
    // the best we can do to avoid surprises at higher optimization levels.
    volatile bool done_init;
    struct eth_driver* eth_driver;
    ps_dma_man_t* dma_man;
    virtqueue_driver_t vq_recv;
    unsigned int num_rx_bufs;
    virtqueue_driver_t vq_send;
    uint8_t mac_address[6];
} imx6_nic_ctx_t;

static imx6_nic_ctx_t imx6_nic_ctx;

//------------------------------------------------------------------------------
static void
nic_event_send_callback(void* cookie)
{
    int ret = nic_event_send_reg_callback(nic_event_send_callback, NULL);
    if (ret)
    {
        LOG_ERROR("Failed to re-initialize send callback");
        return;
    }

    for (;;)
    {
        virtqueue_ring_object_t robj;
        unsigned int used_len;
        int ok = virtqueue_get_used_buf(&imx6_nic_ctx.vq_send, &robj, &used_len);
        if (!ok)
        {
            return;
        }

        void* dma_addr;
        unsigned int buf_len;
        vq_flags_t flag;
        ok = virtqueue_gather_used(&imx6_nic_ctx.vq_send, &robj, &dma_addr, &buf_len, &flag);
        if (!ok)
        {
            LOG_ERROR("Send virtqueue: used empty");
            return;
        }
        void* virt = DECODE_DMA_ADDRESS(dma_addr);

        if (used_len > DMA_BUF_SIZE)
        {
            LOG_ERROR("Send buffer too small");
            return;
        }

        uintptr_t phys = ps_dma_pin(imx6_nic_ctx.dma_man, virt, used_len);
        if (!phys)
        {
            LOG_ERROR("Failed to pin buffer");
            return;
        }

        ret = imx6_nic_ctx.eth_driver->i_fn.raw_tx(
                imx6_nic_ctx.eth_driver,
                1,
                &phys,
                &used_len,
                dma_addr);
        if (ret != ETHIF_TX_ENQUEUED)
        {
            LOG_ERROR("Failed to enqueue tx packet, code %d", ret);
            return;
        }
    }
}

//------------------------------------------------------------------------------
static void cb_eth_tx_complete(
    void* cb_cookie,
    void* cookie)
{
    assert(cb_cookie);
    imx6_nic_ctx_t* nic_ctx = (imx6_nic_ctx_t*)cb_cookie;
    assert(&imx6_nic_ctx == nic_ctx);

    virtqueue_ring_object_t robj;
    virtqueue_init_ring_object(&robj);
    int ok = virtqueue_add_available_buf(&nic_ctx->vq_send, &robj, cookie, DMA_BUF_SIZE, VQ_WRITE);
    if (!ok)
    {
        LOG_ERROR("Send virtqueue: available full");
    }
}


//------------------------------------------------------------------------------
static uintptr_t cb_eth_allocate_rx_buf(
    void* cb_cookie,
    size_t buf_size_in,
    void** cookie)
{
    assert(cb_cookie);
    imx6_nic_ctx_t* nic_ctx = (imx6_nic_ctx_t*)cb_cookie;
    assert(&imx6_nic_ctx == nic_ctx);

    void* virt;
    if (nic_ctx->num_rx_bufs < RX_BUFS)
    {
        ++nic_ctx->num_rx_bufs;
        virt = ps_dma_alloc(
                nic_ctx->dma_man,
                DMA_BUF_SIZE,
                4, // alignment
                0, // uncached
                PS_MEM_HR);
        if (!virt)
        {
            LOG_ERROR("Failed to allocate DMA buffer of size %zu", DMA_BUF_SIZE);
            return 0;
        }
    }
    else
    {
        virtqueue_ring_object_t robj;
        unsigned int used_len;
        vq_flags_t flag;
        // Note: `used_len` is always set to zero.
        int ok = virtqueue_get_used_buf(&nic_ctx->vq_recv, &robj, &used_len);
        if (!ok)
        {
            LOG_INFO("Receive virtqueue: used empty");
            return 0;
        }

        void* dma_addr;
        unsigned int recv_len;
        ok = virtqueue_gather_used(&nic_ctx->vq_recv, &robj, &dma_addr, &recv_len, &flag);
        if (!ok)
        {
            LOG_ERROR("Receive virtqueue: used empty");
            return 0;
        }
        virt = DECODE_DMA_ADDRESS(dma_addr);
    }

    unsigned int buf_len = DMA_BUF_SIZE;
    if (buf_size_in > buf_len)
    {
        LOG_ERROR("Requested size %d doesn't fit in buffer %d", buf_size_in, buf_len);
        return 0;
    }

    *cookie = virt;
    return ps_dma_pin(nic_ctx->dma_man, virt, buf_len);
}


//------------------------------------------------------------------------------
static void
cb_eth_rx_complete(
    void* cb_cookie,
    unsigned int num_bufs,
    void** cookies,
    unsigned int* lens)
{
    assert(cb_cookie);
    imx6_nic_ctx_t* nic_ctx = (imx6_nic_ctx_t*)cb_cookie;
    assert(&imx6_nic_ctx == nic_ctx);

    for (unsigned int i = 0; i < num_bufs; i++)
    {
        virtqueue_ring_object_t robj;
        virtqueue_init_ring_object(&robj);
        int ok = virtqueue_add_available_buf(&nic_ctx->vq_recv, &robj, ENCODE_DMA_ADDRESS(cookies[i]), lens[i], VQ_READ);
        if (!ok)
        {
            LOG_ERROR("Receive virtqueue: available full");
            break;
        }
    }
    nic_event_recv_emit();
}

//------------------------------------------------------------------------------
OS_Error_t
nic_rpc_get_mac_address(uint64_t* mac)
{
    // MAC string aa:bb:cc:dd:ee:ff to big endian integer 0x0000aabbccddeeff
    for (unsigned int i = 0; i < 6; i++)
    {
        *mac <<= 8;
        *mac |= (uint8_t)imx6_nic_ctx.mac_address[i];
    }
    return OS_SUCCESS;
}


#ifdef IMX6_PRIMARY_NIC

// address of the PHY for the 2nd ethernet port is 5. This is something the
// driver could change as any address can be used. But the convention on the
// Nitrogen6_SoloX board seems to be that 5 is used here. Thus we don't allow
// the driver for the second port to specify this values, it's hard-coded here.
#define IMX6_ENET2_PHY_ADDR     5

//------------------------------------------------------------------------------
// RPC interface for secondary NIC driver
void primary_nic__init(void)
{
    // nothing to be done to initialize the interface
}


//------------------------------------------------------------------------------
// RPC interface for secondary NIC driver
int primary_nic_sync(void)
{
    // nothing to be done here, CAmkES guarantees that RPCs are blocked until we
    // have completed server_init(). The secondary NIC will use this function
    // to sync with us and it must be prepared that this RPC call blocks until
    // we are done. It may block forever if there was an initialization error.

    if (!imx6_nic_ctx.done_init)
    {
        LOG_ERROR("Driver init failed, RPCs will be rejected");
        return -1;
    }

    return 0;
}


//------------------------------------------------------------------------------
// RPC interface for secondary NIC driver
int primary_nic_mdio_read(uint16_t reg)
{
    // printf("RPC: MDIO read reg=0x%x, data=0x%x\n", reg);

    if (!imx6_nic_ctx.done_init)
    {
        LOG_ERROR("Driver init failed, reject MDIO read access RPC");
        return -1;
    }

    struct enet* enet = get_enet_from_driver(imx6_nic_ctx.eth_driver);
    assert(enet); // this must be set  if init way successful

    return enet_mdio_read(enet, IMX6_ENET2_PHY_ADDR, reg);
}

//------------------------------------------------------------------------------
// RPC interface for secondary NIC driver
int primary_nic_mdio_write(uint16_t reg, uint16_t data)
{
    // ensure RPC calls are serialized properly, we can only handle them when
    // the semaphore is available, which is after we have finished out init.

    if (!imx6_nic_ctx.done_init)
    {
        LOG_ERROR("Driver init failed, reject MDIO read access RPC");
        return -1;
    }

    struct enet* enet = get_enet_from_driver(imx6_nic_ctx.eth_driver);
    assert(enet); // this must be set  if init way successful

    return enet_mdio_write(enet, IMX6_ENET2_PHY_ADDR, reg, data);
}

#else // not IMX6_PRIMARY_NIC

//------------------------------------------------------------------------------
int call_primary_nic_sync(void)
{
    // call CAmkES function, will block until the primary NIC is up.
    return primary_nic_rpc_sync();
}


//------------------------------------------------------------------------------
int call_primary_nic_mdio_read(uint16_t reg)
{
    // call CAmkES function to make primary NIC driver send the MDIO command
    return primary_nic_rpc_mdio_read(reg);
}


//------------------------------------------------------------------------------
int call_primary_nic_mdio_write(uint16_t reg, uint16_t data)
{
    // call CAmkES function to make primary NIC driver send the MDIO command
    return primary_nic_rpc_mdio_write(reg, data);
}

#endif // [not] IMX6_PRIMARY_NIC


//------------------------------------------------------------------------------
const nic_config_t*
get_nic_configuration(void)
{
    LOG_INFO(
        "[i.MX6 NIC Driver '%s'] get_nic_configuration()",
        get_instance_name());

    static nic_config_t nic_config = {0};
    // CAmkES attributes aren't constant expressions and we can't
    // initialize the struct using a list initializer. As a workaround
    // we set the values here.

    uint64_t mac = 0;
    // MAC string aa:bb:cc:dd:ee:ff to big endian integer 0x0000aabbccddeeff
    for (unsigned int i = 0; i < 6; i++)
    {
        mac <<= 8;
        mac |= (uint8_t)MAC_address[i];
    }

    /* For the 2nd ethernet port, the PHY address is ignored actually, because
     * it is not used in the RPC call. Instead, it's hard-coded above that this
     * is always 5.
     */
    nic_config.phy_address        = nic_phy_address;
    nic_config.id                 = nic_id;
    nic_config.flags              = nic_flags;
    nic_config.mac                = mac;

#ifndef IMX6_PRIMARY_NIC

    nic_config.funcs.sync       = call_primary_nic_sync;
    nic_config.funcs.mdio_read  = call_primary_nic_mdio_read;
    nic_config.funcs.mdio_write = call_primary_nic_mdio_write;

#endif

    return &nic_config;
}


//------------------------------------------------------------------------------
static int cb_eth_interface_found(
    void*  cookie,
    void*  interface_instance,
    char** properties)
{
    assert(cookie);
    assert(interface_instance);

    imx6_nic_ctx_t* nic_ctx = (imx6_nic_ctx_t*)cookie;
    assert(&imx6_nic_ctx == nic_ctx);
    struct eth_driver* eth_driver = interface_instance;

    /* remember the instance */
    nic_ctx->eth_driver = eth_driver;

    return PS_INTERFACE_FOUND_MATCH;
}


//------------------------------------------------------------------------------
// Module initialization
//------------------------------------------------------------------------------

static int
init_virtqueues(ps_io_ops_t *io_ops)
{
    seL4_CPtr send_badge;
    int ret = camkes_virtqueue_driver_init(&imx6_nic_ctx.vq_send, camkes_virtqueue_get_id_from_name("nic_send"));
    if (ret)
    {
        LOG_ERROR("Failed to initialize send virtqueue");
        return -1;
    }
    ret = nic_event_send_reg_callback(nic_event_send_callback, NULL);
    if (ret)
    {
        LOG_ERROR("Failed to initialize send callback");
        return -1;
    }

    ret = camkes_virtqueue_driver_init(&imx6_nic_ctx.vq_recv, camkes_virtqueue_get_id_from_name("nic_recv"));
    if (ret)
    {
        LOG_ERROR("Failed to initialize receive virtqueue");
        return -1;
    }

    imx6_nic_ctx.num_rx_bufs = 0;

    LOG_INFO("allocate TX DMA buffers: %u x %zu (=%zu) bytes",
             TX_BUFS, DMA_BUF_SIZE, TX_BUFS * DMA_BUF_SIZE);
    for (unsigned int i = 0; i < TX_BUFS; i++)
    {
        void* buf = ps_dma_alloc(
                &io_ops->dma_manager,
                DMA_BUF_SIZE,
                4, // alignment
                0, // uncached
                PS_MEM_HW);
        if (!buf)
        {
            LOG_ERROR("Failed to allocate DMA buffer of size %zu for RX buffer #%d ",
                      DMA_BUF_SIZE, i);
            return -1;
        }

        virtqueue_ring_object_t robj;
        virtqueue_init_ring_object(&robj);
        ret = virtqueue_add_available_buf(&imx6_nic_ctx.vq_send, &robj, ENCODE_DMA_ADDRESS(buf), DMA_BUF_SIZE, VQ_WRITE);
        if (!ret) {
            LOG_ERROR("Failed to add buffer to send virtqueue: %d", ret);
            return -1;
        }
    }

    return 0;
}

// We registered this function via the macro CAMKES_POST_INIT_MODULE_DEFINE(),
// but actually it's called as the last thing in the CAmkES pre_init() handler
// implemented by seL4SingleThreadedComponent.template.c function. This means
// we cannot do much interaction with other components here.
int
server_init(ps_io_ops_t* io_ops)
{
    imx6_nic_ctx.dma_man = &io_ops->dma_manager;

    /* this calls cb_eth_interface_found() with the interface instance */
    int error = ps_interface_find(
                    &io_ops->interface_registration_ops,
                    PS_ETHERNET_INTERFACE,
                    cb_eth_interface_found,
                    &imx6_nic_ctx);

    if (error)
    {
        LOG_ERROR("Unable to find an ethernet device, code %d", error);
        return -1;
    }

    /* cb_eth_interface_found() has set this up */
    struct eth_driver* eth_driver = imx6_nic_ctx.eth_driver;
    assert(eth_driver);
    /* cb_cookie is passed to each of the callbacks below */
    eth_driver->cb_cookie = &imx6_nic_ctx;

    static const struct raw_iface_callbacks ethdriver_callbacks =
    {
        .tx_complete = cb_eth_tx_complete,
        .rx_complete = cb_eth_rx_complete,
        .allocate_rx_buf = cb_eth_allocate_rx_buf
    };
    eth_driver->i_cb = ethdriver_callbacks;

    error = init_virtqueues(io_ops);
    if (error)
    {
        LOG_ERROR("Failed to initialize virtqueues, code %d", error);
        return -1;
    }

    /* get MAC from hardware and remember it */
    uint8_t hw_mac[6];
    eth_driver->i_fn.get_mac(eth_driver, hw_mac);
    memcpy(&imx6_nic_ctx.mac_address, hw_mac, sizeof(imx6_nic_ctx.mac_address));

    eth_driver->i_fn.raw_poll(eth_driver);

    imx6_nic_ctx.done_init = true;

    return 0;
}


//------------------------------------------------------------------------------
// this is called when the CAmkES component starts
int do_env_init(
    ps_io_ops_t* io_ops)
{
    memset(&imx6_nic_ctx, 0, sizeof(imx6_nic_ctx));

    return 0;
}


//------------------------------------------------------------------------------
CAMKES_ENV_INIT_MODULE_DEFINE(ethdriver_do_env_init, do_env_init)
CAMKES_POST_INIT_MODULE_DEFINE(ethdriver_run, server_init);

