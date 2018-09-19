### Table of Contents
1. [Scorpio](#scorpio)
2. [Goals](#goals)
3. [Parallel](#parallel)
4. [MCTS](#mcts)
5. [Neural Networks](#nns)
6. [EGBB](#egbb)
7. [Acknowledgement](#ackn)
8. [Book](#book)

<a name="scorpio"></a>
### Scorpio

Scorpio is a strong chess engine at the grand master level. It can be used 
with GUIs that support the Winboard protocol natively such as Winboard and
Arena, or via the Wb2Uci adapter in Fritz and Shredder interfaces.

<a name="goals"></a>
### Goals

The main goals of Scorpio are:

  * [Parallel](#parallel) : Experiments with parallel search algorithms such as YBW and ABDADA 
    algorithms both on shared memory (SMP) and non-uniform memory 
    access (NUMA) systems. Also distributed parallel search on a loosely
    coupled cluster of computers is done using YBW.

  * [MCTS](#mcts) : Experiments with montecarlo tree search (MCTS) for chess. It supports alpha-beta
    rollouts with LMR and null move in the tree part of the search, coupled with
    standard MCTS rollouts or standard alpha-beta search.

  * [Neural Networks](#nns) : Experiments with neural network evaluation ala AlaphaZero.

  * [EGBB](#egbb) : Experiments with encoding, compression and use of endgame bitbases (EGBBS) of upto 6 pieces
      
<a name="parallel"></a>
### Parallel

Cluster version of scorpio can be compiled with MPI libraries OpenMPI
or MPICH. Work is distributed using YBW algorithm. To run Scorpio on
cluster start one processor per node using "mpirun -pernode" and set
"mt" option to "auto" in scorpio.ini so that it detects number of pro-
cessors automatically. You can also start processes only but it will
run slower. Experiment with split depth parameters for optimal perfor-
mance.
   * CLUSTER\_SPLIT\_DEPTH (default 8)
   * SMP\_SPLIT\_DEPTH (default 4)    

<a name="mcts"></a>
### MCTS

The montecarlo tree search engine uses alpha-beta rollouts according to 
[Huang paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2014/11/huang_rollout.pdf).
This is much stronger than standard MCTS in games like chess which ar full of tactics. ScorpioMCTS
storing all the tree in memory has become very close in strength to the standard alpha-beta
searcher due to alpha-beta rollouts. It can actually become same strength as the standard if we
limit the amount of tree stored in memory via "treeht" parameter in the scorpio.in. When
the MCTS search runs out memory, it will spawn standard recursive alpha-beta search at the leaves
so setting treeht = 0 stores only root node and its children, effectively becoming same strength
as the standard alpha-beta method. If we set treeht = 128 MB, upper parts of the tree will be stored
in memory and MCTS used there. Note that 128MB of memory are not allocated immediately at start up;
it only specifies the maximum memory to use for storing tree. Don't forget to set montecarlo=1
if you want to experiment with MCTS.

   * montecarlo 1
   * treeht     128

The parallel search for MCTS uses virtual loss to distribute work among threads in standard MCTS rollouts,
and ABDADA like BUSY flag for alpha-beta rollouts. ABDADA and parallel MCTS from the Go world are very similar
in nature.

<a name="nns"></a>
### Neural Networks (NNs)

The neural network version of Scorpio works with a tensorflow backend. The easiest way is to download the binaries
I provide for windwos for both CPU and GPU (with CUDA + cuDNN libraries)

TLDR; Go to my github release page and download pre-compiled binaries of egbbdll64.dll for windows (CPU and GPU). 
Here are the links for convenience

[egbbdll64-nn-windows-cpu.zip](https://github.com/dshawul/Scorpio/releases/download/2.8.8/egbbdll64-nn-windows-cpu.zip)

[egbbdll64-nn-windows-gpu.zip](https://github.com/dshawul/Scorpio/releases/download/2.8.8/egbbdll64-nn-windows-gpu.zip)


The egbbdll serves dual purpose, namely, for probing bitbases and neural networks so extract it in a directory where bitbases are located. 
For the GPU version of egbbdll, we need to set the Path environment variable and incase of linux LD_LIBRARY_PATH for the system to find
cudnn.dll and other dependencies.

#### Building from sources (for linux)

For those who feel adventurous, here is how you build from sources.
First build C++ tensorflow library following the steps given [here](https://github.com/FloopCZ/tensorflow_cc), 
then compile egbbdll using this library for neural network inference.

#####   Building tensorflow_cc on linux

To build and install the "shared" tensorflow_cc library, follow the instructions given [here](https://github.com/FloopCZ/tensorflow_cc)

It may be easier to use docker using the command below instead of building the shared library with bazel yourself.

    docker run --runtime=nvidia -it floopcz/tensorflow_cc:ubuntu-shared-cuda /bin/bash

Once the build and install (or docker load) is complete, you should see the tensorflow and protobuf libraries

    $ ls /usr/local/lib/tensorflow_cc/
    libprotobuf.a  libtensorflow_cc.so

##### Building egbbdll and Scorpio

This is very straightforward:

    git clone https://github.com/dshawul/egbbdll.git
    cd egbbdll && make && cd ..
    
    git clone https://github.com/dshawul/Scorpio.git
    cd Scorpio && make && cd ..

This will get you an egbbso64.so and linux scorpio executable.
Then, copy the egbbso64.so to the directory you store your egbb files, overwriting the existing one.

    cp egbbdll/egbbso64.so  $EGBB_DIRECTORY

####   Building from sources (for Windows)

Unfortunately tensorflow_cc does not support windows. However, since r1.11, tensorflow can be built on windows using bazel. 
To compile egbbdll directly using bazel, put the egbbdll source directory in tensorflow/tenorflow/cc/ and then compile with

    bazel build --config=opt --config=monolithic //tensorflow/cc/egbbdll:egbbdll.so

You can use this approach for linux as well by setting USE_TF = 2 in Makefile and specifying the tensorflow directly.
Setting USE_TF = 1 means using the tensorflow_cc approach.

#### Downloading networks

Create a directory to store network files and downlowd networks from my site

    mkdir Scorpio/nets-ccrl
    cd Scorpio/nets-ccrl
    wget https://sites.google.com/site/dshawul/net-2x32.pb
    wget https://sites.google.com/site/dshawul/net-6x64.pb
    wget https://sites.google.com/site/dshawul/net-12x64.pb
    cd ..

I have also 24x128 and 40x256 networks

#### Testing on the CPU

    $ ./scorpio use_nn 1 mt 2 montecarlo 1 frac_alphabeta 0 backup_type 6 book off go quit
    feature done=0
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 335539200 X 40 = 12799.8 MB
    processors [1]
    processors [2]
    EgbbProbe 4.1 by Daniel Shawul
    180 egbbs loaded !      
    Loading neural network ...
    Neural network loaded ! 
    loading_time = 0s
    [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 14 111 7849  d2-d4 d7-d5 Nb1-c3
    64 22 222 17438  e2-e4 Ng8-f6 d2-d3 d7-d5 Nb1-c3
    65 24 334 28334  d2-d4 d7-d5 c2-c4 e7-e6
    66 24 445 43483  d2-d4 e7-e5 e2-e3 d7-d6 Ng1-f3
    67 23 556 61266  d2-d4 e7-e5 e2-e3 d7-d6 d4xe5 d6xe5 Qd1xd8 Ke8xd8
    68 21 668 84004  e2-e4 Ng8-f6 f2-f3 e7-e5 d2-d3 Nb8-c6 Nb1-c3
    69 20 779 112214  e2-e4 Ng8-f6 d2-d3 d7-d6 Nb1-c3 e7-e5
    70 19 890 151678  e2-e4 e7-e5 d2-d3 d7-d6 Nb1-c3 Ng8-f6 Bc1-e3 Nb8-c6
    71 18 1002 195211  e2-e4 e7-e5 d2-d4 e5xd4 Qd1xd4 Nb8-c6 Qd4-d1 d7-d6 Ng1-f3 Bc8-e6 Bc1-e3 Ng8-f6 Nb1-c3 a7-a6 h2-h3 h7-h6 Bf1-d3 g7-g6 a2-a3 Bf8-g7 Ke1-g1

    #  1     10    1151 e2-e4 e7-e5 d2-d4 e5xd4 Qd1xd4 Nb8-c6 Qd4-d1 d7-d6 Ng1-f3 Bc8-e6 Bc1-e3 Ng8-f6 Nb1-c3 a7-a6 h2-h3 h7-h6 Bf1-d3 g7-g6 a2-a3 Bf8-g7 Ke1-g1 Ke8-g8 Qd1-e2 Qd8-e7 Rf1-d1 Nf6-d7 Bd3-c4 f7-f5 Nc3-d5 Qe7-f7 e4xf5 Qf7xf5 Be3xh6 Be6xd5 Bc4xd5 Kg8-h7 Bh6xg7 Kh7xg7
    #  2      9     710 d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 e7-e6 Bf1-d3 Bf8-d6 Nb1-d2 Nb8-c6 Ke1-g1 Ke8-g8
    #  3      7     224 Nb1-c3 e7-e5 e2-e4 d7-d6 f2-f3 Nb8-c6 d2-d3 Bc8-e6 Bc1-e3
    #  4      8     516 Ng1-f3 d7-d5 d2-d4 Ng8-f6 g2-g3 Bc8-d7 Nb1-c3 Nb8-c6 Bf1-g2 e7-e6 Ke1-g1 a7-a6 Bc1-e3 h7-h6 a2-a3
    #  5      6     149 e2-e3 e7-e5 Ng1-f3 d7-d6 Nb1-c3 Ng8-f6 Bf1-b5
    #  6      8     267 d2-d3 d7-d5 c2-c4 e7-e6 Ng1-f3 Nb8-c6 c4xd5 e6xd5 e2-e4 Ng8-f6
    #  7      4     101 g2-g3 e7-e5 e2-e4 d7-d6 d2-d3 Nb8-c6
    #  8      2      76 b2-b3 d7-d5 d2-d4 Ng8-f6
    #  9      5     129 f2-f3 e7-e5 e2-e4 Bf8-c5 d2-d3 Nb8-c6 Nb1-c3
    # 10      2      83 h2-h3 d7-d5 d2-d4 e7-e6 Ng1-f3 Ng8-f6
    # 11      3      87 c2-c3 d7-d5 Ng1-f3 e7-e6 d2-d4 Ng8-f6
    # 12      0      58 a2-a3 d7-d5 d2-d3 e7-e6 e2-e4
    # 13      0      59 Nb1-a3 d7-d5 d2-d4 e7-e6 Ng1-f3
    # 14      2      75 Ng1-h3 e7-e5 e2-e4 d7-d6 Nb1-c3 Nb8-c6
    # 15      4     107 f2-f4 Ng8-f6 Ng1-f3 d7-d5 d2-d3 Nb8-c6 Nb1-c3
    # 16      8     326 c2-c4 e7-e5 d2-d3 d7-d6 Ng1-f3 Ng8-f6 e2-e4 h7-h6 h2-h3 Bc8-e6 Nb1-c3 Nb8-c6 Bc1-e3 a7-a6 a2-a3 g7-g6 Bf1-e2
    # 17     -8      35 h2-h4 e7-e5 Nb1-c3 Nb8-c6
    # 18     -4      44 a2-a4 d7-d5 d2-d3 Ng8-f6
    # 19    -13      27 g2-g4 d7-d5 d2-d4 Ng8-f6
    # 20      1      71 b2-b4 e7-e5 Bc1-b2 Bf8xb4 Bb2xe5

    nodes = 513255 <93% qnodes> time = 11120ms nps = 46156 eps = 37600 nneps = 329
    Tree: nodes = 123105 depth = 38 pps = 384 visits = 4276 
          qsearch_calls = 62618 search_calls = 0
    move e2e4
    Bye Bye

#### Testing on the GPU

On the GPU we use multi-threaded batching so launch as many threads as you can. Here is a run of the same problem as the CPU
but with 128 threads and a single P100 GPU. You can see that it hits 10000n/s compared to the 384n/s we get on the CPU

    $ ./scorpio use_nn 1 mt 128 montecarlo 1 frac_alphabeta 0 backup_type 6 go quit
    feature done=0
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 335539200 X 40 = 12799.8 MB
    processors [1]
    processors [128]
    EgbbProbe 4.1 by Daniel Shawul
    0 egbbs loaded !      
    Loading neural network ...
    Neural network loaded ! 
    loading_time = 12s
    [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 35 161 134  e2-e4 e7-e5 Ng1-f3 d7-d6
    64 16 272 7147  d2-d4 d7-d5 e2-e4 d5xe4 Nb1-c3 Nb8-c6
    65 16 383 17114  d2-d4 d7-d5 Ng1-f3 Ng8-f6 h2-h3 e7-e6 e2-e3
    66 16 495 26337  d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 Nb8-c6 Nb1-c3
    67 17 607 38323  d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 e7-e6 Bf1-b5 Nb8-c6 Ke1-g1
    68 17 719 52523  d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 g7-g6 c2-c4 d5xc4 Bf1xc4
    69 17 831 62733  d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 Bc8-e6 Nb1-d2 Nb8-c6 Bf1-d3 h7-h6 Ke1-g1
    70 17 943 78135  d2-d4 d7-d5 Ng1-f3 Ng8-f6 b2-b4 Nf6-e4 Bc1-b2 Bc8-e6 b4-b5 Nb8-d7 e2-e3 h7-h6 Nb1-c3
    71 17 1057 103815  d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 g7-g6 c2-c4 Bf8-g7 Nb1-c3 d5xc4 Bf1xc4 Ke8-g8 Ke1-g1 Nb8-c6 Bc4-b3 e7-e6 a2-a3 Bc8-d7 Bc1-d2 Nc6-e7 e3-e4 Bd7-c6 Qd1-e2 h7-h6 Ra1-c1 Ra8-c8 Bd2-f4 Rf8-e8 h2-h3 b7-b6 Bf4-g3 a7-a5 Bg3-e5 a5-a4 Be5xf6 a4xb3 Bf6-e5

    #  1     20   13688 e2-e4 e7-e5 Ng1-f3 Ng8-f6 d2-d3 d7-d6 b2-b3 Nb8-d7 Bc1-b2 Nd7-c5 Nb1-d2 h7-h6 Nd2-c4 Bc8-d7 h2-h3 Bf8-e7 Bf1-e2 Ke8-g8 Ke1-g1 Bd7-c6 a2-a4 a7-a6 Rf1-e1 Ra8-c8 a4-a5 Nc5-e6 Nc4-e3 Ne6-f4 Qd1-d2 Rf8-e8 Ne3-f5 Nf4xe2 Qd2xe2 Nf6-d7 c2-c4 Be7-f6 Qe2-d2 Nd7-c5 b3-b4 Nc5-e6 Qd2-c2 Qd8-d7 Qc2-d2 Ne6-f4 d3-d4 Rc8-d8 d4xe5 Bf6xe5 Bb2xe5 d6xe5 Qd2-c2 Nf4-d3 Re1-e2 Bc6xe4
    #  2     22   73537 d2-d4 d7-d5 Ng1-f3 Ng8-f6 e2-e3 g7-g6 c2-c4 Bf8-g7 Nb1-c3 c7-c6 h2-h3 Ke8-g8 c4xd5 c6xd5 Bf1-d3 Nb8-c6 Ke1-g1 Bc8-e6 Bc1-d2 Ra8-c8 a2-a3 a7-a6 Ra1-c1
    #  3      0    2646 Nb1-c3 d7-d5 d2-d4 Ng8-f6 f2-f3 c7-c5 e2-e3 c5xd4 Bf1-b5 Bc8-d7 Bb5xd7 Qd8xd7 Qd1xd4 e7-e6 Qd4-f4 Nb8-a6 Ke1-f2 Ra8-c8 e3-e4 Bf8-c5 Bc1-e3 Bc5xe3 Qf4xe3 d5-d4 Qe3-g5 d4xc3 Qg5xg7 Ke8-e7 b2-b3 Qd7-d2 Ng1-e2 Rh8-d8
    #  4     15    6649 Ng1-f3 d7-d5 d2-d4 Ng8-f6 e2-e3 e7-e6 Bf1-d3 Bf8-d6 Nb1-d2 Nb8-d7 h2-h3 Ke8-g8 Ke1-g1 Nd7-b6 c2-c3 h7-h6 Nd2-b3 c7-c6 Nb3-c5 Nb6-c4 Rf1-e1 Rf8-e8 e3-e4 d5xe4 Bd3xc4 e4xf3 g2xf3 Nf6-d5 a2-a3 Qd8-h4 Bc4xd5 c6xd5 Kg1-g2 e6-e5 Kg2-g1
    #  5     -9    1767 e2-e3 e7-e5 d2-d4 d7-d6 Ng1-f3 Nb8-c6 Bf1-d3 Ng8-f6 Nb1-c3 Bc8-e6 h2-h3 a7-a6 a2-a3 h7-h6 Bc1-d2 Bf8-e7 Ke1-g1 Ke8-g8 Qd1-e2 Qd8-d7 b2-b3 Rf8-e8 Rf1-e1 Re8-f8 Re1-f1
    #  6      5    3455 d2-d3 f7-f6 c2-c4 e7-e5 Nb1-c3 Ng8-e7 Ng1-f3 d7-d6 e2-e3 Bc8-d7 e3-e4 Ke8-f7 Bc1-e3 Nb8-c6 a2-a3 Ne7-g6 Nc3-d5 a7-a5 Bf1-e2 b7-b6 h2-h3 Bf8-e7 d3-d4 Rh8-e8 d4xe5 Nc6xe5 b2-b3 Kf7-g8 Ke1-g1 Bd7-e6 Nf3-d4 Be6xd5 e4xd5 Qd8-d7 Rf1-e1 a5-a4 b3xa4 Ra8xa4 Nd4-f5 Ra4-a8 Be2-g4 Kg8-h8
    #  7    -12    1283 g2-g3 e7-e5 e2-e4 d7-d6 d2-d3 Ng8-f6 Nb1-c3 Bc8-e6 Bc1-d2 Nb8-c6 Bf1-g2 h7-h6 a2-a3 Bf8-e7 Nc3-d5 Be6xd5 e4xd5 Nc6-d4 h2-h4 a7-a6 Ng1-f3 Nd4xf3 Bg2xf3 Ke8-g8 Ke1-g1 Qd8-d7 Rf1-e1
    #  8    -17    1110 b2-b3 e7-e5 e2-e4 d7-d5 Bc1-b2 d5xe4 Bb2xe5 Nb8-c6 Be5-b2 f7-f5 Bf1-c4 Ng8-f6 Ng1-e2 Bf8-c5 Ke1-g1 Rh8-f8 d2-d3 h7-h6 Nb1-d2 Bc5-b4 a2-a3 Bb4-a5 b3-b4 Ba5-b6 Bb2xf6 g7xf6 d3xe4 Nc6-e5 Ne2-f4 Ne5xc4 Qd1-h5
    #  9     -4    1487 f2-f3 e7-e5 e2-e4 d7-d5 d2-d4 e5xd4 Qd1xd4 Bc8-e6 e4xd5 Qd8xd5 Bc1-e3 Nb8-c6 Qd4-d3 Ke8-c8 Nb1-c3 Qd5-a5 Qd3-b5 Qa5-b4
    # 10    -16    1094 h2-h3 d7-d5 d2-d4 Ng8-f6 e2-e3 h7-h6 Nb1-c3 Nb8-c6 Ng1-f3 e7-e6 Bf1-d3 a7-a6 Ke1-g1 Bf8-d6 Bc1-d2 Ke8-g8
    # 11    -15    1130 c2-c3 d7-d5 Ng1-f3 b7-b6 d2-d4 Ng8-f6 Bc1-e3 e7-e6 h2-h3 Bc8-b7 Nb1-d2 Bf8-d6 Qd1-a4 c7-c6 Ke1-c1 h7-h6
    # 12    -21     842 a2-a3 e7-e5 e2-e4 d7-d6 d2-d3 Nb8-d7 Ng1-f3 Ng8-f6 h2-h3 Nd7-c5 Nb1-d2 Bc8-d7 Bf1-e2 Bf8-e7 Nd2-c4 Ke8-g8 Ke1-g1 Bd7-c6 Bc1-d2 h7-h6 Bd2-c3 b7-b6 Nc4-e3
    # 13    -21     917 Nb1-a3 d7-d5 d2-d4 f7-f6 Ng1-f3 Nb8-c6 c2-c4 e7-e5 d4xe5 Bf8-b4 Bc1-d2 Bb4xa3 b2xa3 Nc6xe5 c4xd5 Qd8xd5 Bd2-b4 Bc8-e6 Nf3-d4 Ke8-f7 e2-e3 c7-c5 Qd1-h5 g7-g6 Qh5-d1 c5xb4
    # 14    -21     975 Ng1-h3 d7-d5 d2-d4 Bc8xh3 g2xh3 e7-e6 Nb1-c3 Ng8-f6 e2-e3 Nb8-c6 Bf1-g2 h7-h6 Ke1-g1 Bf8-d6 Bc1-d2 a7-a6 a2-a3 Ke8-g8 Qd1-e2 Qd8-e7 f2-f4 Rf8-d8 Bg2-f3 Qe7-e8 Qe2-f2 Ra8-c8 Rf1-e1 Qe8-d7 e3-e4 d5xe4 Bf3xe4
    # 15    -16    1269 f2-f4 Ng8-f6 d2-d3 d7-d5 Nb1-c3 e7-e6 Ng1-f3 Bf8-c5 e2-e4 Ke8-g8 d3-d4 d5xe4 Nc3xe4 Bc5xd4 Ne4xf6 Bd4xf6 Bc1-e3 Nb8-d7 c2-c3 Rf8-e8 g2-g3 c7-c5 Bf1-d3 b7-b6 Bd3-e4 Ra8-b8
    # 16      0    1953 c2-c4 e7-e5 Ng1-f3 e5-e4 Nf3-d4 Bf8-d6 d2-d3 e4xd3
    # 17    -26     552 h2-h4 Ng8-f6 d2-d4 Nb8-c6 e2-e3 d7-d6 Nb1-c3 e7-e5 Ng1-f3 Bc8-e6 d4xe5 d6xe5 a2-a3 Nf6-g4 Bf1-d3 Bf8-c5 Ke1-f1 a7-a6 Rh1-h3 Ke8-g8
    # 18    -24     629 a2-a4 e7-e5 e2-e4 d7-d6 d2-d3 Nb8-d7 Nb1-c3 Ng8-f6 Bc1-d2 Nd7-c5 f2-f3 Bc8-e6 Ke1-f2 h7-h6 Ng1-e2 Bf8-e7 Nc3-b5 Ke8-g8
    # 19    -30     434 g2-g4 d7-d5 d2-d4 e7-e5 d4xe5 Bc8xg4 Bf1-g2 c7-c6 Bc1-e3 Bf8-b4 c2-c3 Bb4-e7 Ng1-f3 Nb8-d7 Nb1-d2 Ra8-c8 Ke1-g1 h7-h6 Nd2-b3 Be7-h4 Qd1-c2 b7-b6
    # 20    -24     696 b2-b4 e7-e5 c2-c3 d7-d5 Ng1-f3 a7-a5 d2-d4 a5xb4 d4xe5 Bf8-c5 c3xb4 Bc5xb4 Bc1-d2 Nb8-c6 Nb1-c3 Ng8-e7

    nodes = 25942380 <66% qnodes> time = 11167ms nps = 2323128 eps = 1286962 nneps = 10311
    Tree: nodes = 3535380 depth = 53 pps = 10396 visits = 116094 
          qsearch_calls = 26356 search_calls = 0
    move d2d4
    Bye Bye


#### Modifying Scorpio.ini for NN use

Once you make sure everything is working you can save the command line options in scorpio.ini
so that you just specify ./scorpio next time. For something that looks like AlphaZero's MCTS+NN 
search and evaluation use the following options

    montecarlo               1
    frac_alphabeta           0
    backup_type              6
    use_nn                   1
    nn_path                  nets-ccrl/net-6x64.pb

<a name="egbb"></a>
### EGBB

  Scorpio uses its own endgame bitbases upto 6 pieces.
   
#### Installation 

  First You have to download some of the bitbases.
  
 * 4-men (3.5 Mb) 
        [Download](http://shawul.olympuschess.com/egbb/egbb4men.zip)
 * 5-men (211 Mb) 
        [Download](http://shawul.olympuschess.com/egbb/egbb5men.zip)
 * 6-men (48.1 Gb) 
        [Torrent](http://oics.olympuschess.com/tracker/index.php)
 
Then put them anywhere in your computer and set the egbb_path, egbb_cache_size 
  and egbb_load_type in the scorpio ini file. Suggested cache size for 4 men 
  egbbs is minimum 4 Mb, for 5 piece minimum 16 Mb, and for 6-men as much as 
  you can. Putting the EGBBs on SSD or USB drives can also speed up access time.

#### For programmers

  For those who want to add support to the bitbases, please take a look 
  at the probe.cpp. There are two files egbbdll.dll and egbbso.so that 
  you can use to probe the bitbases in Windows and Linux. These files 
  should be put in the same directory as the egbbs (not anywhere else!).
  So what you need to do is load these libraries at run time and use their 
  functions. Your engines size hardly increases unlike the case if you
  used Nalimov's egtbs. Plus the engine don't have to be recompiled when-
  ever updates for the egbbs are released. If your engine added supprot for 
  the very first version of the egbbdll.dll , which btw is completely 
  different from the current design, can still use the latest available 
  egbbdll.
        
  The 4men bitbases are loaded to RAM by default so they are fast to use. 
  You can also load the 5 men egbb's to RAM but they require too much RAM.
  So you might prefer to probe them on disk like i do. But make sure that 
  you don't call them near the leaves of the search tree if they are not 
  loaded on RAM.
               
     
  After egbbdll.dll is loaded and we have to get the address of 
  probe_egbb and load_egbb functions. New functions are provided for 6-men
  egbbs , probe_egbb_xmen and load_egbb_xmen.

       load_egbb = (PLOAD_EGBB) GetProcAddress(hmod,"load_egbb_xmen");
       probe_egbb = (PPROBE_EGBB) GetProcAddress(hmod,"probe_egbb_xmen");

  Then we can call these functions like any other standard functions.
      
       typedef int (*PLOAD_EGBB) (char* path,int cache_size,int load_options);
  
  Parameters

       path         => it is the directory where the bitbases are located
       cache_size   => size of cache used in bytes
       load_options => options to load some bitbase in to RAM. By default 
                       the 4 men are loaded in to RAM.
  Returns

       true => if loading succeeds false otherwise. 

       typedef int (*PPROBE_EGBB) (int player, int* piece, int* square);
  Parameters

        player   => side to move
        *piece   => piece types
        *square  => square of pieces (A1=0...H1=7...A8=56...H8...63)

  The piece array should be terminated with 0 and the last element of square
  array should be the enpassant square if it exists or 0 otherwise.

  Returns

        0         => draw
        >0        => win
        <0        => loss
       _NOTFOUND  => bitbase is not included

  Scores are modified according to many factors.

        score = WIN_SCORE 
		- 10 * distance(SQ6488(w_king),SQ6488(b_king)) 
		+ (5 - all_c) * 1000;

  This type of scoring helps in making progress by prefering 3-men endings 
  over 5-men, cornering losing king, handling special endings such as KBNK.

#### Note
  
Those who supported the egbbs does not have to use the method scorpio 
uses for probing. It is upto the programmer to decide what to do 
with them. 
            

<a name="ackn"></a>
### Acknowledgement

Thanks to the following persons for helping me with development
of Scorpio: Dann Corbit. Gerhard Schwager, Oliver Deuville, Gunther Simon,
Leo Dijksman, Ed Shroeder and Pradu Kanan, Shaun Brewer, Werner Schule,
Klaus Friedel, Andrew Fan, Andres Valverde, Bryan Hoffman, Martin Thorensen

<a name="book"></a>
### Book

Salvo Spitaleri is the book author for scorpio. 
Thanks also goes to Oliver Deuville for the book which
earlier version use.
