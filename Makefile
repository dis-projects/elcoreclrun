include $(TOPDIR)/rules.mk

PKG_NAME:=elcoreclrun
PKG_RELEASE:=1
CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/elcoreclrun
	SECTION:=utils
	CATEGORY:=Utilities
	DEPENDS:=@TARGET_elvees_mcom03 +kmod-elvees-elcore50 +elcorecllib
	TITLE:=Simple elcore run utility
endef

define Package/elcoreclrun/description
   elcorecl-run is a simple utility to start Elcore50 program
endef

TARGET_CFLAGS += -I$(STAGING_DIR_ROOT)/usr/include

define Package/elcoreclrun/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/elcorecl-run $(1)/usr/bin/
endef

$(eval $(call BuildPackage,elcoreclrun))

