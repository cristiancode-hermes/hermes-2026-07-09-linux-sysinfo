# Stage 1: Build
FROM gcc:14-bookworm AS builder
RUN apt-get update && apt-get install -y cmake ninja-build pkg-config \
  && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json vcpkg.json ./
COPY src/ include/ tests/ ./
RUN cmake --preset debug && cmake --build --preset debug

# Stage 2: Runtime (~25 MB)
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y libsqlite3-0 ca-certificates \
  && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/build/debug/linux-sysinfo /usr/local/bin/linux-sysinfo
ENTRYPOINT ["linux-sysinfo"]
CMD ["--help"]
