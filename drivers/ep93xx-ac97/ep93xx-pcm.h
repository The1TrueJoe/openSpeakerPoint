/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Local copy of sound/soc/cirrus/ep93xx-pcm.h for out-of-tree build.
 * The in-tree header is not exported; we redeclare the only symbol we need.
 */
#ifndef __EP93XX_PCM_H__
#define __EP93XX_PCM_H__

int devm_ep93xx_pcm_platform_register(struct device *dev);

#endif
