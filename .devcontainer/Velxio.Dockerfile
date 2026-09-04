FROM node:24.3.0-bookworm-slim@sha256:8225b1806c6e37dced949224b5c0d8278a2fe593967288620e0af69b2cbc4539 AS node
FROM ghcr.io/davidmonterocrespo24/velxio@sha256:117a82cc52ec7168b790bc8553c68fb1fcd86a16db202b847f948f7a573691d2
COPY --from=node /usr/local/bin/node /usr/local/bin/node
RUN python -m pip install --no-cache-dir Pillow==11.3.0 PyYAML==6.0.2
ENTRYPOINT ["python"]
