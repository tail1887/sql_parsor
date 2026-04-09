FROM ubuntu:latest

ENV TZ=Asia/Seoul LANG=ko_KR.UTF-8 LANGUAGE=ko_KR.UTF-8

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    locales tzdata build-essential sudo gdb vim curl git wget \
    python3 cmake make gcc valgrind telnet net-tools iproute2 \
    && locale-gen ko_KR.UTF-8 \
    && update-locale LANG=ko_KR.UTF-8

RUN useradd -m -s /bin/bash jungle && usermod -aG sudo jungle
RUN echo 'jungle:jungle' | chpasswd

USER jungle
WORKDIR /workspaces/week6-SQL-processor
