# Installation instructions

The recommended way to install ScorpioNN is using install scripts, a batch file (`install.bat`) for Windows 
and a shell script (`install.sh`) for Ubuntu Linux. You need to only download these scripts and nothing else.
Then figure out beforehand if your GPU supports HALF precision e.g. RTX, V100 support this. GTX 1070 ti doesn't.
If your GPU supports HALF precision, proceed normally as follows

## HALF precision

TCEC uses this to install it on their Linux machine with 4xV100
     
      wget https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh
      chmod +x install.sh
      ./install.sh

The installer takes care of everything for you including multi-GPU mode and other configurations.

## INT8 precision
If your GPU does not support HALF precision, you most likely can use INT8 (most GPUs support this)

      $ ./install.sh --help
      Usage: ./install.sh  

        -p     Precision to use FLOAT/HALF/INT8.
        -t     Threads per GPU/CPU cores.

      Example: ./install.sh -p INT8

So to install with INT8 do this `./install.sh -p INT8`.

## Windows nuances
For windows machine, you  use [install.bat](https://github.com/dshawul/Scorpio/releases/download/3.0/install.bat).
Other than that the steps are very similar to that for linux.
Paths with **spaces** are known to cause problems so please make sure the path you install scorpio to does not have spaces.

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

