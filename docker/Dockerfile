# [2024/04/17:JCC] Force platform otherwise it does not work on Apple silicon.
FROM --platform=linux/amd64 ubuntu:22.04

RUN yes | unminimize \
    && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y locales && rm -rf /var/lib/apt/lists/* \
	&& localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG en_US.utf8

WORKDIR /work

COPY . LLVM-MCA-Daemon/

COPY docker/setup-ssh.sh ./setup/
RUN setup/setup-ssh.sh

COPY docker/setup-deps.sh ./setup/
RUN setup/setup-deps.sh

COPY docker/setup-llvm.sh ./setup/
RUN setup/setup-llvm.sh
 
COPY docker/setup-build.sh ./setup/
RUN setup/setup-build.sh

ENV PATH="/work/LLVM-MCA-Daemon/build:/opt/llvm-14/bin:$PATH"
RUN rm /etc/apt/apt.conf.d/docker-clean

ENTRYPOINT ["llvm-mcad"]

# [2024/04/17:JCC] Expose the MCAD server port.
EXPOSE 50052
