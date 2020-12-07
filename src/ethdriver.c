/*
 * Copyright 2020, Hensoldt Cyber GmbH
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL2.0+
 */

#include "OS_Dataport.h"
#include "network/OS_NetworkStack.h"

#include <stdbool.h>
#include <string.h>

#include <camkes.h>
#include <camkes/dma.h>
#include <camkes/io.h>
#include <camkes/irq.h>

#include <platsupport/io.h>
#include <platsupport/irq.h>
#include <ethdrivers/raw.h>
#include <ethdrivers/helpers.h>
#include <sel4utils/sel4_zf_logif.h>

#define RX_BUFS          256
#define CLIENT_RX_BUFS   128
#define CLIENT_TX_BUFS   128
#define DMA_BUF_SIZE    2048

typedef struct
{
    dma_addr_t dma;
    int len;
} rx_tx_frame;

// Each client has a pool of TX frames
typedef rx_tx_frame tx_frame_t;

// Clients share a pool of RX frames
typedef rx_tx_frame rx_frame_t;


typedef struct
{
    /* this flag indicates whether we or not we need to notify the client
     * if new data is received. We only notify once the client observes
     * the last packet */
    bool should_notify;

    /* keeps track of the head of the queue */
    unsigned int pending_rx_head;
    /* keeps track of the tail of the queue */
    unsigned int pending_rx_tail;
    /*
     * this is a cyclic queue of RX buffers pending to be read by a client,
     * the head represents the first buffer in the queue, and the tail the last
     */
    rx_frame_t pending_rx[CLIENT_RX_BUFS];

    /* keeps track of how many TX buffers are in use */
    unsigned int num_tx;
    /* the allocated TX buffers for the client */
    tx_frame_t tx_mem[CLIENT_TX_BUFS];
    /*
     * this represents the pool of buffers that can be used for TX,
     * this array is a sliding array in that num_tx acts a pointer to
     * separate between buffers that are in use and buffers that are
     * not in use. E.g. 'o' = free, 'x' = in use
     *  -------------------------------------
     *  | o | o | o | o | o | o | x | x | x |
     *  -------------------------------------
     *                          ^
     *                        num_tx
     */
    tx_frame_t* pending_tx[CLIENT_TX_BUFS];

    /* mac address for this client */
    uint8_t mac[6];
} client_t;


typedef struct
{
    bool done_init;
    struct eth_driver* eth_driver;
    unsigned int num_rx_bufs;
    dma_addr_t rx_bufs[RX_BUFS];
    dma_addr_t* rx_buf_pool[RX_BUFS];
    client_t client;
} imx6_nic_ctx_t;


//------------------------------------------------------------------------------
// global variables
imx6_nic_ctx_t imx6_nic_ctx;


//------------------------------------------------------------------------------
static void eth_tx_complete(
    void* iface,
    void* cookie)
{
    client_t* client = &imx6_nic_ctx.client;
    client->pending_tx[client->num_tx++] = (tx_frame_t*)cookie;
}


//------------------------------------------------------------------------------
static uintptr_t eth_allocate_rx_buf(
    void* iface,
    size_t buf_size,
    void** cookie)
{
    if (buf_size > DMA_BUF_SIZE)
    {
        LOG_ERROR("Requested size doesn't fit in buffer");
        return 0;
    }
    if (imx6_nic_ctx.num_rx_bufs == 0)
    {
        LOG_ERROR("Invalid number of buffers");
        return 0;
    }
    imx6_nic_ctx.num_rx_bufs--;
    *cookie = imx6_nic_ctx.rx_buf_pool[imx6_nic_ctx.num_rx_bufs];
    return imx6_nic_ctx.rx_buf_pool[imx6_nic_ctx.num_rx_bufs]->phys;
}


//------------------------------------------------------------------------------
static void eth_rx_complete(
    void* iface,
    unsigned int num_bufs,
    void** cookies,
    unsigned int* lens)
{
    client_t* client = &imx6_nic_ctx.client;

    if (num_bufs != 1)
    {
        LOG_ERROR("Trying to write %d buffers, can only do one", num_bufs);
    }
    else if ((client->pending_rx_head + 1) % CLIENT_RX_BUFS == client->pending_rx_tail)
    {
        LOG_ERROR("RX buffer overflow");
    }
    else
    {
        rx_frame_t* rx_frame = &client->pending_rx[client->pending_rx_head];
        rx_frame->dma        = *(dma_addr_t*)(cookies[0]);
        rx_frame->len        = lens[0];

        client->pending_rx_head = (client->pending_rx_head + 1) % CLIENT_RX_BUFS;
        if (client->should_notify)
        {
            nic_event_hasData_emit();
            client->should_notify = false;
        }
        return;
    }

    /* abort and put all the bufs back */
    for (unsigned int i = 0; i < num_bufs; i++)
    {
        imx6_nic_ctx.rx_buf_pool[imx6_nic_ctx.num_rx_bufs] = (dma_addr_t*)(cookies[i]);
        imx6_nic_ctx.num_rx_bufs++;
    }
}


//------------------------------------------------------------------------------
/** If eth frames have been received by the driver, copy a single frame from
 * the driver's buffer (rx_bufs), into the dataport of the caller of this
 * function.
 *
 * @param[out] len The size in bytes of the eth frame.
 * @param[out] framesRemaining Flag indicating if further frames are available
 * to be read.
 * @return     OS_SUCCESS   A frame was placed in the dataport. len and
 *             framesRemaining are updated accordingly.
 *             OS_ERROR_NOT_INITIALIZED The device hasn't finished initializing.
 *             The call should be retried.
 *             OS_ERROR_NO_DATA No data is available to be read.
 */
OS_Error_t
client_rx_data(
    size_t* pLen,
    size_t* framesRemaining)
{
    if (!imx6_nic_ctx.done_init)
    {
        LOG_ERROR("Device not initialized");
        return OS_ERROR_NOT_INITIALIZED;
    }

    client_t* client = &imx6_nic_ctx.client;
    if (client->pending_rx_head == client->pending_rx_tail)
    {
        // Ideally, the network stack does not poll the driver and we end up
        // here only in very few cases. Practically, we see this message a lot
        // and this pollutes the logs. This needs further investigation, until
        // then we don't print anything here.
        //   LOG_INFO("no RX data, client should wait for notification");
        client->should_notify = true;
        return OS_ERROR_NO_DATA;
    }
    rx_frame_t* rx = &client->pending_rx[client->pending_rx_tail];

    /* ToDo: Instead of copying the DMA buffer into the shared dataport memory,
     *       we should share the ring buffer elements with the network stack to
     *       use a zero-copy approach.
     */
    memcpy(nic_port_to, rx->dma.virt, rx->len);
    *pLen = rx->len;

    client->pending_rx_tail = (client->pending_rx_tail + 1) % CLIENT_RX_BUFS;
    if (client->pending_rx_tail == client->pending_rx_head)
    {
        client->should_notify = true;
        *framesRemaining = 0;
    }
    else
    {
        *framesRemaining = 1;
    }
    imx6_nic_ctx.rx_buf_pool[imx6_nic_ctx.num_rx_bufs] = &(rx->dma);
    imx6_nic_ctx.num_rx_bufs++;
    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
/**
 * @param[in] len The size in bytes of the eth frame
 * @return OS_SUCCESS A frame has been enqueued to be send*
 *         OS_ERROR_NOT_INITIALIZED The device hasn't finished initializing.
 *         The call should be retried.
 *         OS_ERROR_INVALID_PARAMETER The length requested is invalid.
 *         OS_ERROR_TRY_AGAIN Frame couldn't be enqueued and has to be sent again
 */
OS_Error_t client_tx_data(size_t * pLen)
{
    if (!imx6_nic_ctx.done_init)
    {
        LOG_ERROR("Device not initialized");
        return OS_ERROR_NOT_INITIALIZED;
    }

    size_t len = *pLen;
    // packet must at least contain dest MAC and source MAC
    if (len < 12)
    {
        ZF_LOGW("invalid packet size %zu", len);
        return OS_ERROR_GENERIC;
    }

    if (len > DMA_BUF_SIZE)
    {
        ZF_LOGW("truncate packet size %zu to max supported %d", len, DMA_BUF_SIZE);
        len = DMA_BUF_SIZE;
    }

    struct eth_driver* eth_driver = imx6_nic_ctx.eth_driver;
    client_t* client = &imx6_nic_ctx.client;

    /* drop packet if TX queue is full */
    if (0 == client->num_tx)
    {
        LOG_ERROR("TX queue is full, dropping packet");
        return OS_ERROR_GENERIC;
    }

    client->num_tx--;
    tx_frame_t* tx_buf = client->pending_tx[client->num_tx];

    /* copy the packet over */
    memcpy(tx_buf->dma.virt, nic_port_from, len);

    /* set source MAC */
    memcpy(
        &((char*)tx_buf->dma.virt)[6],
        client->mac,
        sizeof(client->mac));

    /* queue up transmit */

    int err = eth_driver->i_fn.raw_tx(
                eth_driver,
                1,
                (uintptr_t*)&(tx_buf->dma.phys),
                (unsigned int*)&len,
                tx_buf);

    if (ETHIF_TX_ENQUEUED != err)
    {
        /* TX failed, free internal TX buffer. Client my retry transmission */
        LOG_ERROR("Failed to enqueue tx packet, code %d", err);
        client->num_tx++;
        return OS_ERROR_GENERIC;
    }

    return OS_SUCCESS;

}


//------------------------------------------------------------------------------
OS_Error_t
client_get_mac_address(void)
{
    client_t* client = &imx6_nic_ctx.client;
    memcpy((uint8_t*)nic_port_to, client->mac, sizeof(client->mac));
    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
static int hardware_interface_searcher(
    void*  cookie,
    void*  interface_instance,
    char** properties)
{
    imx6_nic_ctx.eth_driver = interface_instance;
    return PS_INTERFACE_FOUND_MATCH;
}


//------------------------------------------------------------------------------
// Module initialization
//------------------------------------------------------------------------------

// We registered this function via the macro CAMKES_POST_INIT_MODULE_DEFINE(),
// but actually it's called as the last thing in the CAmkES pre_init() handler
// implemented by seL4SingleThreadedComponent.template.c function. This means
// we cannot do much interaction with other components here.
int server_init(
    ps_io_ops_t* io_ops)
{
    /* this eventually calls hardware_interface_searcher(), which will then
     * initialize eth_driver
     */
    int error = ps_interface_find(
        &io_ops->interface_registration_ops,
        PS_ETHERNET_INTERFACE,
        hardware_interface_searcher,
        NULL);
    if (error)
    {
        LOG_ERROR("Unable to find an ethernet device, code %d", error);
        return -1;
    }

    struct eth_driver* eth_driver = imx6_nic_ctx.eth_driver;

    static const struct raw_iface_callbacks ethdriver_callbacks = {
        .tx_complete = eth_tx_complete,
        .rx_complete = eth_rx_complete,
        .allocate_rx_buf = eth_allocate_rx_buf
    };

    eth_driver->cb_cookie = NULL;
    eth_driver->i_cb      = ethdriver_callbacks;

    client_t* client = &imx6_nic_ctx.client;
    client->should_notify = true;

    /* preallocate buffers */
    for (unsigned int i = 0; i < RX_BUFS; i++)
    {
        /* Note that the parameters "cached" and "alignment" of this helper
         * function are in the opposite order than in ps_dma_alloc()
         */
        dma_addr_t dma = dma_alloc_pin(
                            &(io_ops->dma_manager),
                            DMA_BUF_SIZE,
                            1, // cached
                            4); // alignment
        if (!dma.phys) {
            LOG_ERROR("Failed to allocate DMA of size %zu for RX buffer #%d ",
                    DMA_BUF_SIZE, i);
            return -1;
        }
        memset(dma.virt, 0, DMA_BUF_SIZE);
        dma_addr_t* rx = &imx6_nic_ctx.rx_bufs[imx6_nic_ctx.num_rx_bufs];
        *rx = dma;
        imx6_nic_ctx.rx_buf_pool[imx6_nic_ctx.num_rx_bufs] = rx;
        imx6_nic_ctx.num_rx_bufs++;
    }

    for (unsigned int i = 0; i < CLIENT_TX_BUFS; i++)
    {
        /* Note that the parameters "cached" and "alignment" of this helper
         * function are in the opposite order than in ps_dma_alloc()
         */
        dma_addr_t dma = dma_alloc_pin(
                            &(io_ops->dma_manager),
                            DMA_BUF_SIZE,
                            1, // cached
                            4); // alignment
        if (!dma.phys) {
            LOG_ERROR("Failed to allocate DMA of size %zu for TX buffer #%d ",
                    DMA_BUF_SIZE, i);
            return -1;
        }
        memset(dma.virt, 0, DMA_BUF_SIZE);
        tx_frame_t* tx_frame = &client->tx_mem[client->num_tx];
        tx_frame->dma = dma;
        tx_frame->len = DMA_BUF_SIZE;
        client->pending_tx[client->num_tx] = tx_frame;
        client->num_tx++;
    }

    /* get MAC from hardware and remember it */
    uint8_t hw_mac[6];
    eth_driver->i_fn.get_mac(eth_driver, hw_mac);
    memcpy(client->mac, hw_mac, sizeof(client->mac));

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
