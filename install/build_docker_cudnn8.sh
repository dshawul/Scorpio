#!/bin/bash

VER=3.0
IMAGE_NAME=scorpio:${VER}

build_image() {
     docker run --name temp -d -it --gpus=all nvidia/cuda:11.1-cudnn8-runtime-ubuntu18.04 bash
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
           ./install.sh --only-trt 72 && \
           sed -i.bak -e 's/^nn_cache_size .*/nn_cache_size 16384/g' -e 's/^nn_cache_size_e .*/nn_cache_size_e 4096/g' Scorpio/bin/Linux/scorpio.ini && \
           rm -rf Scorpio/bin/Linux/calibrate.dat"
     docker commit temp ${1}
     docker rm -f temp
}

build_image ${IMAGE_NAME} 
