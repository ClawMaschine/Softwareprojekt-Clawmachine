# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    qt5-qmake \
    qtbase5-dev \
    qtbase5-dev-tools \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
RUN git clone https://github.com/BrowseDNS/CaptiveDNS.git

WORKDIR /build/CaptiveDNS/CaptiveDNS
RUN qmake && make -j"$(nproc)"


# ── Stage 2: Runtime ──────────────────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libqt5core5a \
    libqt5gui5 \
    libqt5network5 \
    libqt5widgets5 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/CaptiveDNS/CaptiveDNS/CaptiveDNS /usr/local/bin/CaptiveDNS
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Redirect-IP — alle DNS-Anfragen landen hier (Hotspot-IP des Raspberry Pi)
ENV DNS_REDIRECT_IP=192.168.0.103

# Qt headless — kein Display nötig
ENV QT_QPA_PLATFORM=offscreen

EXPOSE 53/udp
EXPOSE 53/tcp
EXPOSE 80/tcp

ENTRYPOINT ["/entrypoint.sh"]
