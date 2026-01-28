# syntax=docker/dockerfile:1
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      pkg-config \
      libcjson-dev \
      valgrind \
      cmake \
      make \
      build-essential \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
