# Build + test image for coduoplus_sv.so (and a reference runtime for coduo_lnxded).
#
# i386 userland: coduo_lnxded and the library are 32-bit ELF.
#
# Debian 11 (bullseye, glibc 2.31) on purpose: the library is linked against the
# oldest glibc we can reasonably build on, so it loads on any server distro from
# glibc 2.31 up (newer build hosts add symbol versions old hosts lack - upstream's
# glibc_compat.h exists for exactly that trap).
#
# coduo_lnxded (2005, gcc 3.2.3) needs libstdc++.so.5, which no current distro ships:
# it comes from the Debian archive, as upstream's server image does.
FROM i386/debian:bullseye-slim

ARG LIBSTDCPP5=http://archive.debian.org/debian/pool/main/g/gcc-3.3/libstdc++5_3.3.6-30_i386.deb

# The image's sources.list carries, commented out, the snapshot.debian.org timestamp the
# image was built from. Use exactly that: a frozen, self-consistent package set (the live
# bullseye-security mirror has served 404s for packages its index still listed), so the
# build is reproducible. This image compiles and tests; it is never a production server.
# snapshot.debian.org resets connections now and then, so every download retries.
RUN sed -i -e 's|^deb http://deb.debian.org|# deb http://deb.debian.org|' \
           -e 's|^# deb http://snapshot.debian.org|deb http://snapshot.debian.org|' /etc/apt/sources.list \
 && echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends g++ make binutils curl ca-certificates \
 && curl -fsSL --retry 5 --retry-all-errors -o /tmp/libstdc++5.deb "$LIBSTDCPP5" \
 && dpkg -i /tmp/libstdc++5.deb \
 && rm -f /tmp/libstdc++5.deb \
 && rm -rf /var/lib/apt/lists/* \
 && ldconfig -p | grep -q "libstdc++\.so\.5"

WORKDIR /src
