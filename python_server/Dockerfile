# Build-Context ist das Repo-Root (gesetzt in docker-compose.yml)

FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1

WORKDIR /app

# opencv-python-headless dlopen't diese Libs zur Laufzeit trotz "headless" —
# ohne sie schlägt "import cv2" im Container mit ImportError fehl.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libgl1 libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

COPY python_server/requirements.txt ./python_server/requirements.txt
RUN pip install --no-cache-dir -r python_server/requirements.txt

COPY python_server/ ./python_server/
COPY config.ini ./config.ini

# HTTP-Port für den Kamera-MJPEG-Stream (siehe [camera] in config.ini)
EXPOSE 8000

CMD ["python", "-m", "python_server"]
