#!/bin/bash

VER=3.0
IMAGE_NAME=scorpio:${VER}

build_image() {
     docker run --name temp -d -it --gpus=all nvidia/cuda:10.0-runtime-ubuntu18.04 bash
     docker exec -it temp /bin/bash -c " apt-get -y update && \
           DEBIAN_FRONTEND=noninteractive apt-get -y install wget unzip && \
           echo '* hard core 0' >> /etc/security/limits.conf && \
           echo 'fs.suid_dumpable = 0' >> /etc/sysctl.conf && \
           useradd scorpio && \
           mkdir /home/scorpio && \
           chown scorpio:scorpio /home/scorpio && \
           cd /home/scorpio && \
           wget https://github.com/dshawul/Scorpio/releases/download/${VER}/install.sh && \
           chmod 755 install.sh && \
           ./install.sh -t 68 -p HALF && \
           rm -rf Scorpio/bin/Linux/calibrate.dat"
     docker commit temp ${1}
     docker rm -f temp
}

build_image ${IMAGE_NAME} 
