################################################################################
#
# speakerpoint-control
#
################################################################################

SPEAKERPOINT_CONTROL_SITE = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/../apps/speakerpoint-control
SPEAKERPOINT_CONTROL_SITE_METHOD = local
SPEAKERPOINT_CONTROL_LICENSE = MIT

# Runtime companions the control daemon shells out to / spawns.
SPEAKERPOINT_CONTROL_DEPENDENCIES = alsa-utils i2c-tools mpg123

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
	$(INSTALL) -D -m 0644 $(SPEAKERPOINT_CONTROL_PKGDIR)/mdev.conf \
		$(TARGET_DIR)/etc/mdev.conf
	$(INSTALL) -D -m 0755 $(SPEAKERPOINT_CONTROL_PKGDIR)/S61speakerpoint-control \
		$(TARGET_DIR)/etc/init.d/S61speakerpoint-control
endef

$(eval $(generic-package))
