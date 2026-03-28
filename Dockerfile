# Stage 1: Build SvelteKit UI
FROM node:20-alpine AS ui-builder
WORKDIR /app
COPY ui/package*.json ./
RUN npm ci
COPY ui/ ./
RUN npm run build

# Stage 2: Build Go web server
FROM golang:1.22-alpine AS go-builder
WORKDIR /app
COPY go-server/ .
RUN CGO_ENABLED=0 GOOS=linux go build -mod=mod -o webserver .

# Stage 3: Build C binaries
FROM ubuntu:24.04 AS c-builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  make \
  gcc \
  libc6-dev \
  && rm -rf /var/lib/apt/lists/*

COPY emu/ /build/emu/

WORKDIR /build/emu
RUN make

# Stage 4: Final image
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# No nginx or php needed — the Go binary handles HTTP and SHM reads
RUN apt-get update && apt-get install -y --no-install-recommends \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

# Copy compiled binaries and ROM pages (emulator looks for rom/ relative to its cwd)
COPY --from=c-builder /build/emu/tamaemu /app/emu/tamaemu
COPY --from=c-builder /build/emu/rom/ /app/emu/rom/

# Copy all base EEPROMs; one is randomly chosen per tama at runtime
COPY roms/ /app/roms/
ENV TAMA_COUNT=9
ENV SAVE_INTERVAL=3600

VOLUME /app/state

# Copy Go web server
COPY --from=go-builder /app/webserver /app/webserver

# Copy SvelteKit static build
COPY --from=ui-builder /app/build /var/www/html/

COPY run.sh /app/run.sh
RUN chmod 755 /app/run.sh

EXPOSE 80
ENTRYPOINT ["/app/run.sh"]
