# Build-Context ist das Repo-Root (gesetzt in docker-compose.yml)
#
# Reines statisches HTML ohne Build-Schritt — anders als beim Angular-Frontend
# reicht ein einzelnes nginx-Stage, kein Node-Build nötig.
FROM nginx:alpine

COPY second_page/ /usr/share/nginx/html

EXPOSE 80
