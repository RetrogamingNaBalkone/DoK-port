FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
# No compiler, headers, CMake or development SDK in the runtime test image.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libsdl2-2.0-0 libstdc++6 libgl1 libgl1-mesa-dri python3 xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
