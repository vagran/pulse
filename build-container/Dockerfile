# ghcr.io/vagran/pulse-build:latest
FROM archlinux
LABEL org.opencontainers.image.source=https://github.com/vagran/pulse
LABEL org.opencontainers.image.description="Build and test environment for Pulse C++ framework"

RUN printf '\n[core-debug]\nServer = https://geo.mirror.pkgbuild.com/core-debug/os/$arch\n\n[extra-debug]\nServer = https://geo.mirror.pkgbuild.com/extra-debug/os/$arch\n' >> /etc/pacman.conf

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
RUN pacman -S --noconfirm glibc-debug
