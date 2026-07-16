# Host unit-test harness for the pure devcfg core (no ESP-IDF).
CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 \
           -I../../components/devcfg/include -I../../components/devcfg -Ivendor \
           -I../../components/assetcache/include -I../../components/cloud/include \
           -I../../components/mqtt/include
SRCS    := test_cfg.c vendor/cJSON.c \
           ../../components/devcfg/cfg_parse.c \
           ../../components/devcfg/render_geom.c \
           ../../components/devcfg/clock_format.c \
           ../../components/devcfg/provisioning.c \
           ../../components/devcfg/wifi_util.c \
           ../../components/devcfg/ota_manifest.c \
           ../../components/devcfg/sleep_policy.c \
           ../../components/devcfg/sha256.c \
           ../../components/devcfg/settings_pin.c \
           ../../components/devcfg/gesture.c \
           ../../components/devcfg/overrides.c \
           ../../components/devcfg/brightness.c \
           ../../components/assetcache/asset_key.c \
           ../../components/assetcache/asset_evict.c \
           ../../components/cloud/command_parse.c \
           ../../components/mqtt/mqtt_topics.c \
           vendor/sha1.c

run: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o run

test: run
	./run

clean:
	rm -f run
	rm -rf run.dSYM

.PHONY: test clean
