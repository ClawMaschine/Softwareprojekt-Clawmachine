# Build-Context ist das Repo-Root (gesetzt in docker-compose.yml)

FROM python:3.11-slim

WORKDIR /app

COPY python_server/requirements.txt ./python_server/requirements.txt
RUN pip install --no-cache-dir -r python_server/requirements.txt

COPY python_server/ ./python_server/
COPY config.ini ./config.ini

CMD ["python", "-m", "python_server"]
