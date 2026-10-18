# VERSION is the ACAP SDK version. Keep comments on their own line: make keeps
# the spaces between a value and a trailing `#`.
ARCH        ?= aarch64
VERSION     ?= 12.11.0
VDO_FORMAT  ?= jpeg
OUTPUT_DIR  := build

.PHONY: build clean

build:
	DOCKER_BUILDKIT=1 docker build \
	  --build-arg ARCH=$(ARCH) \
	  --build-arg VERSION=$(VERSION) \
	  --build-arg VDO_FORMAT=$(VDO_FORMAT) \
	  --target export \
	  --output type=local,dest=$(OUTPUT_DIR) \
	  .

clean:
	rm -rf $(OUTPUT_DIR)
