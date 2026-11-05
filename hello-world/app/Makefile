PROG      = $(shell jq -r '.acapPackageConf.setup.appName' manifest.json)
SRCS      = main.c
DEBUG_DIR = debug

PKGS =

CFLAGS += -Wall -Wextra -Wformat=2 -Wpointer-arith \
          -Wbad-function-cast -Wstrict-prototypes -Wmissing-prototypes -Werror
CFLAGS += $(if $(PKGS),$(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS)))
LDLIBS += $(if $(PKGS),$(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS)))

all: $(PROG)

$(PROG): $(SRCS)
	install -d $(DEBUG_DIR)
	$(CC) $^ $(CFLAGS) $(LIBS) $(LDFLAGS) $(LDLIBS) -o $(DEBUG_DIR)/$@
	cp $(DEBUG_DIR)/$@ .
	$(STRIP) $@

clean:
	rm -rf $(PROG) *.o *.eap* *_LICENSE.txt package.conf* param.conf tmp* $(DEBUG_DIR)
