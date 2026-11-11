include $(TOPDIR)/rules.mk

PKG_NAME:=router-telemetry
PKG_VERSION:=1.0
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define Package/router-telemetry
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Router metrics over UART using the STM32 RouterProtocol
endef

define Package/router-telemetry/description
 Samples real network, CPU and memory counters and sends 24-byte CRC32
 telemetry frames at 115200 baud. Managed by procd and UCI.
endef

define Package/router-telemetry/conffiles
/etc/config/router_telemetry
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Wpedantic -Werror \
		-o $(PKG_BUILD_DIR)/router_telemetry_sender \
		$(PKG_BUILD_DIR)/router_telemetry_sender.c \
		$(PKG_BUILD_DIR)/telemetry_metrics.c \
		$(PKG_BUILD_DIR)/router_protocol.c $(TARGET_LDFLAGS)
endef

define Package/router-telemetry/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/router_telemetry_sender $(1)/usr/sbin/router_telemetry_sender
	$(INSTALL_BIN) ./files/router_telemetry.init $(1)/etc/init.d/router_telemetry
	$(INSTALL_CONF) ./files/router_telemetry.config $(1)/etc/config/router_telemetry
endef

$(eval $(call BuildPackage,router-telemetry))
