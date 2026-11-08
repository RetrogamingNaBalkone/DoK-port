FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config libsdl2-dev libgles2-mesa-dev \
    python3 git xvfb xauth libgl1-mesa-dri mesa-utils binutils ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
