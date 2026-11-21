# Build from the repo root:  docker build -f cloud/Dockerfile -t loam .
FROM gcc:13 AS build
WORKDIR /src
COPY loam.h ./
COPY cloud/libloam.c cloud/
RUN gcc -O2 -fPIC -fvisibility=hidden -shared -o cloud/libloam.so cloud/libloam.c

FROM python:3.12-slim
RUN useradd -r -u 10001 loam && mkdir -p /data /srv/app && chown loam /data
WORKDIR /srv
COPY --from=build /src/cloud/libloam.so /srv/libloam.so
COPY cloud/server.py /srv/server.py
COPY app/web/index.html /srv/app/index.html
ENV LOAM_DATA=/data PORT=8080
VOLUME /data
EXPOSE 8080
USER loam
# LOAM_KEY must be supplied at run time (a secret); the server refuses to start without it.
CMD ["python3", "/srv/server.py"]
