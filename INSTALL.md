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
        --only-trt         Install only TensorRT and rely on system cuda and cudnn.
                           72 needs cuda-11 and cudnn-8
                           60 needs cuda-10 and cudnn-7
      
      Example: ./install.sh -p INT8 -t 80

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

For newer GPUs such as 3080s and 3090s, you would need to first install cuda 11.1 and cudnn 8.
The later is too big to ship with Scorpio, so your system must have it.
Then you can install scorpio as

      $ ./install.sh --only-trt 72

This will install Scorpio with TensorRT 7.2 and use the cuda-11 and cudnn-8 on your system.

## Windows nuances
For windows machine, you  use [install.bat](https://github.com/dshawul/Scorpio/releases/download/3.0/install.bat).
Other than that the steps are very similar to that for linux.

Here are some possible issues on windows:

  * Paths with **spaces** are known to cause problems so please make sure the path you install scorpio to does not have spaces.

  * The install script uses `powershell` (available on Windows 10 by default) for extracting zip files. 
    However, Windows 7 and older make sure you can call powershell from the command line before installation.

  * For downloading files `bitsadmin` is used and is usually the lowest level app available on most systems.
    If not (very unlikely) you got to find a way to install it.

## EGBB files
By default, the installer will download and install 5-men egbbs. If you want to use 6-men, you have to download them
from this [torrent site](http://oics.olympuschess.com/tracker/index.php). They are only 45GB in size, a fraction of size of syzygy tablebases.
To make scorpio use them, edit the `egbb_files_path` (NOT `egbb_path`) in scorpio.ini
      
      egbb_files_path          /path/to/6-men/egbb/

## Installing in a GUI
Then, to install it in a GUI (xboard/Arena etc), point the engine to `scorpio.sh` or `scorpio.bat`
not the scropio executable. The scripts set paths to required cuda/cudnn libraries which scorpio.exe
can't do by itself.

For Fritz and other interfaces that do not accept scripts as engines, you will have to install `scorpio.exe`
and manually append the full path to "nnprobe-windows-gpu" in the "Path" environment variable.

That is it. So relax and try these instructions and it will probably work without modification.
Do not change settings in scorpio.ini unless you know what you are doing.

**Tips**: The `mt` parameter in scorpio.ini is not for the number of cores but batchsize so don't change that.
You can increase the hashtable size by modifying the `ht` parameter.

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

## NNUE installation
If you want to use Scorpio as a NNUE engine, you have to edit scorpio.ini manually after installing it
as a NN engines as described above. Scorpio comes with the dh-0.2.bin, as Stockfish style net, by default.
So to use NNUE, we need to turn off NN usage and turn on NNUE usage

      montecarlo 0      # Turn of montecarlo for NNUE
      mt         4      # If you have a 4 threads machine.
      use_nn     0      # Turn off NN
      use_nnue   1      # Turn on NNUE
      nnue_type  0      # This is default
      nnue_scale 256    # dh-0.2 bin needs larger scaling than other SF style nets that need 128
