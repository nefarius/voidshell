# Reproducible VoidShell build: docker build --output dist .
# Produces dist/voidshell.elf, dist/index.html and dist/SHA256SUMS.

ARG SDK_VERSION=v0.45
ARG SDK_SHA256=5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c
ARG BUN_VERSION=1.4.4

FROM ubuntu:24.04 AS elf
ARG SDK_VERSION
ARG SDK_SHA256
RUN apt-get update \
 && apt-get install -y --no-install-recommends bash clang-18 lld-18 llvm-18 make unzip wget ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN wget -q -O /tmp/sdk.zip "https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip" \
 && echo "${SDK_SHA256}  /tmp/sdk.zip" | sha256sum -c - \
 && unzip -q /tmp/sdk.zip -d /opt \
 && rm /tmp/sdk.zip
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
WORKDIR /src
ARG VOIDSHELL_SEMVER=
COPY Makefile ./
COPY src ./src
RUN make -j"$(nproc)" VOIDSHELL_SEMVER="${VOIDSHELL_SEMVER}" && test -f build/voidshell.elf

FROM oven/bun:${BUN_VERSION} AS ui
WORKDIR /src/ui
COPY ui/package.json ui/bun.lock ./
RUN bun install --frozen-lockfile
COPY ui/ ./
RUN bun run build && test -f ../www/index.html \
 && ! grep -q 'unpkg.com/react' ../www/index.html \
 && ! grep -q '@babel/standalone' ../www/index.html

FROM ubuntu:24.04 AS dist
WORKDIR /dist
COPY --from=elf /src/build/voidshell.elf ./voidshell.elf
COPY --from=ui /src/www/index.html ./index.html
RUN sha256sum voidshell.elf index.html > SHA256SUMS

FROM scratch
COPY --from=dist /dist/ /
