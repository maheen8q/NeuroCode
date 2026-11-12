# Backend-only image for Render (frontend is deployed on Vercel)
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      pkg-config \
      git \
      curl \
      unzip \
      ca-certificates \
      libsqlite3-dev \
      sqlite3 \
      nlohmann-json3-dev \
      libgtest-dev \
      libsodium-dev \
      llvm-18-dev \
      libclang-18-dev \
      libjsoncpp-dev \
      uuid-dev \
      zlib1g-dev \
      libssl-dev \
      libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*


# ---------- Drogon ----------
RUN git clone --depth 1 --branch v1.9.10 --recursive \
      https://github.com/drogonframework/drogon /tmp/drogon \
    && cmake -S /tmp/drogon \
         -B /tmp/drogon/build \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_EXAMPLES=OFF \
         -DBUILD_CTL=OFF \
    && cmake --build /tmp/drogon/build -j2 \
    && cmake --install /tmp/drogon/build \
    && rm -rf /tmp/drogon


# ---------- LibTorch ----------
ARG LIBTORCH_URL=https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.5.1%2Bcpu.zip

RUN curl -L -o /tmp/libtorch.zip "$LIBTORCH_URL" \
    && unzip -q /tmp/libtorch.zip -d /opt \
    && rm /tmp/libtorch.zip

ENV LD_LIBRARY_PATH=/opt/libtorch/lib


# ---------- Backend ----------
WORKDIR /app/backend

COPY backend/ ./

RUN rm -rf build \
    && cmake -B build \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_PREFIX_PATH=/opt/libtorch \
         -DNC_LIBCLANG=/usr/lib/llvm-18/lib/libclang.so \
         -DNC_CLANG_INCLUDE=/usr/lib/llvm-18/include \
    && cmake --build build -j2 --target neurocode


# ---------- Deployment ----------
COPY deploy/start.sh /app/start.sh

RUN chmod +x /app/start.sh \
    && mkdir -p /app/backend/data /app/backend/uploads \
    && chown -R 1000:1000 /app

USER 1000

# Render provides the PORT environment variable; start.sh must listen on $PORT.
EXPOSE 7860

CMD ["/app/start.sh"]
