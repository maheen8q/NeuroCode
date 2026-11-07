# ---------- 1. Frontend build ----------
FROM node:20-bookworm AS frontend
WORKDIR /fe
COPY frontend/package*.json ./
RUN npm ci
COPY frontend/ ./
RUN npm run build

# ---------- 2. Backend build + runtime ----------
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git curl unzip ca-certificates nginx \
      libsqlite3-dev sqlite3 nlohmann-json3-dev libgtest-dev libsodium-dev \
      llvm-18-dev libclang-18-dev \
      libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Drogon from source (same as the WSL setup)
RUN git clone --depth 1 --branch v1.9.10 --recursive https://github.com/drogonframework/drogon /tmp/drogon \
 && cmake -S /tmp/drogon -B /tmp/drogon/build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_CTL=OFF \
 && cmake --build /tmp/drogon/build -j2 && cmake --install /tmp/drogon/build \
 && rm -rf /tmp/drogon

# LibTorch CPU. Set this to the SAME version as ~/libs/libtorch/build-version.
ARG LIBTORCH_URL=https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.5.1%2Bcpu.zip
RUN curl -L -o /tmp/libtorch.zip "$LIBTORCH_URL" && unzip -q /tmp/libtorch.zip -d /opt && rm /tmp/libtorch.zip
ENV LD_LIBRARY_PATH=/opt/libtorch/lib

# Backend
WORKDIR /app/backend
COPY backend/ ./
RUN rm -rf build \
 && cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/libtorch \
      -DNC_LIBCLANG=/usr/lib/llvm-18/lib/libclang.so \
      -DNC_CLANG_INCLUDE=/usr/lib/llvm-18/include \
 && cmake --build build -j2 --target neurocode

# Frontend output + server config
COPY --from=frontend /fe/dist /app/frontend
COPY deploy/nginx.conf /app/nginx.conf
COPY deploy/start.sh /app/start.sh

# Hugging Face runs the container as uid 1000: make the writable paths its own
RUN chmod +x /app/start.sh && mkdir -p /app/backend/data /app/backend/uploads \
 && chown -R 1000:1000 /app

USER 1000
EXPOSE 7860
CMD ["/app/start.sh"]