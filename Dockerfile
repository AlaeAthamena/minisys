# Multi-stage Docker build for minisys high-performance C server
FROM alpine:3.19 AS builder

RUN apk add --no-libc-dev build-base clang make

WORKDIR /app

COPY . .

RUN make clean && make

# Minimal Runtime Container (<10MB total footprint)
FROM alpine:3.19

RUN apk add --no-cache libgcc

WORKDIR /app

COPY --from=builder /app/bin/minisysd /app/minisysd
COPY --from=builder /app/bin/minisys-cli /app/minisys-cli
COPY --from=builder /app/bin/minisys-bench /app/minisys-bench
COPY --from=builder /app/web /app/web

EXPOSE 8080

CMD ["/app/minisysd", "-p", "8080", "-t", "8", "-d", "/app/data/minisys.aof", "-w", "/app/web"]
