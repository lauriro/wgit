
FROM docker.io/library/debian:testing-slim
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends gcc libc6-dev make pkg-config libgtk-3-dev git >/dev/null && rm -rf /var/lib/apt/lists/*

