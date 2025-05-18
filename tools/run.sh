#!/bin/sh
update-binfmts --enable
if [ ! -e "alpine-root/root/ZynqSDR/CMakeLists.txt" ]; then
  mkdir -p alpine-root/root/ZynqSDR
  mount --bind ../rx888/web-888/server alpine-root/root/ZynqSDR
fi
if [ ! -e "alpine-root/root/red-pitaya-notes/Makefile" ]; then
  mkdir -p alpine-root/root/red-pitaya-notes
  mount --bind ../rx888/red-pitaya-notes alpine-root/root/red-pitaya-notes
fi
if [ ! -e "alpine-root/root/wsjtx/CMakeLists.txt" ]; then
  mkdir -p alpine-root/root/wsjtx
  mount --bind ../rx888/wsjtx alpine-root/root/wsjtx
fi
#if [ ! -e "alpine-root/root/dumphfdl/CMakeLists.txt" ]; then
#  mkdir -p alpine-root/root/dumphfdl
#  mount --bind ../rx888/dumphfdl alpine-root/root/dumphfdl
#fi
chroot alpine-root /bin/bash --login
