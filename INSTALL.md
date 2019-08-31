# Installation instructions

The recommended way to install ScorpioNN is using scripts, a batch file for Windows and a shell script for Linux.

Here are the steps I followed to install and run it on a GPU machine on linux.

   * Download install.bat, and move it to a directory you want scorpio installed.

   * Run it from the command line (preferable) or double-click it and it will download, install and configure scorpio.ini. 
     This will generate a scorpio folder with the current date (Scorpio-mm-yyyy). During installation, 
     it will make two test runs with "delay 0" and "delay 1" in the end. See which one gets the higher pps value for you, 
     default is delay 0 but delay 1 could be sometimes better when you have few cores. 
     Change the value in bin/Windows/scorpio.ini if delay=1 happens to be better.

   * Install scorpio.bat in your GPU instead of scorpio.exe.

For RTX GPUs and other GPUS that support INT8, I recommend you using INT8 instead of HALF precision since it will give a 2.5x speedup bump. 
Once you make sure the installation is working do the following steps to get INT8 working

  * Go to bin/Windows directory from the command line and run the following

        scorpio.bat runinpnn calibrate.epd calibrate.dat quit

This will do INT8 calibration and generate a calibration file about 1.4G in size
  * Then change `float_type   INT8` in scorpio.ini and run

        scorpio.bat go quit

The first time you run it, it might take upto 2 minutes to load the NN so be patient. 
The next runs will only take a couple of seconds to load the neural network. If everything works out, 
you should see an increase of 2.5x speedup or more. The values that matter is the pps/nneps not the nps value.

Scorpio now supports UCI protocol as well as xboard. Use xboard whenevr you can since that has full features
analysis, pondering etc and is also slightly stronger at fast time controls.

The egbbdll is used for probing endgame bitbases as well as neural networks. So if you want to use bitbases as well
you have to put your egbb files in the nnprobe-windows-gpu directory. Be careful not to overwrite egbbdll64.dll with an
old version that comes prepackaged with the egbbfiles. The old version is 4.1 and the new version is 4.3, so copy
only the egbb files and put them in the directory where the NN dlls are located.

## Step-by-step installation on a Linux Machine with RTX 2070-super

Change to directoy you install engines

    daniel@danidesti-desktop:~/engines$ pwd
    /home/daniel/engines

Download installer script

    daniel@danidesti-desktop:~/engines$ wget https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh
    --2019-08-17 09:50:18--  https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh
    Resolving github.com (github.com)... 140.82.114.3
    Connecting to github.com (github.com)|140.82.114.3|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-90ed-77b58fb3e91b?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155018Z&X-Amz-Expires=300&X-Amz-Signature=a8f261c9a4e89d97617533a6e2a61be929ca242907688113b4863e3e633ee1c1&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dinstall.sh&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:50:18--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-90ed-77b58fb3e91b?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155018Z&X-Amz-Expires=300&X-Amz-Signature=a8f261c9a4e89d97617533a6e2a61be929ca242907688113b4863e3e633ee1c1&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dinstall.sh&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.216.184.187
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.216.184.187|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 3414 (3.3K) [application/octet-stream]
    Saving to: ?install.sh?
    
    install.sh                                         100%[================================================================================================================>]   3.33K  --.-KB/s    in 0s      
    
    2019-08-17 09:50:19 (93.8 MB/s) - ?install.sh? saved [3414/3414]

Change permissions to executable and run it
    
    daniel@danidesti-desktop:~/engines$ chmod +x install.sh 
    daniel@danidesti-desktop:~/engines$ ./install.sh 
    + OSD=windows
    + [[ linux-gnu == \l\i\n\u\x\-\g\n\u ]]
    + OSD=ubuntu
    ++ grep -c '^processor' /proc/cpuinfo
    + CPUS=24
    ++ which nvidia-smi
    + '[' '!' -z /usr/bin/nvidia-smi ']'
    ++ nvidia-smi --query-gpu=name --format=csv,noheader
    ++ wc -l
    + GPUS=1
    + DEV=gpu
    + OS=ubuntu
    + DEV=gpu
    + VERSION=3.0
    ++ echo 3.0
    ++ tr -d .
    + VR=30
    + EGBB=nnprobe-ubuntu-gpu
    + NET='nets-ccrl-cegt nets-lczero nets-maddex'
    + '[' gpu = gpu ']'
    + nn_type=1
    ++ date +%d-%b-%Y
    + SCORPIO=Scorpio-17-Aug-2019
    + mkdir -p Scorpio-17-Aug-2019
    + cd Scorpio-17-Aug-2019
    + LNK=https://github.com/dshawul/Scorpio/releases/download
    + wget --no-check-certificate https://github.com/dshawul/Scorpio/releases/download/3.0/nnprobe-ubuntu-gpu.zip
    --2019-08-17 09:51:22--  https://github.com/dshawul/Scorpio/releases/download/3.0/nnprobe-ubuntu-gpu.zip
    Resolving github.com (github.com)... 140.82.113.4
    Connecting to github.com (github.com)|140.82.113.4|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/8733ad80-c048-11e9-9842-b74e0dd400de?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155123Z&X-Amz-Expires=300&X-Amz-Signature=7c68523ee538d77490494d78466d458ceb9fa0945e4e9ca29fd5dcb2be0352a9&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnnprobe-ubuntu-gpu.zip&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:51:23--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/8733ad80-c048-11e9-9842-b74e0dd400de?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155123Z&X-Amz-Expires=300&X-Amz-Signature=7c68523ee538d77490494d78466d458ceb9fa0945e4e9ca29fd5dcb2be0352a9&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnnprobe-ubuntu-gpu.zip&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.216.169.123
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.216.169.123|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 330753343 (315M) [application/octet-stream]
    Saving to: ?nnprobe-ubuntu-gpu.zip?
    
    nnprobe-ubuntu-gpu.zip                             100%[================================================================================================================>] 315.43M  21.3MB/s    in 23s     
    
    2019-08-17 09:51:46 (13.4 MB/s) - ?nnprobe-ubuntu-gpu.zip? saved [330753343/330753343]
    
    + unzip -o nnprobe-ubuntu-gpu.zip
    Archive:  nnprobe-ubuntu-gpu.zip
       creating: nnprobe-ubuntu-gpu/
      inflating: nnprobe-ubuntu-gpu/TENSORT.txt  
      inflating: nnprobe-ubuntu-gpu/libnvparsers.so.5  
      inflating: nnprobe-ubuntu-gpu/libnnprobe.so  
      inflating: nnprobe-ubuntu-gpu/libnvinfer.so.5  
      inflating: nnprobe-ubuntu-gpu/libcudnn.so.7  
      inflating: nnprobe-ubuntu-gpu/CUDNN.txt  
      inflating: nnprobe-ubuntu-gpu/libcublas.so.10.0  
      inflating: nnprobe-ubuntu-gpu/libcudart.so.10.0  
      inflating: nnprobe-ubuntu-gpu/CUDA.txt  
      inflating: nnprobe-ubuntu-gpu/egbbso64.so  
    + for N in $NET
    + wget --no-check-certificate https://github.com/dshawul/Scorpio/releases/download/3.0/nets-ccrl-cegt.zip
    --2019-08-17 09:51:50--  https://github.com/dshawul/Scorpio/releases/download/3.0/nets-ccrl-cegt.zip
    Resolving github.com (github.com)... 140.82.113.4
    Connecting to github.com (github.com)|140.82.113.4|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-8d5a-8de2225d9ebb?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155151Z&X-Amz-Expires=300&X-Amz-Signature=e3121168710ad641d1c4d11294635f982f41f33da14de17a93ac09a6b1370d84&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-ccrl-cegt.zip&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:51:51--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-8d5a-8de2225d9ebb?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155151Z&X-Amz-Expires=300&X-Amz-Signature=e3121168710ad641d1c4d11294635f982f41f33da14de17a93ac09a6b1370d84&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-ccrl-cegt.zip&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.216.165.251
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.216.165.251|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 201947847 (193M) [application/octet-stream]
    Saving to: ?nets-ccrl-cegt.zip?
    
    nets-ccrl-cegt.zip                                 100%[================================================================================================================>] 192.59M  21.4MB/s    in 9.3s    
    
    2019-08-17 09:52:00 (20.6 MB/s) - ?nets-ccrl-cegt.zip? saved [201947847/201947847]
    
    + unzip -o nets-ccrl-cegt.zip
    Archive:  nets-ccrl-cegt.zip
       creating: nets-ccrl-cegt/
      inflating: nets-ccrl-cegt/net-20x256.uff  
      inflating: nets-ccrl-cegt/net-6x64.uff  
      inflating: nets-ccrl-cegt/net-12x128.pb  
      inflating: nets-ccrl-cegt/net-6x64.pb  
      inflating: nets-ccrl-cegt/net-20x256.pb  
      inflating: nets-ccrl-cegt/net-12x128.uff  
      inflating: nets-ccrl-cegt/net-2x32.pb  
      inflating: nets-ccrl-cegt/net-2x32.uff  
    + for N in $NET
    + wget --no-check-certificate https://github.com/dshawul/Scorpio/releases/download/3.0/nets-lczero.zip
    --2019-08-17 09:52:02--  https://github.com/dshawul/Scorpio/releases/download/3.0/nets-lczero.zip
    Resolving github.com (github.com)... 140.82.114.4
    Connecting to github.com (github.com)|140.82.114.4|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-87f8-eae70963db57?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155202Z&X-Amz-Expires=300&X-Amz-Signature=3fe0b6e6f4f628f63e30c9efb2a642aed4d6a5b38ca40aa01d8b429bc20437c9&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-lczero.zip&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:52:02--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-87f8-eae70963db57?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155202Z&X-Amz-Expires=300&X-Amz-Signature=3fe0b6e6f4f628f63e30c9efb2a642aed4d6a5b38ca40aa01d8b429bc20437c9&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-lczero.zip&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.216.160.155
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.216.160.155|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 67458244 (64M) [application/octet-stream]
    Saving to: ?nets-lczero.zip?
    
    nets-lczero.zip                                    100%[================================================================================================================>]  64.33M  20.6MB/s    in 3.3s    
    
    2019-08-17 09:52:06 (19.7 MB/s) - ?nets-lczero.zip? saved [67458244/67458244]
    
    + unzip -o nets-lczero.zip
    Archive:  nets-lczero.zip
       creating: nets-lczero/
      inflating: nets-lczero/ID-32930.uff  
    + for N in $NET
    + wget --no-check-certificate https://github.com/dshawul/Scorpio/releases/download/3.0/nets-maddex.zip
    --2019-08-17 09:52:06--  https://github.com/dshawul/Scorpio/releases/download/3.0/nets-maddex.zip
    Resolving github.com (github.com)... 140.82.114.4
    Connecting to github.com (github.com)|140.82.114.4|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-9118-ad1be9de9497?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155207Z&X-Amz-Expires=300&X-Amz-Signature=0385d7bd12826a05b02183838542d46539b75c72f189acf4801b3908023f0e38&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-maddex.zip&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:52:07--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/869b1700-c048-11e9-9118-ad1be9de9497?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155207Z&X-Amz-Expires=300&X-Amz-Signature=0385d7bd12826a05b02183838542d46539b75c72f189acf4801b3908023f0e38&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dnets-maddex.zip&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.217.37.172
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.217.37.172|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 68766085 (66M) [application/octet-stream]
    Saving to: ?nets-maddex.zip?
    
    nets-maddex.zip                                    100%[================================================================================================================>]  65.58M  21.3MB/s    in 3.3s    
    
    2019-08-17 09:52:10 (19.6 MB/s) - ?nets-maddex.zip? saved [68766085/68766085]
    
    + unzip -o nets-maddex.zip
    Archive:  nets-maddex.zip
       creating: nets-maddex/
      inflating: nets-maddex/net-maddex.uff  
    + wget --no-check-certificate https://github.com/dshawul/Scorpio/releases/download/3.0/scorpio30-mcts-nn.zip
    --2019-08-17 09:52:11--  https://github.com/dshawul/Scorpio/releases/download/3.0/scorpio30-mcts-nn.zip
    Resolving github.com (github.com)... 140.82.114.4
    Connecting to github.com (github.com)|140.82.114.4|:443... connected.
    HTTP request sent, awaiting response... 302 Found
    Location: https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/96454a80-c09e-11e9-93af-9f86ccabdff9?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155212Z&X-Amz-Expires=300&X-Amz-Signature=c11cf62909307c4142ba92526f95bc681478c53dfc3b3e30e39af4ca97d12aaa&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dscorpio30-mcts-nn.zip&response-content-type=application%2Foctet-stream [following]
    --2019-08-17 09:52:12--  https://github-production-release-asset-2e65be.s3.amazonaws.com/666654/96454a80-c09e-11e9-93af-9f86ccabdff9?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=AKIAIWNJYAX4CSVEH53A%2F20190817%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20190817T155212Z&X-Amz-Expires=300&X-Amz-Signature=c11cf62909307c4142ba92526f95bc681478c53dfc3b3e30e39af4ca97d12aaa&X-Amz-SignedHeaders=host&actor_id=0&response-content-disposition=attachment%3B%20filename%3Dscorpio30-mcts-nn.zip&response-content-type=application%2Foctet-stream
    Resolving github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)... 52.217.37.172
    Connecting to github-production-release-asset-2e65be.s3.amazonaws.com (github-production-release-asset-2e65be.s3.amazonaws.com)|52.217.37.172|:443... connected.
    HTTP request sent, awaiting response... 200 OK
    Length: 3438247 (3.3M) [application/octet-stream]
    Saving to: ?scorpio30-mcts-nn.zip?
    
    scorpio30-mcts-nn.zip                              100%[================================================================================================================>]   3.28M  7.25MB/s    in 0.5s    
    
    2019-08-17 09:52:12 (7.25 MB/s) - ?scorpio30-mcts-nn.zip? saved [3438247/3438247]
    
    + unzip -o scorpio30-mcts-nn.zip
    Archive:  scorpio30-mcts-nn.zip
       creating: bin/
       creating: bin/Linux/
       creating: bin/Linux/log/
     extracting: bin/Linux/log/.gitignore  
      inflating: bin/Linux/calibrate.epd  
      inflating: bin/Linux/scorpio       
      inflating: bin/Linux/scorpio.sh    
      inflating: bin/Linux/scorpio.bat   
      inflating: bin/Linux/scorpio.ini   
       creating: bin/Windows/
       creating: bin/Windows/log/
     extracting: bin/Windows/log/.gitignore  
      inflating: bin/Windows/calibrate.epd  
      inflating: bin/Windows/scorpio.sh  
      inflating: bin/Windows/scorpio.exe  
      inflating: bin/Windows/scorpio.bat  
      inflating: bin/Windows/scorpio.ini  
       creating: bin/Android/
       creating: bin/Android/log/
     extracting: bin/Android/log/.gitignore  
      inflating: bin/Android/calibrate.epd  
      inflating: bin/Android/scorpio     
      inflating: bin/Android/scorpio.sh  
      inflating: bin/Android/scorpio.bat  
      inflating: bin/Android/scorpio.ini  
      inflating: bin/Windows/Wb2Uci.exe  
      inflating: bin/Windows/scorpio-uci.bat  
      inflating: bin/Windows/Wb2Uci.eng  
    + rm -rf nets-ccrl-cegt.zip nets-lczero.zip nets-maddex.zip nnprobe-ubuntu-gpu.zip scorpio30-mcts-nn.zip
    + chmod 755 nnprobe-ubuntu-gpu
    + cd nnprobe-ubuntu-gpu
    + chmod 755 CUDA.txt CUDNN.txt egbbso64.so libcublas.so.10.0 libcudart.so.10.0 libcudnn.so.7 libnnprobe.so libnvinfer.so.5 libnvparsers.so.5 TENSORT.txt
    + cd ../..
    + delay=0
    + '[' gpu = gpu ']'
    + '[' 24 -le 4 ']'
    + '[' 1 -ge 2 ']'
    + '[' 24 -eq 1 ']'
    + mt=128
    + cd Scorpio-17-Aug-2019
    ++ pwd
    + PD=/home/daniel/engines/Scorpio-17-Aug-2019
    ++ echo /home/daniel/engines/Scorpio-17-Aug-2019
    ++ sed 's/\/cygdrive//g'
    + PD=/home/daniel/engines/Scorpio-17-Aug-2019
    ++ echo /home/daniel/engines/Scorpio-17-Aug-2019
    ++ sed 's/\/c\//c:\//g'
    + PD=/home/daniel/engines/Scorpio-17-Aug-2019
    + egbbp=/home/daniel/engines/Scorpio-17-Aug-2019/nnprobe-ubuntu-gpu
    + '[' 1 -eq 1 ']'
    + nnp=/home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    + '[' ubuntu = windows ']'
    + '[' ubuntu = android ']'
    + exep=/home/daniel/engines/Scorpio-17-Aug-2019/bin/Linux
    + cd /home/daniel/engines/Scorpio-17-Aug-2019/bin/Linux
    ++ echo /home/daniel/engines/Scorpio-17-Aug-2019/nnprobe-ubuntu-gpu
    ++ sed 's_/_\\/_g'
    + egbbp_='\/home\/daniel\/engines\/Scorpio-17-Aug-2019\/nnprobe-ubuntu-gpu'
    ++ echo /home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    ++ sed 's_/_\\/_g'
    + nnp_='\/home\/daniel\/engines\/Scorpio-17-Aug-2019\/nets-maddex\/net-maddex.uff'
    + sed -i 's/^egbb_path.*/egbb_path                \/home\/daniel\/engines\/Scorpio-17-Aug-2019\/nnprobe-ubuntu-gpu/g' scorpio.ini
    + sed -i 's/^nn_path.*/nn_path                  \/home\/daniel\/engines\/Scorpio-17-Aug-2019\/nets-maddex\/net-maddex.uff/g' scorpio.ini
    + sed -i 's/^nn_type.*/nn_type                  1/g' scorpio.ini
    + sed -i 's/^delay.*/delay                  0/g' scorpio.ini
    + '[' gpu = gpu ']'
    + sed -i 's/^device_type.*/device_type              GPU/g' scorpio.ini
    + sed -i 's/^n_devices.*/n_devices                1/g' scorpio.ini
    + sed -i 's/^mt.*/mt                  128/g' scorpio.ini
    + '[' ubuntu = windows ']'
    + EXE=scorpio.sh
    + cd ../..
    + echo 'Runnind with delay 0'
    Runnind with delay 0
    + /home/daniel/engines/Scorpio-17-Aug-2019/bin/Linux/scorpio.sh delay 0 go quit
    feature done=0
    Number of cores 24 of 24
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 41932800 X 320 = 12796.9 MB
    processors [128]
    EgbbProbe 4.3 by Daniel Shawul
    egbb_cache 4084 X 8216 = 32.0 MB
    0 egbbs loaded !      
    nn_cache 131072 X 1552 = 194.0 MB
    Loading neural network : /home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    With "HALF" precision
    Loading graph on /gpu:0
    0. main_input 7168 = 112 8 8
    1. value_head 1 = 1 1 1
    2. policy_head 1858 = 1858 1 1
    Neural network loaded !	
    loading_time = 25s
    # rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    # [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 32 111 9653  d2-d4 Ng8-f6 c2-c4 e7-e6 Ng1-f3 d7-d5 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    64 28 223 20971  e2-e4 e7-e6 Nb1-c3 d7-d5 d2-d4 Ng8-f6 e4-e5 Nf6-d7 f2-f4 c7-c5 Ng1-f3 Nb8-c6 Bc1-e3 Bf8-e7
    65 28 336 33072  e2-e4 e7-e6 Nb1-c3 d7-d5 d2-d4 Ng8-f6 e4-e5 Nf6-d7 f2-f4 c7-c5 Ng1-f3 Nb8-c6 Bc1-e3 Bf8-e7 Qd1-d2 Ke8-g8
    66 28 448 46462  d2-d4 d7-d5 e2-e3 Ng8-f6 c2-c4 e7-e6 Ng1-f3 Bf8-e7 Bf1-e2 Ke8-g8 Ke1-g1
    67 32 560 60316  d2-d4 Ng8-f6 c2-c4 e7-e6 e2-e3 d7-d5 Ng1-f3 Bf8-e7 Bf1-e2 Ke8-g8 Ke1-g1 d5xc4 Be2xc4
    68 36 672 72303  e2-e4 c7-c5 Nb1-c3 a7-a6 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7
    69 30 786 84401  Ng1-f3 Ng8-f6 c2-c4 e7-e6 d2-d4 d7-d5 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    70 32 899 95874  e2-e4 c7-c5 Ng1-f3 Nb8-c6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Nb1-c3 e7-e6 a2-a3 Qd8-c7 Bc1-e3 a7-a6 f2-f4 d7-d6
    71 41 996 105112  e2-e4 c7-c5 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Nb1-c3 a7-a6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7 f2-f3 Bc8-e6 Qd1-d2 Nb8-d7
    
    # Move   Value=(V,P,V+P)   Policy  Visits                  PV
    #----------------------------------------------------------------------------------
    #  1   (0.536,0.527,0.532)  19.60   31999   d2-d4 Ng8-f6 Ng1-f3 d7-d5 c2-c4 e7-e6 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2 d5xc4 Nf3-e5
    #  2   (0.559,0.527,0.543)  19.41   46868   e2-e4 c7-c5 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Nb1-c3 a7-a6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7 f2-f3 Bc8-e6 Qd1-d2 Nb8-d7
    #  3   (0.538,0.527,0.533)   9.25    9071   c2-c4 e7-e5 Nb1-c3 Nb8-c6 g2-g3 g7-g6 Bf1-g2 Bf8-g7 d2-d3 Ng8-e7 e2-e3
    #  4   (0.538,0.527,0.532)   8.98   10808   Ng1-f3 Ng8-f6 c2-c4 e7-e6 d2-d4 d7-d5 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    #  5   (0.507,0.527,0.507)   4.80    1123   g2-g3 d7-d5 Ng1-f3 c7-c5 Bf1-g2 Ng8-f6 Ke1-g1 Nb8-c6 d2-d4 c5xd4 Nf3xd4 e7-e5
    #  6   (0.527,0.527,0.527)   4.48    2105   e2-e3 e7-e6 c2-c4 d7-d5 Ng1-f3 Ng8-f6 d2-d4 Bf8-e7 Bf1-e2
    #  7   (0.481,0.527,0.481)   3.49     339   b2-b3 d7-d5 Bc1-b2 Ng8-f6 Ng1-f3 Bc8-f5 Nf3-h4 Bf5-d7 Nh4-f3
    #  8   (0.510,0.527,0.510)   3.32     571   c2-c3 d7-d5 d2-d4 Ng8-f6 Ng1-f3 e7-e6 Bc1-f4 Bf8-d6 e2-e3
    #  9   (0.514,0.527,0.514)   3.16     559   Nb1-c3 d7-d5 d2-d4 Ng8-f6 Bc1-f4 a7-a6 e2-e3 e7-e6 Ng1-f3 c7-c5 Bf1-e2
    # 10   (0.482,0.527,0.482)   3.15     308   d2-d3 d7-d5 Ng1-f3 Ng8-f6 g2-g3 c7-c5 Bf1-g2 Nb8-c6 Ke1-g1
    # 11   (0.450,0.527,0.450)   2.95     194   f2-f4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 c7-c5 b2-b3 g7-g6
    # 12   (0.463,0.527,0.463)   2.56     194   b2-b4 e7-e5 Bc1-b2 Bf8xb4 Bb2xe5 Ng8-f6 c2-c3 Bb4-e7 e2-e3 Ke8-g8 d2-d4
    # 13   (0.479,0.527,0.479)   2.37     231   h2-h3 e7-e5 e2-e3 d7-d5 d2-d4 Nb8-c6 Ng1-f3 e5-e4 Nf3-d2
    # 14   (0.498,0.527,0.498)   2.36     351   a2-a3 Ng8-f6 d2-d4 g7-g6 c2-c4 Bf8-g7 Nb1-c3 d7-d5 c4xd5
    # 15   (0.407,0.527,0.407)   2.03      87   Ng1-h3 d7-d5 g2-g3 e7-e5 d2-d4 e5xd4 Qd1xd4
    # 16   (0.450,0.527,0.450)   1.84     113   a2-a4 e7-e5 e2-e4 Ng8-f6 Nb1-c3 Bf8-b4 Ng1-f3 Ke8-g8 Nf3xe5
    # 17   (0.449,0.527,0.449)   1.67     101   h2-h4 d7-d5 d2-d4 c7-c5 d4xc5 Ng8-f6 Ng1-f3 e7-e6
    # 18   (0.347,0.527,0.347)   1.63      53   g2-g4 d7-d5 h2-h3 e7-e5 Bf1-g2 Nb8-c6 Nb1-c3
    # 19   (0.394,0.527,0.394)   1.59      60   f2-f3 e7-e5 Nb1-c3 Ng8-f6 d2-d4 e5xd4 Qd1xd4
    # 20   (0.423,0.527,0.423)   1.35      63   Nb1-a3 e7-e5 e2-e3 d7-d5 d2-d4 e5-e4 c2-c4
    
    # nodes = 969194 <0% qnodes> time = 10001ms nps = 96909 eps = 0 nneps = 10569
    # Tree: nodes = 143200 depth = 20 pps = 10537 visits = 105199 
    #       qsearch_calls = 0 search_calls = 0
    move e2e4
    Bye Bye
    + echo 'Runnind with delay 1'
    Runnind with delay 1
    + /home/daniel/engines/Scorpio-17-Aug-2019/bin/Linux/scorpio.sh delay 1 go quit
    feature done=0
    Number of cores 24 of 24
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 41932800 X 320 = 12796.9 MB
    processors [128]
    EgbbProbe 4.3 by Daniel Shawul
    egbb_cache 4084 X 8216 = 32.0 MB
    0 egbbs loaded !      
    nn_cache 131072 X 1552 = 194.0 MB
    Loading neural network : /home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    With "HALF" precision
    Loading graph on /gpu:0
    0. main_input 7168 = 112 8 8
    1. value_head 1 = 1 1 1
    2. policy_head 1858 = 1858 1 1
    Neural network loaded !	
    loading_time = 1s
    # rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    # [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 33 112 9558  d2-d4 Ng8-f6 Ng1-f3 d7-d5 c2-c4 e7-e6 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    64 28 224 20484  e2-e4 e7-e6 Nb1-c3 d7-d5 d2-d4 Ng8-f6 e4-e5 Nf6-d7 f2-f4 c7-c5 Ng1-f3 Nb8-c6 Bc1-e3 Bf8-e7
    65 28 336 31970  d2-d4 d7-d5 c2-c4 e7-e6 Nb1-c3 Ng8-f6 Ng1-f3 Bf8-e7 Bc1-g5 Ke8-g8 e2-e3 h7-h6
    66 28 450 45511  d2-d4 Ng8-f6 c2-c4 e7-e6 g2-g3 d7-d5 Bf1-g2 Bf8-e7 Ng1-f3 Ke8-g8 Ke1-g1 c7-c6 Qd1-c2 Nb8-d7
    67 32 562 58667  d2-d4 e7-e6 c2-c4 Ng8-f6 Ng1-f3 d7-d5 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    68 35 674 69683  e2-e4 c7-c5 Nb1-c3 a7-a6 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7
    69 27 786 81526  e2-e4 c7-c5 Ng1-f3 d7-d6 Nb1-c3 Ng8-f6 d2-d4 c5xd4 Nf3xd4 a7-a6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7 f2-f3
    70 29 898 91688  e2-e4 c7-c5 Ng1-f3 d7-d6 d2-d4 Ng8-f6 Nb1-c3 c5xd4 Nf3xd4 a7-a6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7 f2-f3 Bc8-e6
    71 37 986 99604  e2-e4 c7-c6 d2-d4 d7-d5 Nb1-c3 d5xe4 Nc3xe4 Bc8-f5 Ne4-g3 Bf5-g6 Ng1-f3 Nb8-d7 h2-h4 h7-h6 Bf1-d3 Bg6xd3 Qd1xd3
    
    # Move   Value=(V,P,V+P)   Policy  Visits                  PV
    #----------------------------------------------------------------------------------
    #  1   (0.535,0.527,0.531)  19.60   30023   d2-d4 Ng8-f6 c2-c4 e7-e6 Ng1-f3 b7-b6 g2-g3 Bc8-a6 b2-b3 Bf8-b4 Bc1-d2 Bb4-e7 Bf1-g2 c7-c6
    #  2   (0.553,0.527,0.540)  19.41   45717   e2-e4 c7-c6 d2-d4 d7-d5 Nb1-c3 d5xe4 Nc3xe4 Bc8-f5 Ne4-g3 Bf5-g6 Ng1-f3 Nb8-d7 h2-h4 h7-h6 Bf1-d3 Bg6xd3 Qd1xd3
    #  3   (0.540,0.527,0.534)   9.25    7136   c2-c4 e7-e5 Nb1-c3 Ng8-f6 Ng1-f3 Nb8-c6 g2-g3 Bf8-b4 Bf1-g2 Ke8-g8 Ke1-g1 d7-d6 Nc3-d5
    #  4   (0.538,0.527,0.532)   8.98   10574   Ng1-f3 Ng8-f6 c2-c4 e7-e6 d2-d4 d7-d5 Nb1-c3 Bf8-e7 g2-g3 Ke8-g8 Bf1-g2
    #  5   (0.508,0.527,0.508)   4.80    1119   g2-g3 d7-d5 Ng1-f3 c7-c5 Bf1-g2 Ng8-f6 Ke1-g1 Nb8-c6 d2-d4 c5xd4 Nf3xd4 e7-e5
    #  6   (0.529,0.527,0.528)   4.48    2165   e2-e3 d7-d5 c2-c4 e7-e6 Ng1-f3 Ng8-f6 d2-d4 Bf8-e7 Bf1-e2 Ke8-g8 Ke1-g1
    #  7   (0.481,0.527,0.481)   3.49     330   b2-b3 d7-d5 Bc1-b2 Ng8-f6 Ng1-f3 Bc8-f5 Nf3-h4 Bf5-d7 Nh4-f3
    #  8   (0.510,0.527,0.510)   3.32     547   c2-c3 d7-d5 d2-d4 Ng8-f6 Ng1-f3 e7-e6 Bc1-f4 Bf8-d6 e2-e3
    #  9   (0.498,0.527,0.498)   3.16     398   Nb1-c3 d7-d5 d2-d4 Ng8-f6 Bc1-f4 a7-a6 e2-e3 e7-e6 Ng1-f3 c7-c5 Bf1-e2
    # 10   (0.482,0.527,0.482)   3.15     298   d2-d3 d7-d5 Ng1-f3 Ng8-f6 g2-g3 c7-c5 Bf1-g2 Nb8-c6 Ke1-g1
    # 11   (0.450,0.527,0.450)   2.95     187   f2-f4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 c7-c5 b2-b3 g7-g6
    # 12   (0.462,0.527,0.462)   2.56     184   b2-b4 e7-e5 Bc1-b2 Bf8xb4 Bb2xe5 Ng8-f6 c2-c3 Bb4-e7 e2-e3 Ke8-g8
    # 13   (0.479,0.527,0.479)   2.37     229   h2-h3 e7-e5 e2-e3 d7-d5 d2-d4 Nb8-c6 Ng1-f3 e5-e4 Nf3-d2
    # 14   (0.495,0.527,0.495)   2.36     273   a2-a3 d7-d5 d2-d4 Ng8-f6 Ng1-f3 g7-g6 c2-c4 Bf8-g7
    # 15   (0.408,0.527,0.408)   2.03      85   Ng1-h3 d7-d5 g2-g3 e7-e5 d2-d4 e5xd4 Qd1xd4
    # 16   (0.450,0.527,0.450)   1.84     112   a2-a4 e7-e5 e2-e4 Ng8-f6 Nb1-c3 Bf8-b4 Ng1-f3 Ke8-g8
    # 17   (0.450,0.527,0.450)   1.67     100   h2-h4 d7-d5 d2-d4 c7-c5 d4xc5 Ng8-f6 Ng1-f3 e7-e6
    # 18   (0.347,0.527,0.347)   1.63      51   g2-g4 d7-d5 h2-h3 e7-e5 Bf1-g2 Nb8-c6 Nb1-c3
    # 19   (0.394,0.527,0.394)   1.59      59   f2-f3 e7-e5 Nb1-c3 Ng8-f6 d2-d4 e5xd4 Qd1xd4
    # 20   (0.424,0.527,0.424)   1.35      59   Nb1-a3 e7-e5 e2-e3 d7-d5 d2-d4 e5-e4 c2-c4
    
    # nodes = 883143 <0% qnodes> time = 9892ms nps = 89278 eps = 0 nneps = 10127
    # Tree: nodes = 135531 depth = 21 pps = 10095 visits = 99647 
    #       qsearch_calls = 0 search_calls = 0
    move e2e4
    Bye Bye 

It has successfully installed Scorpio and maded a test run. You can see the pps value for delay=0 is slightly faster than
for delay=1 so you don't need to change it in scorpio.ini

The directory structure looks like as follows where the Scorpio folder is taged with current date.
Then switch to the linux directory

    daniel@danidesti-desktop:~/engines$ ls
    install.sh  Scorpio-17-Aug-2019 lc0         Stockfish
    daniel@danidesti-desktop:~/engines$ cd Scorpio-17-Aug-2019/
    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019$ ls
    bin  nets-ccrl-cegt  nets-lczero  nets-maddex  nnprobe-ubuntu-gpu
    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019$ cd bin/Linux/
    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ ls
    calibrate.epd  log  scorpio  scorpio.bat  scorpio.ini  scorpio.sh

I have an RTX GPU so INT8 is going to be much faster. Most GPUs who don't have half precision (FP16) support also
have INT8 support so this is almost always the best way to go. Next we generate the data for calibration by doing

    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ ./scorpio.sh runinpnn calibrate.epd calibrate.dat quit
    feature done=0
    Number of cores 24 of 24
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 41932800 X 320 = 12796.9 MB
    processors [128]
    EgbbProbe 4.3 by Daniel Shawul
    egbb_cache 4084 X 8216 = 32.0 MB
    0 egbbs loaded !      
    nn_cache 131072 X 1552 = 194.0 MB
    Loading neural network : /home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    With "HALF" precision
    Loading graph on /gpu:0
    0. main_input 7168 = 112 8 8
    1. value_head 1 = 1 1 1
    2. policy_head 1858 = 1858 1 1
    Neural network loaded !	
    loading_time = 1s
    Bye Bye

We are ready to run with INT8 now so change float_type to INT8

    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ vi scorpio.ini
    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ grep ^float_type scorpio.ini
    float_type               INT8

Lets run it, first run may take upto 2 minutes

    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ ./scorpio.sh go quit
    feature done=0
    Number of cores 24 of 24
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 41932800 X 320 = 12796.9 MB
    processors [128]
    EgbbProbe 4.3 by Daniel Shawul
    egbb_cache 4084 X 8216 = 32.0 MB
    0 egbbs loaded !      
    nn_cache 131072 X 1552 = 194.0 MB
    Loading neural network : /home/daniel/engines/Scorpio-17-Aug-2019/nets-maddex/net-maddex.uff
    With "INT8" precision
    Loading graph on /gpu:0
    0. main_input 7168 = 112 8 80
    1. value_head 1 = 1 1 1
    2. policy_head 1858 = 1858 1 1
    Neural network loaded !	
    loading_time = 92s
    # rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    # [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 33 111 35327  e2-e4 c7-c5 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Nb1-c3 a7-a6 Bc1-e3 e7-e5 Nd4-b3 Bf8-e7
    64 34 222 73807  e2-e4 c7-c5 Ng1-f3 Nb8-c6 d2-d4 c5xd4 Nf3xd4 g7-g6 c2-c4 Bf8-g7 Bc1-e3 d7-d6 Nb1-c3 Ng8-f6 Bf1-e2 Ke8-g8 Ke1-g1
    65 33 334 117137  d2-d4 Ng8-f6 c2-c4 e7-e6 Nb1-c3 Bf8-b4 e2-e3 Ke8-g8 Ng1-f3 d7-d5 Bf1-d3 d5xc4 Bd3xc4 c7-c5 Ke1-g1
    66 35 445 159342  e2-e4 c7-c5 Ng1-f3 d7-d6 d2-d4 c5xd4 Nf3xd4 Ng8-f6 Nb1-c3 a7-a6 Bc1-g5 e7-e6 f2-f4 Qd8-b6 Nd4-b3 Qb6-e3 Bf1-e2 Nf6xe4
    67 26 558 202441  Ng1-f3 c7-c5 c2-c4 Nb8-c6 Nb1-c3 Ng8-f6 e2-e3 e7-e6 d2-d4 d7-d5 a2-a3 d5xc4 Bf1xc4
    68 34 669 246188  d2-d4 Ng8-f6 c2-c4 c7-c6 Ng1-f3 d7-d5 Nb1-c3 e7-e6 Bc1-g5 h7-h6 Bg5xf6 Qd8xf6 e2-e3 Nb8-d7
    69 34 780 285102  e2-e4 e7-e5 Ng1-f3 Nb8-c6 Bf1-b5 a7-a6 Bb5-a4 Ng8-f6 Ke1-g1 Bf8-e7 Rf1-e1 b7-b5 Ba4-b3 d7-d6 c2-c3 Ke8-g8 h2-h3 Bc8-b7 d2-d4 Rf8-e8
    70 32 891 330829  d2-d4 Ng8-f6 c2-c4 e7-e6 g2-g3 c7-c6 Bf1-g2 d7-d5 Ng1-f3 Bf8-e7 Ke1-g1 Ke8-g8 Qd1-c2 Nb8-d7 Rf1-d1
    71 34 1003 373880  d2-d4 Ng8-f6 c2-c4 e7-e6 Ng1-f3 d7-d5 Nb1-c3 Bf8-b4 c4xd5 e6xd5 Bc1-g5 Ke8-g8 e2-e3 h7-h6 Bg5-h4 Bc8-f5 Qd1-b3
    72 33 1017 378621  d2-d4 Ng8-f6 c2-c4 e7-e6 Ng1-f3 d7-d5 Nb1-c3 d5xc4 e2-e4 Bf8-b4 Bc1-g5 c7-c5 Bf1xc4 c5xd4 Nf3xd4 Qd8-a5 Bg5xf6 Bb4xc3 b2xc3 Qa5xc3 Ke1-f1
    
    # Move   Value=(V,P,V+P)   Policy  Visits                  PV
    #----------------------------------------------------------------------------------
    #  1   (0.533,0.486,0.509)  17.44  119095   e2-e4 e7-e5 Ng1-f3 Nb8-c6 d2-d4 e5xd4 Nf3xd4 Ng8-f6 Nd4xc6 b7xc6 Nb1-c3 Bf8-b4 Bf1-d3 d7-d5 e4xd5 c6xd5 Ke1-g1 Ke8-g8 Bc1-g5 c7-c6 Qd1-f3 Rf8-e8 Bg5xf6 Qd8xf6
    #  2   (0.547,0.486,0.517)  15.28  164574   d2-d4 Ng8-f6 c2-c4 e7-e6 Ng1-f3 d7-d5 Nb1-c3 d5xc4 e2-e4 Bf8-b4 Bc1-g5 c7-c5 Bf1xc4 c5xd4 Nf3xd4 Qd8-a5 Bg5xf6 Bb4xc3 b2xc3 Qa5xc3 Ke1-f1
    #  3   (0.532,0.486,0.509)   9.50   44812   Ng1-f3 d7-d5 d2-d4 Ng8-f6 c2-c4 e7-e6 Nb1-c3 d5xc4 e2-e3 a7-a6 Bf1xc4 b7-b5 Bc4-d3 Bc8-b7 Ke1-g1 c7-c5
    #  4   (0.529,0.486,0.507)   9.24   23413   c2-c4 e7-e6 Ng1-f3 Ng8-f6 g2-g3 d7-d5 Bf1-g2 Bf8-e7 d2-d4 Ke8-g8 Ke1-g1 c7-c6
    #  5   (0.526,0.486,0.506)   5.54   10041   e2-e3 Ng8-f6 Ng1-f3 c7-c5 d2-d4 d7-d5 c2-c4 e7-e6 c4xd5 e6xd5 Bf1-b5
    #  6   (0.523,0.486,0.504)   5.07    5487   g2-g3 d7-d5 d2-d4 Bc8-f5 Bf1-g2 e7-e6 Ng1-f3 Ng8-f6 Ke1-g1 Bf8-e7 c2-c4
    #  7   (0.516,0.486,0.501)   4.38    2942   c2-c3 Ng8-f6 d2-d4 d7-d5 Ng1-f3 e7-e6 Bc1-f4 Bf8-d6 e2-e3 Bd6xf4 e3xf4
    #  8   (0.482,0.486,0.482)   3.71    1129   b2-b3 Ng8-f6 Bc1-b2 d7-d5 Ng1-f3 Bc8-f5 Nf3-h4 Bf5-d7 e2-e3
    #  9   (0.469,0.486,0.469)   3.65     907   d2-d3 d7-d5 Ng1-f3 Ng8-f6 g2-g3 g7-g6 Bf1-g2 Bf8-g7 Ke1-g1 Ke8-g8 Nb1-d2 c7-c5
    # 10   (0.495,0.486,0.490)   3.18    1260   Nb1-c3 d7-d5 d2-d4 Ng8-f6 Bc1-f4 a7-a6 e2-e3 e7-e6 Ng1-f3 c7-c5 Bf1-e2 Nb8-c6 Ke1-g1
    # 11   (0.465,0.486,0.465)   3.07     725   f2-f4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 c7-c5 b2-b3 g7-g6 Bf1-b5
    # 12   (0.467,0.486,0.467)   2.70     638   b2-b4 e7-e5 Bc1-b2 Bf8xb4 Bb2xe5 Ng8-f6 c2-c3 Bb4-e7 d2-d4 Ke8-g8 e2-e3
    # 13   (0.469,0.486,0.469)   2.70     654   h2-h3 e7-e5 e2-e3 d7-d5 d2-d4 Nb8-c6 Ng1-f3 e5-e4 Nf3-d2 f7-f5 c2-c4 Ng8-f6 Nb1-c3
    # 14   (0.504,0.486,0.495)   2.41    1152   a2-a3 e7-e5 c2-c4 Ng8-f6 Nb1-c3 d7-d5 c4xd5 Nf6xd5 Ng1-f3 Nb8-c6 Qd1-c2 Bc8-e6
    # 15   (0.414,0.486,0.414)   2.27     312   Ng1-h3 d7-d5 g2-g3 e7-e5 d2-d4 e5xd4 Qd1xd4 Nb8-c6 Qd4-a4
    # 16   (0.433,0.486,0.433)   2.11     402   h2-h4 d7-d5 d2-d4 c7-c5 d4xc5 Ng8-f6 Ng1-f3 e7-e6 Bc1-e3
    # 17   (0.464,0.486,0.464)   2.04     455   a2-a4 e7-e5 e2-e4 Ng8-f6 Nb1-c3 Bf8-b4 Ng1-f3 Ke8-g8 Nf3xe5 Rf8-e8
    # 18   (0.363,0.486,0.363)   2.04     195   g2-g4 d7-d5 h2-h3 e7-e5 Bf1-g2 Nb8-c6 Nb1-c3 Ng8-e7 d2-d3
    # 19   (0.419,0.486,0.419)   1.93     263   f2-f3 e7-e5 Nb1-c3 Ng8-f6 d2-d4 e5xd4 Qd1xd4 Nb8-c6 Qd4-f2 d7-d5
    # 20   (0.424,0.486,0.424)   1.73     245   Nb1-a3 e7-e5 e2-e3 d7-d5 d2-d4 Nb8-c6 Ng1-f3 e5-e4 Nf3-d2
    
    # nodes = 3952327 <0% qnodes> time = 10190ms nps = 387863 eps = 0 nneps = 37212
    # Tree: nodes = 515131 depth = 23 pps = 37182 visits = 378702 
    #       qsearch_calls = 0 search_calls = 0
    move d2d4
    Bye Bye

You can see we got a speedup of close to 4 times. This is rather good but you should expect on average a 2.5x speedup only.
My half precision runs are rather slow for some reason on my machine and it can sometimes double in nps for reasons I don't
understand yet.

The maddex directory has now two optimized plan files (*.trt) that are used from now on. The 128 is the number of threads
and 0/1/2 represent FP32/FP16/INT8 precision. If you change the number of threads a new plan file will be generated

    daniel@danidesti-desktop:~/engines/Scorpio-17-Aug-2019/bin/Linux$ ls -l ../../nets-maddex/ --block-size=M
    total 222M
    -rw-r--r-- 1 daniel daniel 107M Jul 19 20:51 net-maddex.uff
    -rw-r--r-- 1 daniel daniel  54M Aug 17 09:52 net-maddex.uff.128_1.trt
    -rw-r--r-- 1 daniel daniel  62M Aug 17 09:56 net-maddex.uff.128_2.trt

