# Host unit-test harness for the pure devcfg core (no ESP-IDF).
CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 \
           -I../../components/devcfg/include -Ivendor \
           -I../../components/assetcache/include
SRCS    := test_cfg.c vendor/cJSON.c \
           ../../components/devcfg/cfg_parse.c \
           ../../components/devcfg/render_geom.c \
           ../../components/devcfg/clock_format.c \
           ../../components/assetcache/asset_key.c \
           ../../components/assetcache/asset_evict.c \
           vendor/sha1.c

run: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o run

test: run
	./run

clean:
	rm -f run
	rm -rf run.dSYM

.PHONY: test clean
