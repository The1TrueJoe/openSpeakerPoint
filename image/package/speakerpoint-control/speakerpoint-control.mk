################################################################################
#
# speakerpoint-control
#
################################################################################

SPEAKERPOINT_CONTROL_SITE = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/../apps/speakerpoint-control
SPEAKERPOINT_CONTROL_SITE_METHOD = local
SPEAKERPOINT_CONTROL_LICENSE = MIT

# Runtime companions the control daemon shells out to / proxies, and
# libmosquitto for its MQTT client.
SPEAKERPOINT_CONTROL_DEPENDENCIES = alsa-utils i2c-tools mpd shairport-sync mosquitto

define SPEAKERPOINT_CONTROL_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) speakerpoint-control
endef

define SPEAKERPOINT_CONTROL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/speakerpoint-control \
		$(TARGET_DIR)/usr/bin/speakerpoint-control
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/speakerpoint-audio-apply \
		$(TARGET_DIR)/usr/bin/speakerpoint-audio-apply
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/usb-automount \
		$(TARGET_DIR)/usr/bin/usb-automount
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/usb-detect \
		$(TARGET_DIR)/usr/bin/usb-detect
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/airplay-hook \
		$(TARGET_DIR)/usr/bin/airplay-hook
	$(INSTALL) -D -m 0644 $(SPEAKERPOINT_CONTROL_PKGDIR)/mdev.conf \
		$(TARGET_DIR)/etc/mdev.conf
	# Override Buildroot's stock shairport-sync.conf and init script (we
	# depend on the package, so its files are already installed): our conf
	# sets the AirPlay name/session hooks, and our init script backgrounds
	# shairport-sync via start-stop-daemon instead of its stock script's
	# "-d" flag, which requires libdaemon (not selected - unused weight).
	$(INSTALL) -D -m 0644 $(SPEAKERPOINT_CONTROL_PKGDIR)/shairport-sync.conf \
		$(TARGET_DIR)/etc/shairport-sync.conf
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/S99shairport-sync \
		$(TARGET_DIR)/etc/init.d/S99shairport-sync
	# Our mpd.conf + init replace Buildroot's stock ones (we depend on mpd, so
	# its files are already installed). Removing S95mpd stops MPD being
	# started twice and keeps it ordered before the control daemon.
	$(INSTALL) -D -m 0644 $(SPEAKERPOINT_CONTROL_PKGDIR)/mpd.conf \
		$(TARGET_DIR)/etc/mpd.conf
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/S58mpd \
		$(TARGET_DIR)/etc/init.d/S58mpd
	rm -f $(TARGET_DIR)/etc/init.d/S95mpd
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/S61speakerpoint-control \
		$(TARGET_DIR)/etc/init.d/S61speakerpoint-control
endef

$(eval $(generic-package))
