include $(TOPDIR)/rules.mk

PKG_NAME:=rpi4-router
PKG_VERSION:=1.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/rpi4-router
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Raspberry Pi 4 Wi-Fi router first-boot configuration
  DEPENDS:=@TARGET_bcm27xx_bcm2711 +router-telemetry +dnsmasq +firewall4 +kmod-brcmfmac +wpad-basic-mbedtls +wifi-scripts +uci
endef

define Package/rpi4-router/description
 Configures eth0 DHCP WAN and a WPA2 Wi-Fi LAN at 192.168.88.1 before
 network startup. Requires /etc/config/rpi4_router with a Wi-Fi key.
 Releases the serial console in images built from this source tree.
endef

define Package/rpi4-router/conffiles
/etc/config/rpi4_router
endef

define Build/Compile
endef

define Package/rpi4-router/install
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/usr/libexec $(1)/etc/config
	$(INSTALL_BIN) ./files/rpi4-router.init $(1)/etc/init.d/rpi4-router
	$(INSTALL_BIN) ./files/rpi4-router-setup $(1)/usr/libexec/rpi4-router-setup
	$(INSTALL_CONF) ./files/rpi4_router.config $(1)/etc/config/rpi4_router
endef

$(eval $(call BuildPackage,rpi4-router))
