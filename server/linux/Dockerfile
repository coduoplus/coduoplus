# check=skip=FromPlatformFlagConstDisallowed
# CoDUO+ Linux server image: coduo_lnxded 1.51b + coduoplus_sv.so, ready to run.
# Build context = the REPOSITORY ROOT (the library is compiled from src/shared too):
#
#   docker build -f server/linux/Dockerfile -t coduoplus-server .
#
# (The check directive on line 1: the stages are i386 ON PURPOSE, whatever the build
# host - coduo_lnxded is a 32-bit binary - so the platform is a constant, which
# BuildKit's lint would otherwise flag.)
#
# Three stages:
#   build     compiles coduoplus_sv.so (server/linux/build.sh)
#   artifact  just the .so, for CI / releases:
#               docker build -f server/linux/Dockerfile --target artifact \
#                 --output type=local,dest=build/linux .
#   server    (default) the runtime image - see docs/DOCKER.md
#
# NO GAME FILES ARE IN THE IMAGE. coduo_lnxded, its game module and the pk3s are
# Activision's; they are mounted at run time (/game), so the image itself only holds
# Debian, libstdc++5 and our own code, and can be published.

# ---------------------------------------------------------------------------------
# build: Debian 11 i386 at the snapshot date the base image was cut from.
#   - i386: coduo_lnxded and the library are 32-bit ELF;
#   - Debian 11 (glibc 2.31): the oldest glibc we build on, so the library loads on any
#     server distro from there up (it ends up needing only GLIBC_2.17);
#   - the snapshot: a frozen, self-consistent package set (the live bullseye-security
#     mirror has served 404s for packages its index still listed) -> reproducible.
# ---------------------------------------------------------------------------------
FROM --platform=linux/386 debian:bullseye-slim AS build
RUN sed -i -e 's|^deb http://deb.debian.org|# deb http://deb.debian.org|' \
           -e 's|^# deb http://snapshot.debian.org|deb http://snapshot.debian.org|' /etc/apt/sources.list \
 && echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends g++ binutils \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY src/shared/ src/shared/
COPY server/linux/ server/linux/
RUN sh server/linux/build.sh

# ---------------------------------------------------------------------------------
# artifact: nothing but the library (docker build --target artifact --output ...)
# ---------------------------------------------------------------------------------
FROM scratch AS artifact
COPY --from=build /src/build/linux/coduoplus_sv.so /

# ---------------------------------------------------------------------------------
# server: Debian 12 i386, live (security updates - this one faces the internet).
# ---------------------------------------------------------------------------------
FROM --platform=linux/386 debian:bookworm-slim AS server

# coduo_lnxded (2005, gcc 3.2.3) needs libstdc++.so.5, which no current distro ships:
# taken from the Debian archive, pinned by hash. tini = PID 1: forwards SIGTERM to the
# server and reaps (a bare coduo_lnxded as PID 1 ignores `docker stop` for 10 s).
ARG LIBSTDCPP5_URL=http://archive.debian.org/debian/pool/main/g/gcc-3.3/libstdc++5_3.3.6-30_i386.deb
ARG LIBSTDCPP5_SHA256=c88f93cd5b7b32ca88b40f671fc09f3387c7fb514902b4737b471bd812786ee6
RUN apt-get update \
 && apt-get install -y --no-install-recommends tini curl ca-certificates \
 && curl -fsSL -o /tmp/libstdc++5.deb "$LIBSTDCPP5_URL" \
 && echo "$LIBSTDCPP5_SHA256  /tmp/libstdc++5.deb" | sha256sum -c - \
 && dpkg -i /tmp/libstdc++5.deb \
 && apt-get purge -y --auto-remove curl ca-certificates \
 && rm -rf /tmp/libstdc++5.deb /var/lib/apt/lists/* \
 && ldconfig -p | grep -q "libstdc++\.so\.5"

# unprivileged server user; /srv is assembled at every start, /data is its home
# (fs_homepath: logs, games_mp.log, downloaded/generated files)
RUN useradd --uid 1000 --home-dir /data --shell /usr/sbin/nologin coduo \
 && mkdir -p /srv /data /config && chown coduo:coduo /srv /data

COPY --from=build /src/build/linux/coduoplus_sv.so /opt/coduoplus/coduoplus_sv.so
COPY competitive.cfg.example /opt/coduoplus/competitive.cfg.example
COPY server/linux/server.cfg.example /opt/coduoplus/server.cfg.example
COPY server/linux/entrypoint.sh /opt/coduoplus/entrypoint.sh
COPY server/linux/healthcheck.sh /opt/coduoplus/healthcheck.sh

ENV PORT=28960 \
    DEDICATED=2 \
    CODUOPLUS_COMPETITIVE_FILE=/config/competitive.cfg

USER coduo
WORKDIR /srv
VOLUME ["/data"]
EXPOSE 28960/udp

# getinfo over UDP must be answered (the server is up and running a map)
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD ["bash", "/opt/coduoplus/healthcheck.sh"]

ENTRYPOINT ["tini", "--", "sh", "/opt/coduoplus/entrypoint.sh"]
# everything after the image name in `docker run` replaces this: engine arguments
CMD ["+exec", "server.cfg", "+map_rotate"]
