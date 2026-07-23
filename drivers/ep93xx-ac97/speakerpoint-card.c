// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC machine driver for the Control4 SpeakerPoint.
 *
 * Signal path (confirmed from stock firmware /etc/modules load order):
 *
 *   EP93xx AC97 controller (0x80880000)
 *     ↓  AC97 bus
 *   STAC9758 AC97 2.3 codec  (mixing / DAC / ADC)
 *     ├─ 2× DAC channels  → serial I2S-justified output
 *     │     ↓
 *     │   D241051QR DSP amplifier  (I2C cfg @ bus 0 addr 0x59, I2S audio in)
 *     │     ↓  2× speaker outputs
 *     ├─ 2× RCA LINE OUT  (line-level from STAC9758 DAC)
 *     └─ 2× RCA LINE IN   (ADC input to STAC9758)
 *
 * The STAC9758 handles mixing between the network/playback source and the
 * RCA LINE IN; the D241051QR drives the physical speaker outputs.
 *
 * Based on sound/soc/cirrus/simone.c (removed from mainline with its board
 * support in Linux 5.9), updated for the v6.x ASoC API:
 *  - of_device_id table for DT probing.
 *  - remove() returns void.
 *  - devm_snd_soc_register_card() replaces the non-devm variant.
 *  - ac97-codec platform device still managed manually (no devm_ variant for
 *    platform_device_register_simple exists; manual unregister in remove is
 *    safe because devm card cleanup runs first).
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

/*
 * The "ac97-codec" platform device instantiates the SND_SOC_AC97_CODEC
 * driver (sound/soc/codecs/ac97.c) which wraps the generic snd_ac97_codec
 * layer.  The ep93xx-ac97 driver registers snd_ac97_bus_ops via
 * snd_soc_set_ac97_ops(); once the ASoC card binds the DAI link the
 * framework performs the AC97 cold-reset and codec enumeration
 * automatically, discovering the STAC9758 at slot 0.
 *
 * CPU DAI and Platform component names are matched via
 * snd_soc_lookup_component_by_name() against component->name, which
 * snd_soc_component_initialize() derives from fmt_single_name(dev, NULL)
 * whenever the component driver doesn't pre-populate component->name
 * (ep93xx_ac97_component's .name field isn't used for this - see
 * soc-core.c). Since ep93xx-ac97's controller device now comes from the
 * DT node ac97@80880000 rather than the old
 * platform_device_register_simple("ep93xx-ac97", ...), its real
 * dev_name() - and so its component name - is "80880000.ac97", not the
 * bare driver name "ep93xx-ac97" fmt_single_name() would produce for a
 * "<name>.<id>"-style legacy device. Confirmed against a running board:
 * /sys/bus/platform/devices/80880000.ac97 bound to ep93xx-ac97, but the
 * card stayed on -EPROBE_DEFER forever because "ep93xx-ac97" never
 * matched. ac97-codec doesn't have this problem: it's still registered
 * below the old way, via platform_device_register_simple(), so its name
 * really is the literal "ac97-codec".
 *
 * CPU DAI:   "80880000.ac97"  — ep93xx-ac97.ko, DT node ac97@80880000
 * Codec DAI: "ac97-hifi"      — snd-soc-ac97-codec (in-tree, CONFIG_SND_SOC_AC97_CODEC)
 * Platform:  "80880000.ac97"  — PCM platform registered by the same device
 */
SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("80880000.ac97")),
	DAILINK_COMP_ARRAY(COMP_CODEC("ac97-codec", "ac97-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("80880000.ac97")));

static struct snd_soc_dai_link speakerpoint_dai = {
	.name		= "STAC9758",
	.stream_name	= "AC97 HiFi",
	SND_SOC_DAILINK_REG(hifi),
};

static struct snd_soc_card speakerpoint_card = {
	.name		= "SpeakerPoint",
	.owner		= THIS_MODULE,
	.dai_link	= &speakerpoint_dai,
	.num_links	= 1,
};

/* The ac97-codec platform device is registered here so that the
 * SND_SOC_AC97_CODEC platform driver can bind to it (its name is the
 * bus match key used by COMP_CODEC("ac97-codec", ...)).               */
static struct platform_device *speakerpoint_ac97_codec_dev;

static int speakerpoint_probe(struct platform_device *pdev)
{
	int ret;

	/*
	 * Register ac97-codec exactly once. Recreating it on every deferred
	 * probe retry (as this used to do, tearing it down again on
	 * failure) meant each retry's successful ac97-codec bind called
	 * driver_bound() -> driver_deferred_probe_trigger(), which re-walks
	 * the whole deferred-probe list - including this device's own
	 * still-deferred entry. That made every -EPROBE_DEFER schedule its
	 * own next retry forever, regardless of what else was or wasn't
	 * ready, and never let the deferred list settle.
	 */
	if (!speakerpoint_ac97_codec_dev) {
		speakerpoint_ac97_codec_dev =
			platform_device_register_simple("ac97-codec", -1, NULL, 0);
		if (IS_ERR(speakerpoint_ac97_codec_dev)) {
			ret = PTR_ERR(speakerpoint_ac97_codec_dev);
			speakerpoint_ac97_codec_dev = NULL;
			return ret;
		}
	}

	speakerpoint_card.dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, &speakerpoint_card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static void speakerpoint_remove(struct platform_device *pdev)
{
	/* Card already unregistered by devm at this point. */
	if (speakerpoint_ac97_codec_dev) {
		platform_device_unregister(speakerpoint_ac97_codec_dev);
		speakerpoint_ac97_codec_dev = NULL;
	}
}

static const struct of_device_id speakerpoint_of_ids[] = {
	{ .compatible = "control4,speakerpoint-audio" },
	{ }
};
MODULE_DEVICE_TABLE(of, speakerpoint_of_ids);

static struct platform_driver speakerpoint_driver = {
	.probe	= speakerpoint_probe,
	.remove	= speakerpoint_remove,
	.driver	= {
		.name		= "speakerpoint-audio",
		.of_match_table	= speakerpoint_of_ids,
	},
};

module_platform_driver(speakerpoint_driver);

MODULE_DESCRIPTION("ASoC machine driver for Control4 SpeakerPoint (EP93xx + STAC9758)");
MODULE_AUTHOR("Joseph Telaak <joseph@telaak.dev>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:speakerpoint-audio");
