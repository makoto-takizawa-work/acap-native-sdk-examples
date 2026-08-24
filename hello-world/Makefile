# VERSION is the ACAP SDK version chosen at setup. Keep the comment on its own
# line: make keeps the spaces between a value and a trailing `#`, so writing
# `VERSION ?= 12.11.0   # ...` puts those spaces inside VERSION itself.
ARCH        ?= aarch64
VERSION     ?= 12.11.0
OUTPUT_DIR  := build

.PHONY: build clean

build:
	DOCKER_BUILDKIT=1 docker build \
	  --build-arg ARCH=$(ARCH) \
	  --build-arg VERSION=$(VERSION) \
	  --target export \
	  --output type=local,dest=$(OUTPUT_DIR) \
	  .

clean:
	rm -rf $(OUTPUT_DIR)
