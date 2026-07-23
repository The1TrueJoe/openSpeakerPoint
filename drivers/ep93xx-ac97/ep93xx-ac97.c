// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC driver for Cirrus Logic EP93xx AC97 controller.
 *
 * Ported from Linux v5.8 sound/soc/cirrus/ep93xx-ac97.c to the v6.x ASoC
 * API.  Original copyright:
 *
 *   Copyright (c) 2010 Mika Westerberg
 *   Based on s3c-ac97 ASoC driver by Jaswinder Singh.
 *
 * Changes vs. the v5.8 original:
 *  - linux/platform_data/dma-ep93xx.h removed upstream; DMA channels are
 *    now obtained from DT via snd_dmaengine_dai_dma_data.chan_name.
 *  - DAI .probe moved from snd_soc_dai_driver into snd_soc_dai_ops.
 *  - snd_soc_dai_init_dma_data() replaces direct dai->playback/capture_dma_data
 *    assignment.
 *  - devm_snd_soc_register_component() replaces the non-devm variant.
 *  - legacy_dai_naming = 1 added to snd_soc_component_driver.
 *  - ep93xx_ac97_remove() changed to return void (kernel >= 5.12 expectation).
 *  - of_device_id table added for DT probing.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/ac97_codec.h>
#include <sound/soc.h>

#include "ep93xx-pcm.h"

/* Per-channel (1-4) registers */
#define AC97CH(n)		(((n) - 1) * 0x20)

#define AC97DR(n)		(AC97CH(n) + 0x0000)

#define AC97RXCR(n)		(AC97CH(n) + 0x0004)
#define AC97RXCR_REN		BIT(0)
#define AC97RXCR_RX3		BIT(3)
#define AC97RXCR_RX4		BIT(4)
#define AC97RXCR_CM		BIT(15)

#define AC97TXCR(n)		(AC97CH(n) + 0x0008)
#define AC97TXCR_TEN		BIT(0)
#define AC97TXCR_TX3		BIT(3)
#define AC97TXCR_TX4		BIT(4)
#define AC97TXCR_CM		BIT(15)

#define AC97SR(n)		(AC97CH(n) + 0x000c)
#define AC97SR_TXFE		BIT(1)
#define AC97SR_TXUE		BIT(6)

#define AC97RISR(n)		(AC97CH(n) + 0x0010)
#define AC97ISR(n)		(AC97CH(n) + 0x0014)
#define AC97IE(n)		(AC97CH(n) + 0x0018)

/* Global AC97 controller registers */
#define AC97S1DATA		0x0080
#define AC97S2DATA		0x0084
#define AC97S12DATA		0x0088

#define AC97RGIS		0x008c
#define AC97GIS			0x0090
#define AC97IM			0x0094

/* Common bits for RGIS, GIS, IM */
#define AC97_SLOT2RXVALID	BIT(1)
#define AC97_CODECREADY		BIT(5)
#define AC97_SLOT2TXCOMPLETE	BIT(6)

#define AC97EOI			0x0098
#define AC97EOI_WINT		BIT(0)
#define AC97EOI_CODECREADY	BIT(1)

#define AC97GCR			0x009c
#define AC97GCR_AC97IFE		BIT(0)

#define AC97RESET		0x00a0
#define AC97RESET_TIMEDRESET	BIT(0)

#define AC97SYNC		0x00a4
#define AC97SYNC_TIMEDSYNC	BIT(0)

#define AC97_TIMEOUT		msecs_to_jiffies(5)

/*
 * SYSCON DEVCFG bits A1ONG (bit21) and A2ONG (bit22), cleared alongside
 * I2SONAC97 (bit6) by this board's stock 2.4-kernel driver
 * (stac9758_init_controller() in Research/Speakerpoint/mtd4_jffs2.bin's
 * stac9758.o) before any AC97 register access. Mainline's pinctrl-ep93xx.c
 * "ac97" pinmux group only ever clears I2SONAC97 - A1ONG/A2ONG aren't part
 * of the generic AC97 group at all, so they're presumably a requirement
 * specific to this board's own PCB wiring (these bits otherwise mux
 * external-memory-bus address lines A1/A2), not something any upstream
 * mechanism would know to touch. Poked directly by physical address here
 * since the swlock-aware regmap wiring in clk-ep93xx.c/pinctrl-ep93xx.c
 * isn't reachable from a plain syscon phandle lookup, and we already have
 * the exact address/value from disassembly.
 */
#define EP93XX_SYSCON_PHYS_BASE		0x80930000
#define EP93XX_SYSCON_DEVCFG		0x0080
#define EP93XX_SYSCON_DEVCFG_A1ONG	BIT(21)
#define EP93XX_SYSCON_DEVCFG_A2ONG	BIT(22)
#define EP93XX_SYSCON_SWLOCK		0x00c0
#define EP93XX_SYSCON_SWLOCK_MAGIC	0xaa

/**
 * struct ep93xx_ac97_info - EP93xx AC97 controller private data
 * @lock:         serialises slot 1 & 2 register accesses
 * @dev:          platform device pointer (for dev_warn etc.)
 * @regs:         remapped controller registers
 * @done:         wait queue for interrupt-driven register read/write
 * @dma_params_rx: DMA slave config for capture
 * @dma_params_tx: DMA slave config for playback
 */
struct ep93xx_ac97_info {
	struct mutex			lock;
	struct device			*dev;
	void __iomem			*regs;
	struct completion		done;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
};

/* ALSA supports only one AC97 device; keep a module-global pointer for the
 * AC97 bus ops callbacks which receive struct snd_ac97 * (no back-pointer). */
static struct ep93xx_ac97_info *ep93xx_ac97_info;

static inline unsigned ep93xx_ac97_read_reg(struct ep93xx_ac97_info *info,
					    unsigned reg)
{
	return __raw_readl(info->regs + reg);
}

static inline void ep93xx_ac97_write_reg(struct ep93xx_ac97_info *info,
					 unsigned reg, unsigned val)
{
	__raw_writel(val, info->regs + reg);
}

static unsigned short ep93xx_ac97_read(struct snd_ac97 *ac97,
				       unsigned short reg)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;
	unsigned short val;

	mutex_lock(&info->lock);

	ep93xx_ac97_write_reg(info, AC97S1DATA, reg);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_SLOT2RXVALID);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT)) {
		dev_warn(info->dev, "timeout reading register %#x\n", reg);
		mutex_unlock(&info->lock);
		return -ETIMEDOUT;
	}
	val = (unsigned short)ep93xx_ac97_read_reg(info, AC97S2DATA);

	mutex_unlock(&info->lock);
	return val;
}

static void ep93xx_ac97_write(struct snd_ac97 *ac97,
			      unsigned short reg,
			      unsigned short val)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/* Slot 2 must be written before slot 1 */
	ep93xx_ac97_write_reg(info, AC97S2DATA, val);
	ep93xx_ac97_write_reg(info, AC97S1DATA, reg);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_SLOT2TXCOMPLETE);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "timeout writing register %#x\n", reg);

	mutex_unlock(&info->lock);
}

static void ep93xx_ac97_warm_reset(struct snd_ac97 *ac97)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/*
	 * Keep SYNC high > 1 µs via the TIMEDSYNC bit; the codec must have
	 * stopped BIT_CLK first (i.e. be in powerdown) before this is called.
	 */
	ep93xx_ac97_write_reg(info, AC97SYNC, AC97SYNC_TIMEDSYNC);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_CODECREADY);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "codec warm reset timeout\n");

	mutex_unlock(&info->lock);
}

static void ep93xx_ac97_cold_reset(struct snd_ac97 *ac97)
{
	struct ep93xx_ac97_info *info = ep93xx_ac97_info;

	mutex_lock(&info->lock);

	/*
	 * Mainline's ep93xx-ac97.c (and this driver, before this change) only
	 * ever wrote AC97RESET_TIMEDRESET (bit0=1) once and AC97SYNC_TIMEDSYNC
	 * (bit0=1) never during cold reset. Disassembly of this board's stock
	 * 2.4-kernel driver (stac9758.o's stac9758_init_controller(), from
	 * Research/Speakerpoint/mtd4_jffs2.bin) - the driver that DOES produce
	 * working audio on this exact hardware - showed AC97RESET written 1,
	 * then 4, then 6, and AC97SYNC written 4, then 0, around the GCR
	 * disable/enable toggle. That's real multi-bit manipulation of
	 * registers our simplified single-bit understanding never used.
	 * Replicated verbatim (register order preserved; the stock driver
	 * polls AC97RGIS bit5 with schedule_timeout() instead of our
	 * IRQ+completion wait, which is just a different way to detect the
	 * same CODECREADY condition, so that part is left as our own).
	 */
	ep93xx_ac97_write_reg(info, AC97RESET, 1);
	usleep_range(1000, 2000);

	/*
	 * Disable the interface, clear WINT and CODECREADY, re-enable.
	 */
	ep93xx_ac97_write_reg(info, AC97GCR, 0);
	ep93xx_ac97_write_reg(info, AC97EOI,
			      AC97EOI_CODECREADY | AC97EOI_WINT);
	usleep_range(1000, 2000);
	ep93xx_ac97_write_reg(info, AC97GCR, AC97GCR_AC97IFE);
	usleep_range(1000, 2000);

	ep93xx_ac97_write_reg(info, AC97SYNC, 4);
	ep93xx_ac97_write_reg(info, AC97RESET, 4);
	usleep_range(1000, 2000);
	ep93xx_ac97_write_reg(info, AC97RESET, 6);
	usleep_range(1000, 2000);
	ep93xx_ac97_write_reg(info, AC97SYNC, 0);

	/* Assert reset and wait for codec ready */
	ep93xx_ac97_write_reg(info, AC97RESET, AC97RESET_TIMEDRESET);
	ep93xx_ac97_write_reg(info, AC97IM, AC97_CODECREADY);
	if (!wait_for_completion_timeout(&info->done, AC97_TIMEOUT))
		dev_warn(info->dev, "codec cold reset timeout\n");

	/* Give the codec time to fully leave reset */
	usleep_range(15000, 20000);

	mutex_unlock(&info->lock);
}

static irqreturn_t ep93xx_ac97_interrupt(int irq, void *dev_id)
{
	struct ep93xx_ac97_info *info = dev_id;
	unsigned status, mask;

	status = ep93xx_ac97_read_reg(info, AC97GIS);
	mask   = ep93xx_ac97_read_reg(info, AC97IM);
	mask  &= ~status;
	ep93xx_ac97_write_reg(info, AC97IM, mask);

	complete(&info->done);
	return IRQ_HANDLED;
}

static struct snd_ac97_bus_ops ep93xx_ac97_ops = {
	.read		= ep93xx_ac97_read,
	.write		= ep93xx_ac97_write,
	.reset		= ep93xx_ac97_cold_reset,
	.warm_reset	= ep93xx_ac97_warm_reset,
};

static int ep93xx_ac97_trigger(struct snd_pcm_substream *substream,
			       int cmd, struct snd_soc_dai *dai)
{
	struct ep93xx_ac97_info *info = snd_soc_dai_get_drvdata(dai);
	unsigned v = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/* Compact mode, TX slots 3 & 4, TX FIFO enable */
			v |= AC97TXCR_CM;
			v |= AC97TXCR_TX3 | AC97TXCR_TX4;
			v |= AC97TXCR_TEN;
			ep93xx_ac97_write_reg(info, AC97TXCR(1), v);
		} else {
			/* Compact mode, RX slots 3 & 4, RX FIFO enable */
			v |= AC97RXCR_CM;
			v |= AC97RXCR_RX3 | AC97RXCR_RX4;
			v |= AC97RXCR_REN;
			ep93xx_ac97_write_reg(info, AC97RXCR(1), v);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/*
			 * Per Cirrus errata ER667E2B: wait for the TX FIFO to
			 * drain before clearing TEN.
			 */
			unsigned long timeout = jiffies + AC97_TIMEOUT;

			do {
				v = ep93xx_ac97_read_reg(info, AC97SR(1));
				if (time_after(jiffies, timeout)) {
					dev_warn(info->dev, "TX FIFO drain timeout\n");
					break;
				}
			} while (!(v & (AC97SR_TXFE | AC97SR_TXUE)));

			ep93xx_ac97_write_reg(info, AC97TXCR(1), 0);
		} else {
			ep93xx_ac97_write_reg(info, AC97RXCR(1), 0);
		}
		break;

	default:
		dev_warn(info->dev, "unknown trigger command %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

/*
 * DAI probe: wire up DMA data.  Called every time the DAI is opened.
 * Moved into snd_soc_dai_ops.probe (was snd_soc_dai_driver.probe in v5.8).
 */
static int ep93xx_ac97_dai_probe(struct snd_soc_dai *dai)
{
	struct ep93xx_ac97_info *info = snd_soc_dai_get_drvdata(dai);

	/*
	 * DMA channel selection via DT dma-names "tx" / "rx" on the AC97
	 * controller node (replaces the old ep93xx_dma_data filter_data path
	 * that was removed when dma-ep93xx.h was dropped).
	 */
	info->dma_params_tx.chan_name = "tx";
	info->dma_params_rx.chan_name = "rx";

	snd_soc_dai_init_dma_data(dai, &info->dma_params_tx,
				       &info->dma_params_rx);
	return 0;
}

static const struct snd_soc_dai_ops ep93xx_ac97_dai_ops = {
	.probe		= ep93xx_ac97_dai_probe,
	.trigger	= ep93xx_ac97_trigger,
};

static struct snd_soc_dai_driver ep93xx_ac97_dai = {
	.name		= "ep93xx-ac97",
	.id		= 0,
	.playback	= {
		.stream_name	= "AC97 Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture	= {
		.stream_name	= "AC97 Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops		= &ep93xx_ac97_dai_ops,
};

static const struct snd_soc_component_driver ep93xx_ac97_component = {
	.name			= "ep93xx-ac97",
	/* Required so the AC97 codec layer can find the DAI by name when
	 * the old-style component/DAI name matching path is used. */
	.legacy_dai_naming	= 1,
};

/*
 * Clear DEVCFG bits A1ONG/A2ONG, matching this board's stock driver.
 * See the comment by their #defines for why this can't just go through
 * the normal pinctrl/regmap path.
 */
static int ep93xx_ac97_clear_devcfg_bus_bits(struct device *dev)
{
	void __iomem *syscon;
	u32 val;

	syscon = devm_ioremap(dev, EP93XX_SYSCON_PHYS_BASE,
			     EP93XX_SYSCON_SWLOCK + 4);
	if (!syscon)
		return -ENOMEM;

	/* SWLOCK re-locks after every write to a protected register, so
	 * unlock immediately before the actual DEVCFG write. */
	__raw_writel(EP93XX_SYSCON_SWLOCK_MAGIC, syscon + EP93XX_SYSCON_SWLOCK);
	val = __raw_readl(syscon + EP93XX_SYSCON_DEVCFG);
	val &= ~(EP93XX_SYSCON_DEVCFG_A1ONG | EP93XX_SYSCON_DEVCFG_A2ONG);
	__raw_writel(EP93XX_SYSCON_SWLOCK_MAGIC, syscon + EP93XX_SYSCON_SWLOCK);
	__raw_writel(val, syscon + EP93XX_SYSCON_DEVCFG);

	devm_iounmap(dev, syscon);
	return 0;
}

static int ep93xx_ac97_probe(struct platform_device *pdev)
{
	struct ep93xx_ac97_info *info;
	int irq;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	ret = ep93xx_ac97_clear_devcfg_bus_bits(&pdev->dev);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq < 0 ? irq : -ENODEV;

	ret = devm_request_irq(&pdev->dev, irq, ep93xx_ac97_interrupt,
			       IRQF_TRIGGER_HIGH, pdev->name, info);
	if (ret)
		return ret;

	mutex_init(&info->lock);
	init_completion(&info->done);
	info->dev = &pdev->dev;

	/* Store globally so that AC97 bus ops callbacks (which only receive
	 * struct snd_ac97 *) can reach the hardware registers. */
	ep93xx_ac97_info = info;
	dev_set_drvdata(&pdev->dev, info);

	ret = snd_soc_set_ac97_ops(&ep93xx_ac97_ops);
	if (ret)
		goto err_clear_global;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &ep93xx_ac97_component,
					      &ep93xx_ac97_dai, 1);
	if (ret)
		goto err_clear_ops;

	ret = devm_ep93xx_pcm_platform_register(&pdev->dev);
	if (ret)
		goto err_clear_ops;

	return 0;

err_clear_ops:
	snd_soc_set_ac97_ops(NULL);
err_clear_global:
	ep93xx_ac97_info = NULL;
	return ret;
}

static void ep93xx_ac97_remove(struct platform_device *pdev)
{
	struct ep93xx_ac97_info *info = platform_get_drvdata(pdev);

	/* Disable the AC97 interface */
	ep93xx_ac97_write_reg(info, AC97GCR, 0);

	ep93xx_ac97_info = NULL;
	snd_soc_set_ac97_ops(NULL);
	/* devm handles snd_soc_unregister_component and free_irq */
}

static const struct of_device_id ep93xx_ac97_of_ids[] = {
	{ .compatible = "cirrus,ep9301-ac97" },
	{ }
};
MODULE_DEVICE_TABLE(of, ep93xx_ac97_of_ids);

static struct platform_driver ep93xx_ac97_driver = {
	.probe	= ep93xx_ac97_probe,
	.remove	= ep93xx_ac97_remove,
	.driver	= {
		.name		= "ep93xx-ac97",
		.of_match_table	= ep93xx_ac97_of_ids,
	},
};

module_platform_driver(ep93xx_ac97_driver);

MODULE_DESCRIPTION("EP93xx AC97 ASoC driver (ported from v5.8)");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-ac97");
