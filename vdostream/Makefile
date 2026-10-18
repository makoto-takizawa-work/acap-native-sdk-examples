# VERSION is the ACAP SDK version. VDO_FORMAT selects app/manifest.json.<VDO_FORMAT>
# (h264, h265, av1, jpeg, avif, nv12, y800, rgb).
ARCH        ?= aarch64
VERSION     ?= 12.11.0
VDO_FORMAT  ?= h264
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
