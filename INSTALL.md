# Installation instructions

The recommended way to install ScorpioNN is using install scripts, a batch file [install.bat](https://github.com/dshawul/Scorpio/releases/download/3.0/install.bat) for Windows 
and a shell script [install.sh](https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh) for Ubuntu Linux. You need to download only these scripts and nothing else.

      $ ./install.sh --help
      Usage: ./install.sh  
      
        -h,--help          Display this help message.
        -p,--precision     Precision to use FLOAT/HALF/INT8.
        -t,--threads       Total number of threads, i.e minibatch size.
        -f,--factor        Factor for auto minibatch size determination from SMs, default 2.
        --cpu              Force installation on the CPU even if machine has GPU.
        --no-egbb          Do not install 5-men egbb.
        --no-lcnets        Do not install lczero nets.
        --no-scnets        Do not download scorpio nets.
        --trt              72 is for latest GPUs.
                           60 is for older GPUs (default).
      
      Example: ./install.sh -p INT8 -t 80

## NN installation

GPU version works only on NVIDIA GPUs, so you are out of luck if you have AMD GPUs.
For older GPUs (older than 3080s), executing the installer without any arguments, as shown below
   
      $ ./install.sh

will automatically determine how many GPUs you have, which precision to use, how many threads per GPU (minibatch size) to use etc.
If you want to force the precision, and batch size pass those arguments.

The version of Linux supported by the install script is Ubuntu 18.04, other versions probably won't work due to
glibc issues. To run on other versions of Linux and also Mac, you can build docker containers of scorpio using Dockerfiles
provided [here](https://github.com/dshawul/Scorpio/tree/master/install). Installation on Windows may pose some difficulties
described below.

NN and NNUE versions of Scorpio are compiled with AVX2 support so if your machine does not support that, it will not work.
AVX2 has been around since 2012 so your CPU most likely supports it unless it is older than that.

For newer GPUs such as 3080s and 3090s, you can install scorpio as

      $ ./install.sh --trt 72

This will install Scorpio with TensorRT 7.2 and use the cuda-11 and cudnn-8 on your system.

## NNUE installation

If you want to install Scorpio purely as a NNUE engine i.e. not using any NN do this instead

      $ ./install.sh --cpu --no-scnets --no-lcnets --no-egbb

Here I am forcing installation for CPU even if you have GPU, not downloading any Scorpio or LC0 nets
and also no egbb files if you already have them. If you execute the exact commands above, it should give 
you a pure Scorpio NNUE installion with no need to edit scorpio.ini, except to set the number of threads.
By default the installation uses all your cores, if you want it to use just 1 core set

      mt          1     # Set the number of cores Scorpio uses for NNUE here

The `--no-scnets` is required for pure NNUE installation. If you did the regular NN installaion by removing that,
you have to edit scorpio.ini manually to make sure `montecarlo` and `use_nn` are turned off.

      montecarlo  0     # Turn off montecarlo 
      use_nn      0     # Turn off NN

You can configure NNUE further in scorpio.ini. Scorpio comes with its own NNUE net (net-scorpio-k16.bin),
DarkHorse net from dkappe (dh-0.2.bin), and the latest Stockfish net.

      nnue_type  1      # Set this to 0 for Stockfish style nets including dh-0.2
      nnue_scale 256    # Use a scale of 256 for scorpio's own NNUE net and also dh-0.2, 
                          Use 128 for Stockfish nets

## Windows nuances
For windows machine, you  use [install.bat](https://github.com/dshawul/Scorpio/releases/download/3.0/install.bat), 
other than that the steps are very similar to that for linux.

Here are some possible issues on windows:

  * Paths with **spaces** are known to cause problems so please make sure the path you install scorpio to does not have spaces.

  * The install script uses `powershell` (available on Windows 10 by default) for extracting zip files. 
    However, Windows 7 and older make sure you can call powershell from the command line before installation.

  * For downloading files `bitsadmin` is used and is usually the lowest level app available on most systems.
    If not (very unlikely) you 've got to find a way to install it.

  * If your AntiVirus software interrupts installation, you may need to disable it just for the installation.

## EGBB files
By default, the installer will download and install 5-men egbbs. If you want to use 6-men, you have to download them
from this [torrent site](http://oics.olympuschess.com/tracker/index.php). They are only 45GB in size, a fraction of size of syzygy tablebases.
To make scorpio use them, edit the `egbb_files_path` (NOT `egbb_path`) in scorpio.ini
      
      egbb_files_path          /path/to/6-men/egbb/

## Installing in a GUI
To install it in a GUI (e.g. xboard/Arena etc), point to the path of `scorpio.exe`.
For advanced setup you can modify and install the scripts `scorpio.sh` / `scorpio.bat` instead.
The scripts were a must previously, but now you should be able to use the executable directly.

**Tips**: The `mt` parameter in scorpio.ini is not for the number of cores with NN installation,
but for the batchsize so don't change it in that case.

If you can't get this to work, send me an email at dshawul at yahoo.com, and I will do my best to assist.

## Advanced user -- Optimizing minibatch size and delay parameters

Usually the best batch size is an integer multiple of the number of multiprocessors (SMs) of the GPU.
You can use device.exe, located in the `nnprobe-OS-gpu` directory, to determine the number of SMs of your GPU

      $ ./device -h
      Usage: device [option]
         -h,--help    Display this help message
         -n,--number  Number of GPUs
         --mp         Total number of multiprocessors
         --mp-each    Number of multiprocessors for each GPU
         --name       Name of GPU 0
         --name-each  Name of each GPU
         --int8       Does all GPUs support fast INT8?
         --int8-each  INT8 support for each GPU
         --fp16       Does all GPUs support fast FP16?
         --fp16-each  FP16 support for each GPU

If the number of SMs is 40, try 40, 80, 120, 160, 200 etc. Usually 1x or 2x is enough.
Also, the smaller the net or the less powerful the GPU, the more multiples you want to try.
If you get a small increase in pps going from 80 to 120, the smaller batch size i.e. 80 since it results 
in less overhead and a more selective tree.

There is a parameter `delay` that can be set to 0 or 1. The installer tries its best to choose
based on your CPU and GPU. If you have powerful CPU delay=0 with fewer threads may give better performance
than delay=1 with more threads. If your CPU can't handle the many threads that Scorpio launches, you are
better of with delay=1

