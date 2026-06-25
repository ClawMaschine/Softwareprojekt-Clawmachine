# ── Stage 1: Angular Build ────────────────────────────────────────────────────
FROM node:20-alpine AS builder

WORKDIR /app

COPY package*.json ./
RUN npm ci

COPY . .
RUN npm run build


# ── Stage 2: nginx ────────────────────────────────────────────────────────────
FROM nginx:alpine

# Angular 17 (esbuild builder) schreibt nach dist/<name>/browser/
COPY --from=builder /app/dist/clawmachine-frontend/browser /usr/share/nginx/html

COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
