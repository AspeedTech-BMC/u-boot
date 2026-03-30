// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 *
 */

#include <asm/io.h>
#include <asm/cache.h>
#include <asm/dma-mapping.h>
#include <clk.h>
#include <common.h>
#include <config.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <fdtdec.h>
#include <log.h>
#include <malloc.h>
#include <reset.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

#include "aspeed_udc.h"

/* number of endpoints on this UDC */
#define UDC_MAX_ENDPOINTS	21
#define EP_DMA_SIZE		2048

/* define to use descriptor mode */
#define AST_EP_DESC_MODE

/* could be 32/256 stages */
#define AST_EP_NUM_OF_DESC	256

/*************************************************************************************/
#define AST_VHUB_CTRL				0x00
#define AST_VHUB_CONF				0x04
#define AST_VHUB_IER				0x08
#define AST_VHUB_ISR				0x0C
#define AST_VHUB_EP_ACK_IER			0x10
#define AST_VHUB_EP_NAK_IER			0x14
#define AST_VHUB_EP_ACK_ISR			0x18
#define AST_VHUB_EP_NAK_ISR			0x1C
#define AST_VHUB_DEV_RESET			0x20
#define AST_VHUB_USB_STS			0x24
#define AST_VHUB_EP_DATA			0x28
#define AST_VHUB_ISO_TX_FAIL			0x2C
#define AST_VHUB_EP0_CTRL			0x30
#define AST_VHUB_EP0_DATA_BUFF			0x34
#define AST_VHUB_EP1_CTRL			0x38
#define AST_VHUB_EP1_STS_CHG			0x3C

#define AST_VHUB_SETUP_DATA0			0x80
#define AST_VHUB_SETUP_DATA1			0x84

/*  ************************************************************************************/
/* AST_VHUB_CTRL				0x00 */
#define ROOT_PHY_CLK_EN				BIT(31)
#define EP_LONG_DESC_MODE			BIT(18)
#define ROOT_PHY_RESET_DIS			BIT(11)
#define ROOT_UPSTREAM_EN			BIT(0)

/* AST_VHUB_ISR					0x0C */
#define ISR_EP_NAK				BIT(17)
#define ISR_EP_ACK_STALL			BIT(16)
#define ISR_DEVICE7				BIT(15)
#define ISR_DEVICE6				BIT(14)
#define ISR_DEVICE5				BIT(13)
#define ISR_DEVICE4				BIT(12)
#define ISR_DEVICE3				BIT(11)
#define ISR_DEVICE2				BIT(10)
#define ISR_DEVICE1				BIT(9)
#define ISR_SUSPEND_RESUME			BIT(8)
#define ISR_BUS_SUSPEND				BIT(7)
#define ISR_BUS_RESET				BIT(6)
#define ISR_HUB_EP1_IN_DATA_ACK			BIT(5)
#define ISR_HUB_EP0_IN_DATA_NAK			BIT(4)
#define ISR_HUB_EP0_IN_ACK_STALL		BIT(3)
#define ISR_HUB_EP0_OUT_NAK			BIT(2)
#define ISR_HUB_EP0_OUT_ACK_STALL		BIT(1)
#define ISR_HUB_EP0_SETUP			BIT(0)
#define ISR_EP0_SETUP_IN_OUT			(ISR_HUB_EP0_SETUP         | \
						 ISR_HUB_EP0_OUT_ACK_STALL | \
						 ISR_HUB_EP0_IN_ACK_STALL)

/* AST_VHUB_USB_STS				0x24 */
#define USB_BUS_HIGH_SPEED			BIT(27)

/* AST_VHUB_EP0_CTRL				0x30 */
#define EP0_GET_RX_LEN(x)			(((x) >> 16) & 0x7f)
#define EP0_TX_LEN(x)				(((x) & 0x7f) << 8)
#define EP0_RX_BUFF_RDY				BIT(2)
#define EP0_TX_BUFF_RDY				BIT(1)
#define EP0_STALL				BIT(0)

#define AST_UDC_DEV_CTRL			0x00
#define AST_UDC_DEV_ISR				0x04
#define AST_UDC_DEV_EP0_CTRL			0x08
#define AST_UDC_DEV_EP0_DATA_BUFF		0x0C

/*************************************************************************************/
#define AST_EP_BASE				0x200
#define AST_EP_OFFSET				0x10
#define AST_EP_CONFIG				0x00
#define AST_EP_DMA_CTRL				0x04
#define AST_EP_DMA_BUFF				0x08
#define AST_EP_DMA_STS				0x0C

/*************************************************************************************/
/* AST_EP_CONFIG				0x00 */
#define EP_SET_MAX_PKT(x)			(((x) & 0x3ff) << 16)
#define EP_SET_EP_STALL				BIT(12)
#define EP_SET_EP_NUM(x)			(((x) & 0xf) << 8)
#define EP_TYPE_BULK_IN				(0x2 << 4)
#define EP_TYPE_BULK_OUT			(0x3 << 4)
#define EP_TYPE_INT_IN				(0x4 << 4)
#define EP_TYPE_INT_OUT				(0x5 << 4)
#define EP_TYPE_ISO_IN				(0x6 << 4)
#define EP_TYPE_ISO_OUT				(0x7 << 4)
#define EP_ENABLE				BIT(0)

/* AST_EP_DMA_CTRL				0x04 */
#define EP_DMA_IN_LONG_MODE			BIT(3)
#define EP_RESET_DESC_OP			BIT(2)
#define EP_SINGLE_DESC_MODE			BIT(1)
#define EP_DESC_OP_ENABLE			BIT(0)

/* AST_EP_DMA_STS				0x0C */
#define AST_EP_TX_DATA_BYTE(x)			(((x) & 0x3ff) << 16)
#define AST_EP_START_TRANS			BIT(0)

/* Desc W1 IN */
#define VHUB_DSC1_IN_INTERRUPT			BIT(31)
#define VHUB_DSC1_IN_SPID_DATA0			(0 << 14)
#define VHUB_DSC1_IN_SPID_DATA2			BIT(14)
#define VHUB_DSC1_IN_SPID_DATA1			(2 << 14)
#define VHUB_DSC1_IN_SPID_MDATA			(3 << 14)
#define VHUB_DSC1_IN_SET_LEN(x)			((x) & 0xfff)
#define VHUB_DSC1_IN_LEN(x)			((x) & 0xfff)
#define VHUB_DSC1_OUT_LEN(x)			((x) & 0x7ff)

#define AST_SETUP_DEBUG
#define AST_UDC_DEBUG
#define AST_EP_DEBUG

#ifdef AST_SETUP_DEBUG
#define SETUP_DBG(fmt, args...) pr_debug("%s() " fmt, __func__, ##args)
#else
#define SETUP_DBG(fmt, args...)
#endif

#ifdef AST_UDC_DEBUG
#define UDC_DBG(fmt, args...) pr_debug("%s() " fmt, __func__, ##args)
#else
#define UDC_DBG(fmt, args...)
#endif

#ifdef AST_EP_DEBUG
#define EP_DBG(fmt, args...) pr_debug("%s() " fmt, __func__, ##args)
#else
#define EP_DBG(fmt, args...)
#endif

static void aspeed_udc_done(struct aspeed_udc_ep *ep,
			    struct aspeed_udc_request *req, int status)
{
	EP_DBG("%s len: (%d/%d) buf: %p, dir: %s\n",
	       ep->ep.name, req->req.actual, req->req.length, req->req.buf,
	       ep->ep_dir ? "IN" : "OUT");

	list_del(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		EP_DBG("%s done %p, status %d\n", ep->ep.name, req, status);

	usb_gadget_giveback_request(&ep->ep, &req->req);
}

void ast_udc_ep0_data_tx(struct aspeed_udc_priv *udc, u8 *tx_data, u32 len)
{
	void __iomem *reg = udc->udc_base;

	if (len) {
		memcpy(udc->ep0_ctrl_buf, tx_data, len);

		writel(udc->ep0_ctrl_dma, reg + AST_VHUB_EP0_DATA_BUFF);
		writel(EP0_TX_LEN(len), reg + AST_VHUB_EP0_CTRL);
		writel(EP0_TX_LEN(len) | EP0_TX_BUFF_RDY,
		       reg + AST_VHUB_EP0_CTRL);

		udc->is_udc_control_tx = 1;

	} else {
		writel(EP0_TX_BUFF_RDY, reg + AST_VHUB_EP0_CTRL);
	}
}

static void aspeed_udc_getstatus(struct aspeed_udc_priv *udc)
{
	void __iomem *reg = udc->udc_base;
	uint16_t wIndex = le16_to_cpu(udc->root_setup->wIndex);
	u32 status = 0;
	int epnum;

	switch (udc->root_setup->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		/* Get device status */
		status = 1 << USB_DEVICE_SELF_POWERED;
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		epnum = wIndex & USB_ENDPOINT_NUMBER_MASK;
		status = udc->ep[epnum].stopped;
		break;
	default:
		goto stall;
	}

	EP_DBG("%s: response status: %d\n", __func__, status);
	ast_udc_ep0_data_tx(udc, (u8 *)&status, sizeof(status));

	return;

stall:
	dev_err(udc->dev, "Can't respond to %s request\n", __func__);
	writel(readl(reg + AST_VHUB_EP0_CTRL) | EP0_STALL,
	       reg + AST_VHUB_EP0_CTRL);
}

static void aspeed_udc_nuke(struct aspeed_udc_ep *ep, int status)
{
	// TODO: drop since always true
//	if (!&ep->queue)
//		return;

	while (!list_empty(&ep->queue)) {
		struct aspeed_udc_request *req;

		req = list_entry(ep->queue.next, struct aspeed_udc_request,
				 queue);
		aspeed_udc_done(ep, req, status);
	}
}

static void aspeed_udc_setup_handle(struct aspeed_udc_priv *udc)
{
	u8 bRequestType = udc->root_setup->bRequestType;
	u8 bRequest = udc->root_setup->bRequest;
	uint16_t wValue = le16_to_cpu(udc->root_setup->wValue);
	uint16_t wIndex = le16_to_cpu(udc->root_setup->wIndex);
	uint16_t ep_num = wIndex & USB_ENDPOINT_NUMBER_MASK;
	struct aspeed_udc_request *req;
	void __iomem *base = udc->udc_base;
	int i = 0;
	int ret;

	SETUP_DBG("%s: %x, %s: %x, %s: %x, %s: %x, %s: %d\n",
		  "bRequestType", bRequestType, "bRequest", bRequest,
		  "wValue", wValue,
		  "wIndex", wIndex,
		  "wLength", udc->root_setup->wLength);

	list_for_each_entry(req, &udc->ep[0].queue, queue) {
		i++;
		dev_err(udc->dev, "[%d] there is req %lx in ep0 queue\n", i, (uintptr_t)req);
	}

	aspeed_udc_nuke(&udc->ep[0], -ETIMEDOUT);

	udc->ep[0].ep_dir = bRequestType & USB_DIR_IN;

	if ((bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (bRequest) {
		case USB_REQ_SET_ADDRESS:
			SETUP_DBG("Set addr: %x\n", wValue);

			if (readl(base + AST_VHUB_USB_STS) &
			    USB_BUS_HIGH_SPEED)
				udc->gadget.speed = USB_SPEED_HIGH;
			else
				udc->gadget.speed = USB_SPEED_FULL;

			writel(wValue, base + AST_VHUB_CONF);
			writel(0, base + AST_VHUB_EP0_DATA_BUFF);
			writel(EP0_TX_BUFF_RDY, base + AST_VHUB_EP0_CTRL);
			break;

		case USB_REQ_CLEAR_FEATURE:
			SETUP_DBG("USB_REQ_CLEAR_FEATURE ep: %d\n", ep_num);
			writel(EP0_TX_BUFF_RDY, base + AST_VHUB_EP0_CTRL);
			break;

		case USB_REQ_SET_FEATURE:
			SETUP_DBG("USB_REQ_SET_FEATURE ep: %d\n", ep_num);
			break;

		case USB_REQ_GET_STATUS:
			SETUP_DBG("USB_REQ_GET_STATUS\n");
			aspeed_udc_getstatus(udc);
			break;

		default:
			spin_unlock(&udc->lock);
			ret = udc->gadget_driver->setup(&udc->gadget,
				udc->root_setup);
			spin_lock(&udc->lock);
			if (ret < 0)
				dev_err(udc->dev, "Gadget setup failed, ret: 0x%x\n",
					ret);
			break;
		}

	} else {
		SETUP_DBG("non-standard request type\n");
		spin_unlock(&udc->lock);
		ret = udc->gadget_driver->setup(&udc->gadget, udc->root_setup);
		spin_lock(&udc->lock);
		if (ret < 0)
			dev_err(udc->dev, "Gadget setup failed, ret: 0x%x\n", ret);
	}
}

static void aspeed_udc_ep0_queue(struct aspeed_udc_ep *ep,
				 struct aspeed_udc_request *req)
{
	struct aspeed_udc_priv *udc = ep->udc;
	u16 tx_len;
	void __iomem *reg;

	if ((req->req.length - req->req.actual) > ep->ep.maxpacket)
		tx_len = ep->ep.maxpacket;
	else
		tx_len = req->req.length - req->req.actual;

	writel(req->req.dma + req->req.actual,
	       udc->udc_base + AST_VHUB_EP0_DATA_BUFF);

	SETUP_DBG("ep0 REQ buf: %lx, dma: %llx , txlen: %d (%d/%d) ,dir: %s\n",
		  (uintptr_t)req->req.buf, req->req.dma + req->req.actual,
		  tx_len, req->req.actual, req->req.length,
		  ep->ep_dir ? "IN" : "OUT");

	reg = udc->udc_base + AST_VHUB_EP0_CTRL;
	if (ep->ep_dir) {
		req->req.actual += tx_len;
		writel(EP0_TX_LEN(tx_len), reg);
		writel(EP0_TX_LEN(tx_len) | EP0_TX_BUFF_RDY, reg);

	} else {
		if (!req->req.length) {
			writel(EP0_TX_BUFF_RDY, reg);
			ep->ep_dir = 0x80;
		} else {
			writel(EP0_RX_BUFF_RDY, reg);
		}
	}
}

static void aspeed_udc_ep0_rx(struct aspeed_udc_priv *udc)
{
	writel(udc->ep0_ctrl_dma, udc->udc_base + AST_VHUB_EP0_DATA_BUFF);
	writel(EP0_RX_BUFF_RDY, udc->udc_base + AST_VHUB_EP0_CTRL);
}

static void aspeed_udc_ep0_tx(struct aspeed_udc_priv *udc)
{
	writel(udc->ep0_ctrl_dma, udc->udc_base + AST_VHUB_EP0_DATA_BUFF);
	writel(EP0_TX_BUFF_RDY, udc->udc_base + AST_VHUB_EP0_CTRL);
}

static void aspeed_udc_ep0_in(struct aspeed_udc_priv *udc)
{
	struct aspeed_udc_ep *ep = &udc->ep[0];
	struct aspeed_udc_request *req;

	if (list_empty(&ep->queue)) {
		if (udc->is_udc_control_tx) {
			SETUP_DBG("is_udc_control_tx\n");
			aspeed_udc_ep0_rx(udc);
			udc->is_udc_control_tx = 0;
		}
		return;

	} else {
		req = list_entry(ep->queue.next, struct aspeed_udc_request,
				 queue);
	}

	SETUP_DBG("req=%lx (%d/%d)\n", (uintptr_t)req, req->req.length,
		  req->req.actual);

	if (req->req.length == req->req.actual) {
		if (req->req.length)
			aspeed_udc_ep0_rx(udc);

		if (ep->ep_dir)
			aspeed_udc_done(ep, req, 0);

	} else {
		aspeed_udc_ep0_queue(ep, req);
	}
}

void aspeed_udc_ep0_out(struct aspeed_udc_priv *udc)
{
	struct aspeed_udc_ep *ep = &udc->ep[0];
	struct aspeed_udc_request *req;
	u16 rx_len;

	rx_len = EP0_GET_RX_LEN(readl(udc->udc_base + AST_VHUB_EP0_CTRL));

	if (list_empty(&ep->queue))
		return;

	req = list_entry(ep->queue.next, struct aspeed_udc_request,
			 queue);

	req->req.actual += rx_len;

	SETUP_DBG("req %lx (%d/%d)\n", (uintptr_t)req, req->req.length,
		  req->req.actual);

	if (rx_len < ep->ep.maxpacket ||
	    req->req.actual == req->req.length) {
		aspeed_udc_ep0_tx(udc);
		if (!ep->ep_dir)
			aspeed_udc_done(ep, req, 0);

	} else {
		ep->ep_dir = 0;
		aspeed_udc_ep0_queue(ep, req);
	}
}

static int aspeed_dma_descriptor_setup(struct aspeed_udc_ep *ep,
				       unsigned int dma_address, u32 tx_len,
				       struct aspeed_udc_request *req)
{
	u32 packet_len;
	u32 chunk;
	int i;

	if (!ep->dma_desc_list) {
		printf("%s: %s %s\n", __func__, ep->ep.name,
		       "failed due to empty DMA descriptor list");
		return -1;
	}

	packet_len = tx_len;
	chunk = ep->chunk_max;
	i = 0;
	while (packet_len > 0) {
		EP_DBG("%s: %s:%d, %s:0x%x, %s:%d %s:%d (%s:0x%x)\n",
		       ep->ep.name,
		       "wptr", ep->dma_desc_list_wptr,
		       "dma_address", dma_address,
		       "packet_len", packet_len,
		       "chunk", chunk,
		       "tx_len", tx_len);

		ep->dma_desc_list[ep->dma_desc_list_wptr].des_0 =
			(dma_address + (i * chunk));

		/* last packet */
		if (packet_len <= chunk)
			ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
				packet_len | VHUB_DSC1_IN_INTERRUPT;
		else
			ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
				chunk;

		EP_DBG("wptr:%d, req:%lx, dma_desc_list 0x%x 0x%x\n",
		       ep->dma_desc_list_wptr, (uintptr_t)req,
		       ep->dma_desc_list[ep->dma_desc_list_wptr].des_0,
		       ep->dma_desc_list[ep->dma_desc_list_wptr].des_1);

		if (i == 0)
			req->saved_dma_wptr = ep->dma_desc_list_wptr;

		ep->dma_desc_list_wptr++;
		i++;
		if (ep->dma_desc_list_wptr >= AST_EP_NUM_OF_DESC)
			ep->dma_desc_list_wptr = 0;
		if (packet_len >= chunk)
			packet_len -= chunk;
		else
			break;
	}

	if (req->req.zero)
		printf("TODO: Send an extra zero length packet\n");

	return 0;
}

static void aspeed_udc_ep_dma_desc_mode(struct aspeed_udc_ep *ep,
					struct aspeed_udc_request *req)
{
	u32 max_req_size;
	u32 dma_conf;
	u32 tx_len;
	int ret;

	max_req_size = ep->chunk_max * (AST_EP_NUM_OF_DESC - 1);

	if ((req->req.length - req->req.actual) > max_req_size)
		tx_len = max_req_size;
	else
		tx_len = req->req.length - req->req.actual;

	EP_DBG("%s: req(0x%lx) dma:0x%llx, len:0x%x, actual:0x%x, %s:%d, %s:%x\n",
	       ep->ep.name, (uintptr_t)req, req->req.dma, req->req.length,
	       req->req.actual, "tx_len", tx_len,
	       "dir", ep->ep_dir);

	if ((req->req.dma % 4) != 0) {
		printf("Not supported=> 1: %s : %llx len (%d/%d) dir %x\n",
		       ep->ep.name, req->req.dma, req->req.actual,
		       req->req.length, ep->ep_dir);
	} else {
		writel(EP_RESET_DESC_OP, ep->ep_base + AST_EP_DMA_CTRL);
		ret = aspeed_dma_descriptor_setup(ep, req->req.dma +
						  req->req.actual,
						  tx_len, req);
		if (!ret)
			req->actual_dma_length += tx_len;

		writel(ep->dma_desc_dma_handle, ep->ep_base + AST_EP_DMA_BUFF);
		dma_conf = EP_DESC_OP_ENABLE;
		if (ep->ep_dir)
			dma_conf |= EP_DMA_IN_LONG_MODE;
		writel(dma_conf, ep->ep_base + AST_EP_DMA_CTRL);

		writel(ep->dma_desc_list_wptr,
		       ep->ep_base + AST_EP_DMA_STS);
	}
}

static void aspeed_udc_ep_dma(struct aspeed_udc_ep *ep,
			      struct aspeed_udc_request *req)
{
	u16 tx_len;

	if ((req->req.length - req->req.actual) > ep->ep.maxpacket)
		tx_len = ep->ep.maxpacket;
	else
		tx_len = req->req.length - req->req.actual;

	EP_DBG("req(0x%lx) dma: 0x%llx, length: 0x%x, actual: 0x%x\n",
	       (uintptr_t)req, req->req.dma, req->req.length, req->req.actual);

	EP_DBG("%s: len: %d, dir(0x%x): %s\n",
	       ep->ep.name, tx_len, ep->ep_dir,
	       ep->ep_dir ? "IN" : "OUT");

	writel(req->req.dma + req->req.actual, ep->ep_base + AST_EP_DMA_BUFF);
	writel(AST_EP_TX_DATA_BYTE(tx_len), ep->ep_base + AST_EP_DMA_STS);
	writel(AST_EP_TX_DATA_BYTE(tx_len) | AST_EP_START_TRANS,
	       ep->ep_base + AST_EP_DMA_STS);
}

static int aspeed_udc_ep_enable(struct usb_ep *_ep,
				const struct usb_endpoint_descriptor *desc)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	u16 nr_trans = ((usb_endpoint_maxp(desc) >> 11) & 3) + 1;
	u16 maxpacket = usb_endpoint_maxp(desc) & 0x7ff;
	u8 epnum = usb_endpoint_num(desc);
	u32 ep_conf = 0;
	u8 dir_in;
	u8 type;

	EP_DBG("%s, set ep #%d, maxpacket %d ,wmax %d trans:%d\n", ep->ep.name,
	       epnum, maxpacket, le16_to_cpu(desc->wMaxPacketSize), nr_trans);

	if (!_ep || !ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		printf("bad ep or descriptor %s %d, %s: %d, %s: %d\n",
		       _ep->name, desc->bDescriptorType,
		       "maxpacket", maxpacket,
		       "ep maxpacket", ep->ep.maxpacket);
		return -EINVAL;
	}

	ep->ep.desc = desc;
	ep->stopped = 0;
	ep->ep.maxpacket = maxpacket;

	if (maxpacket > 1024) {
		printf("maxpacket is out-of-range: 0x%x\n", maxpacket);
		maxpacket = 1024;
	}

	if (maxpacket == 1024)
		ep_conf = 0;
	else
		ep_conf = EP_SET_MAX_PKT(maxpacket);

	ep_conf |= EP_SET_EP_NUM(epnum);

	type = usb_endpoint_type(desc);
	dir_in = usb_endpoint_dir_in(desc);
	ep->ep_dir = dir_in;

	ep->chunk_max = ep->ep.maxpacket;
	if (ep->ep_dir) {
		ep->chunk_max <<= 3;
		while (ep->chunk_max > 4095)
			ep->chunk_max -= ep->ep.maxpacket;
	}

	EP_DBG("epnum %d, type %d, dir_in %d\n", epnum, type, dir_in);
	switch (type) {
	case USB_ENDPOINT_XFER_ISOC:
		if (dir_in)
			ep_conf |= EP_TYPE_ISO_IN;
		else
			ep_conf |= EP_TYPE_ISO_OUT;
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (dir_in)
			ep_conf |= EP_TYPE_BULK_IN;
		else
			ep_conf |= EP_TYPE_BULK_OUT;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (dir_in)
			ep_conf |= EP_TYPE_INT_IN;
		else
			ep_conf |= EP_TYPE_INT_OUT;
		break;
	}

	writel(EP_RESET_DESC_OP, ep->ep_base + AST_EP_DMA_CTRL);
	writel(EP_SINGLE_DESC_MODE, ep->ep_base + AST_EP_DMA_CTRL);
	writel(0, ep->ep_base + AST_EP_DMA_STS);

	writel(ep_conf | EP_ENABLE, ep->ep_base + AST_EP_CONFIG);

	EP_DBG("read ep %d seting: 0x%08X\n", epnum,
	       readl(ep->ep_base + AST_EP_CONFIG));

	return 0;
}

static int aspeed_udc_ep_disable(struct usb_ep *_ep)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);

	EP_DBG("%s\n", _ep->name);

	ep->ep.desc = NULL;
	ep->stopped = 1;
	writel(0, ep->ep_base + AST_EP_CONFIG);

	return 0;
}

static struct usb_request *aspeed_udc_ep_alloc_request(struct usb_ep *_ep,
						       gfp_t gfp_flags)
{
	struct aspeed_udc_request *req;

	EP_DBG("%s\n", _ep->name);
	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static void aspeed_udc_ep_free_request(struct usb_ep *_ep,
				       struct usb_request *_req)
{
	struct aspeed_udc_request *req;

	EP_DBG("%s\n", _ep->name);
	req = container_of(_req, struct aspeed_udc_request, req);
	kfree(req);
}

static int aspeed_udc_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			       gfp_t gfp_flags)
{
	struct aspeed_udc_request *req = req_to_aspeed_udc_req(_req);
	struct aspeed_udc_ep *ep = ep_to_aspeed_udc_ep(_ep);
	struct aspeed_udc_priv *udc = ep->udc;
	unsigned long flags = 0;

	if (unlikely(!_req || !_req->complete || !_req->buf || !_ep))
		return -EINVAL;

	if (ep->stopped) {
		printf("%s : is stop\n", _ep->name);
		return -1;
	}

	spin_lock_irqsave(&udc->lock, flags);

	list_add_tail(&req->queue, &ep->queue);

	req->actual_dma_length = 0;
	req->req.actual = 0;
	req->req.status = -EINPROGRESS;

	if (usb_gadget_map_request(&udc->gadget, &req->req, ep->ep_dir)) {
		printf("Map request failed\n");
		return -1;
	}

	EP_DBG("%s: req(0x%lx) dma:0x%llx, len:%d, actual:0x%x\n",
	       ep->name, (uintptr_t)req, req->req.dma,
	       req->req.length, req->req.actual);

	if (!ep->ep.desc) { /* ep0 */
		if ((req->req.dma % 4) != 0) {
			printf("ep0 request dma is not 4 bytes align\n");
			return -1;
		}
		aspeed_udc_ep0_queue(ep, req);

	} else {
		if (list_is_singular(&ep->queue)) {
			if (udc->desc_mode)
				aspeed_udc_ep_dma_desc_mode(ep, req);
			else
				aspeed_udc_ep_dma(ep, req);
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int aspeed_udc_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct aspeed_udc_ep *ep = ep_to_aspeed_udc_ep(_ep);
	struct aspeed_udc_request *req;

	EP_DBG("%s\n", _ep->name);

	if (!_ep)
		return -EINVAL;

	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init(&req->queue);
			_req->status = -ECONNRESET;
			break;
		}
	}
	if (&req->req != _req) {
		printf("Cannot find REQ in ep queue\n");
		return -EINVAL;
	}

	aspeed_udc_done(ep, req, -ESHUTDOWN);

	return 0;
}

static int aspeed_udc_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct aspeed_udc_ep *ep = ep_to_aspeed_udc_ep(_ep);
	struct aspeed_udc_priv *udc = ep->udc;
	void __iomem *reg;
	u32 val;

	EP_DBG("%s: %d\n", _ep->name, value);
	if (!_ep)
		return -EINVAL;

	if (!strncmp(_ep->name, "ep0", 3)) {
		reg = udc->udc_base + AST_VHUB_EP0_CTRL;
		if (value)
			val = readl(reg) | EP0_STALL;
		else
			val = readl(reg) & ~EP0_STALL;

	} else {
		reg = ep->ep_base + AST_EP_CONFIG;
		if (value)
			val = readl(reg) | EP_SET_EP_STALL;
		else
			val = readl(reg) & ~EP_SET_EP_STALL;

		ep->stopped = value ? 1 : 0;
	}

	writel(val, reg);

	return 0;
}

static void aspeed_udc_ep_handle_desc_mode(struct aspeed_udc_priv *udc,
					   u16 ep_num)
{
	struct aspeed_udc_ep *ep = &udc->ep[ep_num];
	struct aspeed_udc_request *req;
	u32 processing_status;
	u32 wr_ptr, rd_ptr;
	u16 total_len = 0;
	u16 len_in_desc;
	u16 len;
	int i;

	if (list_empty(&ep->queue)) {
		dev_err(udc->dev, "%s ep req queue is empty!!!\n", ep->ep.name);
		return;
	}

	EP_DBG("%s handle\n", ep->ep.name);

	req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);

	processing_status = (readl(ep->ep_base + AST_EP_DMA_CTRL) >> 4) & 0xf;
	if (processing_status != 0 && processing_status != 8) {
		dev_err(udc->dev, "Desc process status: 0x%x\n", processing_status);
		return;
	}

	rd_ptr = (readl(ep->ep_base + AST_EP_DMA_STS) >> 8) & 0xFF;
	wr_ptr = (readl(ep->ep_base + AST_EP_DMA_STS)) & 0xFF;

	EP_DBG("req(0x%lx) length: 0x%x, actual: 0x%x, rd_ptr:%d, wr_ptr:%d\n",
	       (uintptr_t)req, req->req.length, req->req.actual, rd_ptr, wr_ptr);

	if (rd_ptr != wr_ptr) {
		dev_err(udc->dev, "%s: Desc is not empy. %s:%d,  %s:%d\n",
			__func__, "rd_ptr", rd_ptr, "wr_ptr", wr_ptr);
		return;
	}

	i = req->saved_dma_wptr;
	do {
		if (ep->ep_dir)
			len_in_desc =
				VHUB_DSC1_IN_LEN(ep->dma_desc_list[i].des_1);
		else
			len_in_desc =
				VHUB_DSC1_OUT_LEN(ep->dma_desc_list[i].des_1);
		total_len += len_in_desc;
		i++;
		if (i >= AST_EP_NUM_OF_DESC)
			i = 0;
	} while (i != wr_ptr);

	len = total_len;
	req->req.actual += len;

	EP_DBG("%s: total transfer len:0x%x\n", ep->ep.name, len);

	if (req->req.length <= req->req.actual || len < ep->ep.maxpacket) {
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);
		if ((req->req.dma % 4) != 0) {
			dev_err(udc->dev, "Not supported in desc_mode\n");
			return;
		}

		aspeed_udc_done(ep, req, 0);

		if (!list_empty(&ep->queue)) {
			req = list_first_entry(&ep->queue,
					       struct aspeed_udc_request,
					       queue);

			EP_DBG("%s: next req(0x%lx) dma 0x%llx\n", ep->ep.name,
			       (uintptr_t)req, req->req.dma);

			if (req->actual_dma_length == req->req.actual)
				aspeed_udc_ep_dma_desc_mode(ep, req);
			else
				EP_DBG("%s: skip req(0x%lx) dma(%d %d)\n",
				       ep->ep.name, (uintptr_t)req,
				       req->actual_dma_length,
				       req->req.actual);
		}

	} else {
		EP_DBG("%s: not done, keep trigger dma\n", ep->ep.name);
		if (req->actual_dma_length == req->req.actual)
			aspeed_udc_ep_dma_desc_mode(ep, req);
		else
			EP_DBG("%s: skip req(0x%lx) dma (%d %d)\n",
			       ep->ep.name, (uintptr_t)req,
			       req->actual_dma_length,
			       req->req.actual);
	}

	EP_DBG("%s exits\n", ep->ep.name);
}

static void aspeed_udc_ep_handle(struct aspeed_udc_priv *udc, u16 ep_num)
{
	struct aspeed_udc_ep *ep = &udc->ep[ep_num];
	struct aspeed_udc_request *req;
	u16 len = 0;

	EP_DBG("%s handle\n", ep->ep.name);

	if (list_empty(&ep->queue))
		return;

	req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);
	len = (readl(ep->ep_base + AST_EP_DMA_STS) >> 16) & 0x7ff;

	EP_DBG("%s req: length: 0x%x, actual: 0x%x, len: 0x%x\n",
	       ep->ep.name, req->req.length, req->req.actual, len);

	req->req.actual += len;

	if (req->req.length == req->req.actual || len < ep->ep.maxpacket) {
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);

		aspeed_udc_done(ep, req, 0);
		if (!list_empty(&ep->queue)) {
			req = list_first_entry(&ep->queue,
					       struct aspeed_udc_request,
					       queue);
			aspeed_udc_ep_dma(ep, req);
		}

	} else {
		aspeed_udc_ep_dma(ep, req);
	}
}

static void aspeed_udc_isr(struct aspeed_udc_priv *udc)
{
	void __iomem *base = udc->udc_base;
	u32 isr, val;
	u32 ep_isr;
	unsigned long flags;
	int i;
	uint8_t ret;

	spin_lock_irqsave(&udc->lock, flags);
	isr = readl(base + AST_VHUB_ISR);
	isr &= 0x3ffff;
	if (!isr)
		return;
	writel(isr, base + AST_VHUB_ISR);
	UDC_DBG("%s: isr: 0x%x\n", __func__, isr);
	ret = readl_poll_timeout(base + AST_VHUB_ISR, val,
				 (((val & ISR_EP0_SETUP_IN_OUT) & isr) == 0x0),
				 100);
	if (ret)
		printf("VHUB_ISR clear failed: wrote=0x%x, remain=0x%x\n", isr, val);

	if (isr & ISR_BUS_RESET) {
		UDC_DBG("ISR_BUS_RESET\n");
	}

	if (isr & ISR_BUS_SUSPEND) {
		UDC_DBG("ISR_BUS_SUSPEND\n");
	}

	if (isr & ISR_SUSPEND_RESUME) {
		UDC_DBG("ISR_SUSPEND_RESUME\n");
	}

	if (isr & ISR_HUB_EP0_IN_ACK_STALL) {
		UDC_DBG("ISR_HUB_EP0_IN_ACK_STALL\n");
		aspeed_udc_ep0_in(udc);
	}

	if (isr & ISR_HUB_EP0_OUT_ACK_STALL) {
		UDC_DBG("ISR_HUB_EP0_OUT_ACK_STALL\n");
		aspeed_udc_ep0_out(udc);
	}

	if (isr & ISR_HUB_EP0_OUT_NAK) {
		UDC_DBG("ISR_HUB_EP0_OUT_NAK\n");
	}

	if (isr & ISR_HUB_EP0_IN_DATA_NAK) {
		UDC_DBG("ISR_HUB_EP0_IN_DATA_NAK\n");
	}

	if (isr & ISR_HUB_EP0_SETUP) {
		UDC_DBG("SETUP\n");
		aspeed_udc_setup_handle(udc);
	}

	if (isr & ISR_HUB_EP1_IN_DATA_ACK) {
		// HUB Bitmap control
		dev_err(udc->dev, "Error: EP1 IN ACK\n");
		writel(0x00, base + AST_VHUB_EP1_STS_CHG);
	}

	if (isr & ISR_EP_ACK_STALL) {
		ep_isr = readl(base + AST_VHUB_EP_ACK_ISR);
		UDC_DBG("ISR_EP_ACK_STALL, ep_ack_isr: 0x%x\n", ep_isr);

		for (i = 0; i < UDC_MAX_ENDPOINTS; i++) {
			if (ep_isr & (0x1 << i)) {
				writel(BIT(i), base + AST_VHUB_EP_ACK_ISR);
				if (udc->desc_mode)
					aspeed_udc_ep_handle_desc_mode(udc, i);
				else
					aspeed_udc_ep_handle(udc, i);
			}
		}
	}

	if (isr & ISR_EP_NAK) {
		UDC_DBG("ISR_EP_NAK\n");
	}

	spin_unlock_irqrestore(&udc->lock, flags);
}

static const struct usb_ep_ops aspeed_udc_ep_ops = {
	.enable = aspeed_udc_ep_enable,
	.disable = aspeed_udc_ep_disable,
	.alloc_request = aspeed_udc_ep_alloc_request,
	.free_request = aspeed_udc_ep_free_request,
	.queue = aspeed_udc_ep_queue,
	.dequeue = aspeed_udc_ep_dequeue,
	.set_halt = aspeed_udc_ep_set_halt,
};

static void aspeed_udc_ep_init(struct aspeed_udc_priv *udc)
{
	struct aspeed_udc_ep *ep;
	int i;

	for (i = 0; i < UDC_MAX_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		snprintf(ep->name, sizeof(ep->name), "ep%d", i);
		ep->ep.name = ep->name;
		ep->ep.ops = &aspeed_udc_ep_ops;

		if (i) {
			ep->ep_buf = udc->ep0_ctrl_buf + (i * EP_DMA_SIZE);
			ep->ep_dma = udc->ep0_ctrl_dma + (i * EP_DMA_SIZE);
			usb_ep_set_maxpacket_limit(&ep->ep, 1024);
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

			/* allocate endpoint descrptor list (Note: must be DMA memory) */
			if (udc->desc_mode) {
				ep->dma_desc_list =
					dma_alloc_coherent(AST_EP_NUM_OF_DESC *
						sizeof(struct aspeed_ep_desc),
						(unsigned long *)
						&ep->dma_desc_dma_handle
						);
				ep->dma_desc_list_wptr = 0;
			}

		} else {
			usb_ep_set_maxpacket_limit(&ep->ep, 64);
		}

		ep->ep_base = udc->udc_base + AST_EP_BASE + (AST_EP_OFFSET * i);
		ep->udc = udc;

		INIT_LIST_HEAD(&ep->queue);
	}
}

static int aspeed_gadget_getframe(struct usb_gadget *gadget)
{
	struct aspeed_udc_priv *udc = gadget_to_aspeed_udc(gadget);

	return (readl(udc->udc_base + AST_VHUB_USB_STS) >> 16) & 0x7ff;
}

static int aspeed_gadget_wakeup(struct usb_gadget *gadget)
{
	UDC_DBG("TODO\n");
	return 0;
}

/*
 * activate/deactivate link with host; minimize power usage for
 * inactive links by cutting clocks and transceiver power.
 */
static int aspeed_gadget_pullup(struct usb_gadget *gadget, int is_on)
{
	struct aspeed_udc_priv *udc = gadget_to_aspeed_udc(gadget);
	void __iomem *reg = udc->udc_base + AST_VHUB_CTRL;

	UDC_DBG("is_on: %d\n", is_on);

	if (is_on)
		writel(readl(reg) | ROOT_UPSTREAM_EN, reg);
	else
		writel(readl(reg) & ~ROOT_UPSTREAM_EN, reg);

	return 0;
}

static int aspeed_gadget_start(struct usb_gadget *gadget,
			       struct usb_gadget_driver *driver)
{
	struct aspeed_udc_priv *udc = gadget_to_aspeed_udc(gadget);

	if (!udc)
		return -ENODEV;

	udc->gadget_driver = driver;

	return 0;
}

static int aspeed_gadget_stop(struct usb_gadget *gadget)
{
	struct aspeed_udc_priv *udc = gadget_to_aspeed_udc(gadget);
	void __iomem *reg = udc->udc_base + AST_VHUB_CTRL;

	writel(readl(reg) & ~ROOT_UPSTREAM_EN, reg);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget_driver = NULL;

	usb_gadget_set_state(&udc->gadget, USB_STATE_NOTATTACHED);

	return 0;
}

static const struct usb_gadget_ops aspeed_gadget_ops = {
	.get_frame		= aspeed_gadget_getframe,
	.wakeup			= aspeed_gadget_wakeup,
	.pullup			= aspeed_gadget_pullup,
	.udc_start		= aspeed_gadget_start,
	.udc_stop		= aspeed_gadget_stop,
};

int dm_usb_gadget_handle_interrupts(struct udevice *dev)
{
	struct aspeed_udc_priv *udc = dev_get_priv(dev);

	aspeed_udc_isr(udc);

	return 0;
}

static int udc_init(struct aspeed_udc_priv *udc)
{
	void __iomem *base;
	u32 val;

	if (!udc) {
		dev_err(udc->dev, "Error: udc driver is not init yet");
		return -1;
	}

	base = udc->udc_base;

	writel(ROOT_PHY_CLK_EN | ROOT_PHY_RESET_DIS, base + AST_VHUB_CTRL);

	writel(1, base + AST_VHUB_DEV_RESET);
	udelay(1);
	writel(0, base + AST_VHUB_DEV_RESET);

	val = ~(u32)BIT(18);
	writel(val, base + AST_VHUB_ISR);
	writel(val, base + AST_VHUB_IER);

	val = ~(u32)BIT(UDC_MAX_ENDPOINTS);
	writel(val, base + AST_VHUB_EP_ACK_ISR);
	writel(val, base + AST_VHUB_EP_ACK_IER);

	writel(0, base + AST_VHUB_EP0_CTRL);
	writel(0, base + AST_VHUB_EP1_CTRL);

#ifdef AST_EP_DESC_MODE
	if (AST_EP_NUM_OF_DESC == 256)
		writel(readl(base + AST_VHUB_CTRL) | EP_LONG_DESC_MODE,
		       base + AST_VHUB_CTRL);
#endif

	return 0;
}

static int aspeed_udc_probe(struct udevice *dev)
{
	struct aspeed_udc_priv *udc = dev_get_priv(dev);
	int ret;

	dev_info(dev, "Start aspeed udc...\n");

	reset_assert(&udc->reset);

#if CONFIG_IS_ENABLED(CLK)
	ret = clk_enable(&udc->clk);
	if (ret) {
		dev_err(dev, "%s: clk_enable failed\n", __func__);
		return -EINVAL;
	}
#endif

	// Wait 10ms for PLL locking
	mdelay(10);
	reset_deassert(&udc->reset);
	// HW requires ~1us delay after reset release before register access works.
	udelay(1);

	udc->init = 1;
	ret = udc_init(udc);
	if (ret) {
		dev_err(dev, "%s: udc_init failed\n", __func__);
		return -EINVAL;
	}

	spin_lock_init(&udc->lock);
	udc->gadget.ops			= &aspeed_gadget_ops;
	udc->gadget.ep0			= &udc->ep[0].ep;
	udc->gadget.max_speed		= udc->maximum_speed;
	udc->gadget.speed		= USB_SPEED_UNKNOWN;
	udc->gadget.name		= "aspeed-gadget";
	udc->root_setup			= (struct usb_ctrlrequest *)
					(udc->udc_base + AST_VHUB_SETUP_DATA0);
#ifdef AST_EP_DESC_MODE
	udc->desc_mode			= 1;
#else
	udc->desc_mode			= 0;
#endif
	pr_info("%s: desc_mode: %d\n", __func__, udc->desc_mode);
	/* * Allocate DMA buffers for all EP0s in one chunk,
	 * one per port and one for the vHub itself
	 */
	udc->ep0_ctrl_buf =
		dma_alloc_coherent(EP_DMA_SIZE * UDC_MAX_ENDPOINTS,
				   (unsigned long *)&udc->ep0_ctrl_dma);

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	aspeed_udc_ep_init(udc);

	ret = usb_add_gadget_udc((struct device *)udc->dev, &udc->gadget);
	if (ret) {
		dev_err(udc->dev, "failed to register udc\n");
		return ret;
	}

	return 0;
}

static int aspeed_udc_of_to_plat(struct udevice *dev)
{
	struct aspeed_udc_priv *udc = dev_get_priv(dev);
	ofnode node = dev_ofnode(dev);
	int ret;

	udc->udc_base = dev_read_addr_index_ptr(dev, 0);
	udc->dev = dev;

	udc->maximum_speed = usb_get_maximum_speed(node);
	if (udc->maximum_speed == USB_SPEED_UNKNOWN) {
		UDC_DBG("No USB maximum speed specified. Using High Speed\n");
		udc->maximum_speed = USB_SPEED_HIGH;
	}

	ret = reset_get_by_index(dev, 0, &udc->reset);
	if (ret) {
		dev_err(dev, "%s: Failed to get udc reset signal\n", __func__);
		return ret;
	}

	ret = clk_get_by_index(dev, 0, &udc->clk);
	if (ret) {
		dev_err(dev, "%s: Failed to get udc clock\n", __func__);
		return ret;
	}

	return 0;
}

static int aspeed_udc_remove(struct udevice *dev)
{
	struct aspeed_udc_priv *udc = dev_get_priv(dev);

	usb_del_gadget_udc(&udc->gadget);
	if (udc->gadget_driver)
		return -EBUSY;

	writel(readl(udc->udc_base + AST_VHUB_CTRL) & ~ROOT_UPSTREAM_EN,
	       udc->udc_base + AST_VHUB_CTRL);

	reset_release_all(&udc->reset, 1);

	clk_release_all(&udc->clk, 1);

	dma_free_coherent(udc->ep0_ctrl_buf);

	return 0;
}

static const struct udevice_id aspeed_udc_ids[] = {
	{ .compatible = "aspeed,ast2700-usb-vhub" },
	{ }
};

U_BOOT_DRIVER(aspeed_udc_generic) = {
	.name		= "aspeed_udc_generic",
	.id		= UCLASS_USB_GADGET_GENERIC,
	.of_match	= aspeed_udc_ids,
	.probe		= aspeed_udc_probe,
	.remove		= aspeed_udc_remove,
	.of_to_plat	= aspeed_udc_of_to_plat,
	.priv_auto	= sizeof(struct aspeed_udc_priv),
};
