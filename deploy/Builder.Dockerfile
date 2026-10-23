FROM rust:1.97.1-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends clang libclang-dev pkg-config protobuf-compiler cmake ninja-build python3 zlib1g-dev && rm -rf /var/lib/apt/lists/*
ENV RUSTUP_TOOLCHAIN=1.97.1 CARGO_PROFILE_DEV_DEBUG=0
WORKDIR /src
