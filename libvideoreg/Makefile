#
# Copyright (C) 2006-2011 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libvideoreg
PKG_VERSION:=1.0.0
PKG_RELEASE:=1


PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/libvideoreg
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=video cache library

endef

define Package/libvideoreg/description
	The libvideoreg API provides method to parse the dynamic video URL to a fix ID.
endef

TARGET_CFLAGS += $(FPIC)

CONFIGURE_ARGS += 

MAKE_FLAGS += \
	CFLAGS="$(TARGET_CFLAGS)" \

define Build/Compile
	$(CP) ./src/* $(PKG_BUILD_DIR)
	$(call Build/Compile/Default)
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/videoreg.h $(1)/usr/include/

	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/libvideoreg.so $(1)/usr/lib/
endef

define Package/libvideoreg/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/libvideoreg.so $(1)/usr/lib/
	$(CP) $(PKG_BUILD_DIR)/videoreg.h $(1)/usr/include/
endef

$(eval $(call BuildPackage,libvideoreg))
