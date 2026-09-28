XBE_TITLE = Johnamos
NXDK_DIR ?= $(CURDIR)/../../nxdk
NXDK_NET = y
NXDK_SDL = y

MBEDTLS_DIR = $(CURDIR)/../mbedtls

SRCS = $(CURDIR)/main.c $(CURDIR)/app.c $(CURDIR)/gfx.c $(CURDIR)/net.c $(CURDIR)/dom.c $(CURDIR)/music.c $(CURDIR)/i18n.c \
       $(wildcard $(MBEDTLS_DIR)/library/*.c)

# nxdk applies NO optimization by default (an -O0 build). On the 733 MHz single-core
# Xbox that makes every frame - font rendering, drawing, MP3 decode, TLS - run several
# times slower than it should, dropping the frame rate low enough that quick controller
# taps land between input polls (you have to mash). -O2 fixes the whole class of problem.
NXDK_CFLAGS += -O2 -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library -I$(CURDIR) -Wno-everything

include $(NXDK_DIR)/Makefile

# files that must sit next to default.xbe
TARGET += $(OUTPUT_DIR)/vegur-regular.ttf $(OUTPUT_DIR)/cacert.pem $(OUTPUT_DIR)/logo_s.png $(OUTPUT_DIR)/logo_l.png
$(OUTPUT_DIR)/vegur-regular.ttf: $(CURDIR)/vegur-regular.ttf $(OUTPUT_DIR)
	$(VE)cp '$<' '$@'
$(OUTPUT_DIR)/cacert.pem: $(CURDIR)/cacert.pem $(OUTPUT_DIR)
	$(VE)cp '$<' '$@'
$(OUTPUT_DIR)/logo_s.png: $(CURDIR)/logo_s.png $(OUTPUT_DIR)
	$(VE)cp '$<' '$@'
$(OUTPUT_DIR)/logo_l.png: $(CURDIR)/logo_l.png $(OUTPUT_DIR)
	$(VE)cp '$<' '$@'
