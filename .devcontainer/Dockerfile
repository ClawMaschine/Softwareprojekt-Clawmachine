FROM mcr.microsoft.com/devcontainers/base:ubuntu

RUN usermod -aG dialout vscode

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        python3 \
        python3-venv \
        git \
        openssh-client \
    && python3 -m venv /opt/platformio \
    && /opt/platformio/bin/pip install --no-cache-dir platformio \
    && ln -s /opt/platformio/bin/pio /usr/local/bin/pio \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
