# ghcr.io/vagran/pulse-build:latest
FROM archlinux
LABEL org.opencontainers.image.source=https://github.com/vagran/pulse
LABEL org.opencontainers.image.description="Build and test environment for Pulse C++ framework"

RUN pacman -Syu --noconfirm
RUN pacman -S --noconfirm arm-none-eabi-gcc
RUN pacman -S --noconfirm arm-none-eabi-newlib
RUN pacman -S --noconfirm clang
RUN pacman -S --noconfirm llvm
RUN pacman -S --noconfirm lld
RUN pacman -S --noconfirm valgrind
RUN pacman -S --noconfirm cmake
RUN pacman -S --noconfirm ninja
RUN pacman -S --noconfirm git
RUN pacman -S --noconfirm catch2

ENV DEBUGINFOD_URLS=https://debuginfod.elfutils.org/
ENV DEBUGINFOD_CACHE_PATH=/opt/debuginfod-cache

RUN debuginfod-find debuginfo /usr/lib/ld-linux-x86-64.so.2
RUN debuginfod-find debuginfo /usr/lib/libc.so.6
