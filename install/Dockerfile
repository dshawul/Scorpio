FROM ubuntu:18.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get -y install \
    git wget vim unzip && \
    echo "* hard core 0" >> /etc/security/limits.conf && \
    echo 'fs.suid_dumpable = 0' >> /etc/sysctl.conf && \
    useradd scorpio && \
    mkdir /home/scorpio && \
    chown scorpio:scorpio /home/scorpio && \
    wget -O /home/scorpio/install.sh https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh && \
    chmod 755 /home/scorpio/install.sh
    
WORKDIR /home/scorpio

USER scorpio
    
RUN ./install.sh

