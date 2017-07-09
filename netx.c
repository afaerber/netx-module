/*
 * Copyright (c) 2017 Andreas Färber
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>

struct netx_ops {
	const char *name;
	int (*init)(struct spi_device *spi);
	int (*read)(struct spi_device *spi, u32 addr, u8 len, void *buf);
};

struct netx_packet_head {
	u32 dest;
	u32 src;
	u32 dest_id;
	u32 src_id;
	u32 len;
	u32 id;
	u32 state;
	u32 cmd;
	u32 ext;
	u32 rout;
};

struct netx_packet {
	struct netx_packet_head head;
};

static int netx10_init(struct spi_device *spi)
{
	return -ENOTSUPP;
}

static const struct netx_ops netx10_ops = {
	.name = "netX10",
	.init = netx10_init,
};

static int netx50_init(struct spi_device *spi)
{
	return -ENOTSUPP;
}

static const struct netx_ops netx50_ops = {
	.name = "netX50",
	.init = netx50_init,
};

static int netx100_init(struct spi_device *spi)
{
	return -ENOTSUPP;
}

static const struct netx_ops netx100_ops = {
	.name = "netX100",
	.init = netx100_init,
};

static int netx51_read(struct spi_device *spi, u32 addr, u8 len, void *val)
{
	struct spi_transfer xfers[2];
	u8 buf[4], rxbuf[4];
	int ret;

	buf[0] = ((addr >> 16) & 0xf) | BIT(7);
	buf[1] = (addr >> 8) & 0xff;
	buf[2] = addr & 0xff;
	buf[3] = len;

	memset(xfers, 0, 2 * sizeof(struct spi_transfer));
	xfers[0].tx_buf = buf;
	xfers[0].rx_buf = rxbuf;
	xfers[0].len = 4;
	xfers[1].rx_buf = val;
	xfers[1].len = len;

	ret = spi_sync_transfer(spi, xfers, 2);
	if (ret < 0)
		return ret;

	dev_dbg(&spi->dev, "read status: %02x\n",
		(unsigned)rxbuf[0]);

	return 0;
}

static int netx51_init(struct spi_device *spi)
{
	char sz[5];
	int ret;

	/* Two dummy reads for sDPM initialization */
	ret = netx51_read(spi, 0x0, 1, sz);
	if (ret < 0)
		return ret;
	ret = netx51_read(spi, 0x0, 1, sz);
	if (ret < 0)
		return ret;

	memset(sz, 0, 5);
	ret = netx51_read(spi, 0x0, 4, sz);
	if (ret < 0)
		return ret;

	sz[4] = '\0';
	dev_info(&spi->dev, "abCookie = %s\n", sz);

	return 0;
}

static const struct netx_ops netx51_ops = {
	.name = "netX51",
	.init = netx51_init,
	.read = netx51_read,
};

static int netx_probe(struct spi_device *spi)
{
	u8 buf[3], rxbuf[3];
	struct spi_transfer xfer = {
		.tx_buf = buf,
		.rx_buf = rxbuf,
		.len = 4,
	};
	const struct netx_ops *ops;
	u32 val;
	int ret;

	dev_info(&spi->dev, "netx probe\n");

	buf[0] = 0x00;
	buf[1] = 0xFF;
	buf[2] = 0x84;
	ret = spi_sync_transfer(spi, &xfer, 1);
	if (ret < 0)
		return ret;

	dev_dbg(&spi->dev, "read: %02x %02x %02x\n",
		(unsigned)rxbuf[0], (unsigned)rxbuf[1], (unsigned)rxbuf[2]);

	if (rxbuf[0] == 0x00 && rxbuf[1] == 0x00 && rxbuf[2] == 0x00) {
		ops = &netx10_ops;
	} else if (rxbuf[0] == 0xFF && rxbuf[1] == 0xFF && rxbuf[2] == 0xFF) {
		ops = &netx50_ops;
	} else if ((rxbuf[0] & GENMASK(4, 0)) == 0x11) {
		ops = &netx51_ops;
	} else if (rxbuf[0] == 0x64) {
		ops = &netx100_ops;
	} else {
		dev_err(&spi->dev, "netX model not recognized\n");
		return -EINVAL;
	}

	dev_info(&spi->dev, "%s family\n", ops->name);

	if (ops->init) {
		ret = ops->init(spi);
		if (ret < 0)
			return ret;
	}

	ret = ops->read(spi, 0x00C4, 4, &val);
	if (ret < 0)
		return ret;

	le32_to_cpus(&val);
	dev_info(&spi->dev, "status = %08x\n", val);

#define RCX_SYS_STATUS_NXO_SUPPORTED BIT(31)
	if (val & RCX_SYS_STATUS_NXO_SUPPORTED) {
	}

	return 0;
}

static int netx_remove(struct spi_device *spi)
{
	dev_info(&spi->dev, "netx removed\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id netx_dt_ids[] = {
	{ .compatible = "hilscher,netx52" },
	{}
};
MODULE_DEVICE_TABLE(of, netx_dt_ids);
#endif

static struct spi_driver netx_spi_driver = {
	.driver = {
		.name = "netx",
		.of_match_table = of_match_ptr(netx_dt_ids),
	},
	.probe = netx_probe,
	.remove = netx_remove,
};

module_spi_driver(netx_spi_driver);

MODULE_DESCRIPTION("netX SPI driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
