FROM node:22-alpine
WORKDIR /app

# better-sqlite3 needs build tools on alpine
RUN apk add --no-cache python3 make g++

COPY package.json ./
RUN npm install --omit=dev

COPY src ./src
COPY public ./public

ENV NODE_ENV=production
ENV PORT=3000
ENV DATA_DIR=/data
VOLUME /data

EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=5s CMD wget -qO- http://127.0.0.1:3000/api/health || exit 1
CMD ["node", "src/index.js"]
