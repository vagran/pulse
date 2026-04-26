# ghcr.io/vagran/pulse-build:latest
FROM archlinux
LABEL org.opencontainers.image.source=https://github.com/vagran/pulse
LABEL org.opencontainers.image.description="Build and test environment for Pulse C++ framework"

RUN pacman -Syu --noconfirm
RUN pacman -S --noconfirm arm-none-eabi-gcc
RUN pacman -S --noconfirm clang
RUN pacman -S --noconfirm valgrind
RUN pacman -S --noconfirm cmake
RUN pacman -S --noconfirm ninja
RUN pacman -S --noconfirm git
RUN pacman -S --noconfirm catch2
