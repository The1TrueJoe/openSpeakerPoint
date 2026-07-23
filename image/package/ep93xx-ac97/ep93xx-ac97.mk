################################################################################
#
# ep93xx-ac97 — EP93xx AC97 + SpeakerPoint card ASoC drivers
#
# CONFIG_MODULES=y breaks boot on this board (EP9301, ARM920T), so these
# drivers cannot be built as loadable modules.  Instead we inject the source
# files directly into the kernel source tree via a LINUX_POST_PATCH_HOOKS
# hook and compile them as built-in objects using new Kconfig symbols
# SND_EP93XX_SOC_AC97 and SND_SOC_SPEAKERPOINT.
#
# Helper files used by the hook (no escaping issues with file-based approach):
#   ep93xx-ac97-kbuild.mk  — lines to append to sound/soc/cirrus/Makefile
#   ep93xx-ac97-kconfig.txt — config entries to insert before endmenu
#
################################################################################

EP93XX_AC97_VERSION     = 1
EP93XX_AC97_SITE        = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/../drivers/ep93xx-ac97
EP93XX_AC97_SITE_METHOD = local
EP93XX_AC97_LICENSE     = GPL-2.0-only

EP93XX_AC97_BRDDIR = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/board/speakerpoint
EP93XX_AC97_APPDIR = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/../drivers/ep93xx-ac97

define EP93XX_AC97_INJECT_INTO_KERNEL
	cp -f $(EP93XX_AC97_APPDIR)/ep93xx-pcm.h \
	      $(LINUX_DIR)/sound/soc/cirrus/ep93xx-pcm.h
	cp -f $(EP93XX_AC97_APPDIR)/ep93xx-ac97.c \
	      $(LINUX_DIR)/sound/soc/cirrus/ep93xx-ac97.c
	cp -f $(EP93XX_AC97_APPDIR)/speakerpoint-card.c \
	      $(LINUX_DIR)/sound/soc/cirrus/speakerpoint-card.c
	grep -q 'snd-soc-ep93xx-ac97' \
	     $(LINUX_DIR)/sound/soc/cirrus/Makefile || \
	cat $(EP93XX_AC97_BRDDIR)/ep93xx-ac97-kbuild.mk \
	    >> $(LINUX_DIR)/sound/soc/cirrus/Makefile
	grep -q 'SND_EP93XX_SOC_AC97' \
	     $(LINUX_DIR)/sound/soc/cirrus/Kconfig || \
	( head -n -1 $(LINUX_DIR)/sound/soc/cirrus/Kconfig \
	      > /tmp/ep93xx-kconfig.tmp && \
	  cat $(EP93XX_AC97_BRDDIR)/ep93xx-ac97-kconfig.txt \
	      >> /tmp/ep93xx-kconfig.tmp && \
	  printf 'endmenu\n' >> /tmp/ep93xx-kconfig.tmp && \
	  cp /tmp/ep93xx-kconfig.tmp $(LINUX_DIR)/sound/soc/cirrus/Kconfig )
endef

LINUX_POST_PATCH_HOOKS += EP93XX_AC97_INJECT_INTO_KERNEL

define EP93XX_AC97_BUILD_CMDS
endef
define EP93XX_AC97_INSTALL_TARGET_CMDS
endef

$(eval $(generic-package))
