# Build-Context ist das Repo-Root (gesetzt in docker-compose.yml)

# ── Stage 1: Angular Build ────────────────────────────────────────────────────
FROM node:20-alpine AS builder

# WORKDIR spiegelt die echte Repo-Struktur (Repo-Root, mit frontend/ als
# Unterordner) statt nur frontend/ zu kopieren — scripts/dev/generate_frontend_
# environment.py bestimmt seinen Projekt-Root relativ zu sich selbst und
# erwartet daher config.ini und frontend/ an denselben Stellen wie im Repo.
WORKDIR /repo

COPY frontend/package*.json ./frontend/
RUN cd frontend && npm ci

COPY frontend/ ./frontend/

# environment.ts aus config.ini erzeugen, bevor Angular baut — python3 ist im
# node:alpine-Image nicht enthalten, daher hier kurz nachinstalliert.
COPY config.ini ./config.ini
COPY scripts/dev/generate_frontend_environment.py ./scripts/dev/generate_frontend_environment.py
RUN apk add --no-cache python3 \
    && python3 scripts/dev/generate_frontend_environment.py

RUN cd frontend && npm run build


# ── Stage 2: nginx ────────────────────────────────────────────────────────────
FROM nginx:alpine

# Angular 17 (esbuild builder) schreibt nach dist/<name>/browser/
COPY --from=builder /repo/frontend/dist/clawmachine-frontend/browser /usr/share/nginx/html

COPY docker/frontend/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
