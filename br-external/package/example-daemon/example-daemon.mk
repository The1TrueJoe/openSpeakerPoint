################################################################################
#
# example-daemon
#
# Template package: builds apps/example-daemon (a sibling of br-external/ in
# the main repo) with the cross-toolchain and installs it + a boot script.
# Copy this .mk + its Config.in as the starting point for a real app - see
# apps/README.md.
#
################################################################################

EXAMPLE_DAEMON_SITE = $(BR2_EXTERNAL_SPEAKERPOINT_PATH)/../apps/example-daemon
EXAMPLE_DAEMON_SITE_METHOD = local
EXAMPLE_DAEMON_LICENSE = MIT

define EXAMPLE_DAEMON_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) example-daemon
endef

define EXAMPLE_DAEMON_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/example-daemon $(TARGET_DIR)/usr/bin/example-daemon
	$(INSTALL) -D -m 0755 $(EXAMPLE_DAEMON_PKGDIR)/S60example-daemon \
		$(TARGET_DIR)/etc/init.d/S60example-daemon
endef

$(eval $(generic-package))
