#!/bin/bash

VER=3.0
IMAGE_NAME=scorpio:${VER}

#RAM sizes
TR=$((45*1024))
NN=$((16*1024))
HT=$((32*1024))

build_image() {
     docker run --name temp -d -it --gpus=all ubuntu:18.04 bash
     docker exec -it temp /bin/bash -c " apt-get -y update && \
           DEBIAN_FRONTEND=noninteractive apt-get -y install wget unzip mpich && \
           echo '* hard core 0' >> /etc/security/limits.conf && \
           echo 'fs.suid_dumpable = 0' >> /etc/sysctl.conf && \
           useradd scorpio && \
           mkdir /home/scorpio && \
           chown scorpio:scorpio /home/scorpio && \
           cd /home/scorpio && \
           wget https://github.com/dshawul/Scorpio/releases/download/${VER}/install.sh && \
           chmod 755 install.sh && \
           ./install.sh --no-lcnets && \
           sed -i.bak -e 's/^nn_cache_size .*/nn_cache_size ${NN}/g' \
                      -e 's/^treeht.*/treeht ${TR}/' \
                      -e 's/^ht.*/ht ${HT}/' \
                      -e 's/^egbb_cache_size.*/egbb_cache_size 1024/' \
                      Scorpio/bin/Linux/scorpio.ini && \
           rm -rf Scorpio/bin/Linux/calibrate.dat"
     docker commit temp ${1}
     docker rm -f temp
}

build_image ${IMAGE_NAME} 
