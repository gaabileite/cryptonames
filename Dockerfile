# CryptoNames server image. Works on any host that runs containers (Fly.io, Railway, Render, a VPS with Docker).
#   docker build -t cryptonames .
#   docker run -p 8080:8080 cryptonames        then open http://localhost:8080

# Step 1: build the server
FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY third_party ./third_party
RUN make build/server

# Step 2: only what is needed to run it. The server finds data/, assets/images/ and web/ next to where it runs.
FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/server ./server
COPY data ./data
COPY assets/images ./assets/images
COPY web ./web
USER nobody
# The host can choose another port with the PORT variable; with it set the server listens on 0.0.0.0
ENV PORT=8080
EXPOSE 8080
CMD ["./server"]
