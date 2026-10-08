FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu libc6-dev-arm64-cross ca-certificates file binutils-aarch64-linux-gnu \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /build
