FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release \
    && strip build/dashboard

FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/system-monitoring-dashboard
COPY --from=build /src/build/dashboard /usr/local/bin/dashboard
COPY web ./web
COPY README.md ./README.md

ENV HOST_LABEL="" \
    PORT=8080 \
    WEB_ROOT="web"

EXPOSE 8080

# The binary serves both the API and static assets from WEB_ROOT.
ENTRYPOINT ["dashboard"]