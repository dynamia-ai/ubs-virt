FROM openeuler/openeuler:24.03-lts

LABEL maintainer="enpu-manager"
LABEL description="ENPU Manager Standalone Mode"

RUN dnf install -y \
    systemd \
    ca-certificates \
    && dnf clean all \
    && rm -rf /var/cache/dnf

RUN mkdir -p /etc/enpu /opt/enpu /var/lib/enpu-manager /var/log/enpu-manager

COPY build/output/enpu-manager /usr/bin/enpu-manager
COPY config/enpu-manager.conf /etc/enpu/enpu-manager.conf

RUN chmod +x /usr/bin/enpu-manager \
    && install -d -m 0750 /run/enpu

EXPOSE 50051

ENTRYPOINT ["/usr/bin/enpu-manager"]
