FROM node:22-bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 zlib1g && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY release ./release
COPY service ./service
RUN mkdir /app/data && chown node:node /app/data
USER node
ENV NODE_ENV=production BIND=0.0.0.0 PORT=8783 DATA_DIR=/app/data TEEWORLDS_ASSETS=/app/release
CMD ["node", "service/server.mjs"]
