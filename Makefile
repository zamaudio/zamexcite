#!/usr/bin/make -f

PREFIX ?= /usr/local
LIBDIR ?= lib
LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only

LDFLAGS ?= -Wl,--as-needed
CXXFLAGS ?= $(OPTIMIZATIONS) -Wall
CFLAGS ?= $(OPTIMIZATIONS) -Wall

###############################################################################
BUNDLE = zamexcite.lv2

CXXFLAGS += -fPIC -DPIC
CFLAGS += -fPIC -DPIC

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LIB_EXT=.dylib
  LDFLAGS += -dynamiclib
else
  LDFLAGS += -shared -Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
endif


ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
else
  LV2FLAGS=`pkg-config --cflags --libs lv2`
endif

$(BUNDLE): manifest.ttl zamexcite.ttl zamexcite$(LIB_EXT)
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp manifest.ttl zamexcite.ttl zamexcite$(LIB_EXT) $(BUNDLE)

zamexcite$(LIB_EXT): zamexcite.c
	$(CXX) -o zamexcite$(LIB_EXT) \
		$(CXXFLAGS) \
		zamexcite.c \
		$(LV2FLAGS) $(LDFLAGS)

install: $(BUNDLE)
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -t $(DESTDIR)$(LV2DIR)/$(BUNDLE) $(BUNDLE)/*

uninstall:
	rm -rf $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -rf $(BUNDLE) zamexcite$(LIB_EXT)

.PHONY: clean install uninstall
