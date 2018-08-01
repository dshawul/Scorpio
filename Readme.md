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

The neural network version works with a tensorflow backend. You need to build the C++ tensorflow library
available [here](https://github.com/FloopCZ/tensorflow_cc), and then compile egbbdll using this library
for neural network inference.

####   Building tensorflow_cc

To build and install the "shared" tensorflow_cc library, follow the instructions given [here](https://github.com/FloopCZ/tensorflow_cc)

It may be easier to use docker using the command below instead of building the shared library with bazel yourself.

    docker run --runtime=nvidia -it floopcz/tensorflow_cc:ubuntu-shared-cuda /bin/bash

Once the build and install (or docker load) is complete, you should see the tensorflow and protobuf libraries

    $ ls /usr/local/lib/tensorflow_cc/
    libprotobuf.a  libtensorflow_cc.so

#### Building egbbdll and Scorpio

This is very straightforward:

    git clone https://github.com/dshawul/egbbdll.git
    cd egbbdll && make && cd ..
    
    git clone https://github.com/dshawul/Scorpio.git
    cd Scorpio && make && cd ..

This will get you an egbbso64.so and linux scorpio executable.
Then, copy the egbbso64.so to the directory you store your egbb files, overwriting the existing one.

    cp egbbdll/egbbso64.so  $EGBB_DIRECTORY

#### Downloading networks

Create a directory to store network files and downlowd networks from my site

    mkdir Scorpio/nets-ccrl
    cd Scorpio/nets-ccrl
    wget https://sites.google.com/site/dshawul/net-1x32.pb
    wget https://sites.google.com/site/dshawul/net-3x64.pb
    wget https://sites.google.com/site/dshawul/net-6x64.pb
    cd ..

#### Testing

    $ ./scorpio use_nn 1 montecarlo 1 frac_alphabeta 0 go quit
    feature done=0
    ht 4194304 X 16 = 64.0 MB
    eht 524288 X 8 = 8.0 MB
    pht 32768 X 24 = 0.8 MB
    treeht 419430400 X 32 = 12800.0 MB
    processors [1]
    EgbbProbe 4.1 by Daniel Shawul
    180 egbbs loaded !      
    Loading neural network....
    Neural network loaded !      
    loading_time = 0s
    [st = 11114ms, mt = 29250ms , hply = 0 , moves_left 10]
    63 -2 111 153169  e2-e4 c7-c6 Nb1-c3 e7-e5 d2-d4 e5xd4 Qd1xd4
    64 20 222 344751  h2-h4 h7-h5 e2-e4 e7-e5 d2-d3 Nb8-c6 Ng1-f3 Ng8-f6
    65 40 333 532303  d2-d4 Ng8-f6 c2-c4 d7-d6 Ng1-f3 Nb8-c6
    66 22 444 747846  h2-h3 h7-h5 e2-e4 e7-e5 Ng1-f3 d7-d6 d2-d4 e5xd4 Nf3xd4
    67 0 556 941626  e2-e3 Nb8-a6 c2-c3 d7-d5
    68 19 667 1168057  e2-e3 d7-d6 h2-h3 Bc8-d7 Nb1-c3 e7-e5 d2-d4
    69 16 778 1358238  e2-e3 h7-h6 g2-g3 d7-d5 d2-d4 Ng8-f6 Ng1-f3 Nb8-c6
    70 3 889 1576992  d2-d4 d7-d5 Nb1-c3 Bc8-e6 Ng1-f3 Ng8-f6 e2-e3 Nb8-c6 Bf1-d3
    71 9 1000 1819974  e2-e3 h7-h6 c2-c4 d7-d6 Ng1-h3 Bc8-f5 Nb1-c3 e7-e5 d2-d4
    
    #  1    -82    1218 e2-e4 e7-e6 f2-f4 d7-d5 e4xd5 e6xd5 d2-d4
    #  2    -47    4724 d2-d4 d7-d5 Nb1-c3 Ng8-f6 e2-e3 h7-h6 Ng1-f3 e7-e6 Bf1-b5
    #  3    -72     124 Nb1-c3 f7-f5 e2-e3 e7-e5 d2-d4
    #  4     -6    2575 Ng1-f3 Ng8-f6 h2-h3 Nb8-a6 d2-d3 d7-d6 e2-e4 e7-e5 Nb1-c3 Na6-c5 Bc1-e3 b7-b6 Be3xc5
    #  5    -46    7639 e2-e3 h7-h6 c2-c4 d7-d6 h2-h3 b7-b6 Ng1-f3 e7-e5 d2-d4 Nb8-c6 d4xe5
    #  6    -51     963 d2-d3 e7-e5 h2-h3 d7-d5 Ng1-f3 Nb8-c6 e2-e4
    #  7    -85     118 g2-g3 e7-e5 e2-e4 d7-d5 e4xd5 Qd8xd5 f2-f3
    #  8    -69      84 b2-b3 c7-c6 e2-e4 d7-d5 e4xd5
    #  9   -109      73 f2-f3 d7-d5 Nb1-c3 e7-e5 e2-e4
    # 10    -49    1008 h2-h3 Nb8-c6 Nb1-a3 e7-e5 e2-e4 Ng8-f6 d2-d3
    # 11    -89     248 c2-c3 e7-e6 d2-d4 d7-d5 Ng1-f3 Ng8-f6 h2-h3
    # 12    -72      97 a2-a3 d7-d5 d2-d4 Ng8-f6 Ng1-f3 e7-e6 Nb1-c3
    # 13    -75     160 Nb1-a3 e7-e5 e2-e3 d7-d5 d2-d4
    # 14    -58     259 Ng1-h3 f7-f5 d2-d4 Ng8-f6 Nh3-g5
    # 15    -82      47 f2-f4 e7-e6 e2-e4 d7-d5 e4xd5 e6xd5 d2-d4
    # 16    -66     811 c2-c4 Ng8-f6 f2-f4 d7-d5 c4xd5
    # 17    -64     846 h2-h4 Ng8-f6 d2-d4 e7-e6 Ng1-f3 d7-d5 Nb1-c3
    # 18    -74     292 a2-a4 h7-h5 e2-e4 e7-e5 Nb1-c3
    # 19   -143      39 g2-g4 Ng8-f6 e2-e3 d7-d5 g4-g5
    # 20    -65     124 b2-b4 c7-c6 e2-e4 d7-d5 e4xd5
    
    nodes = 2055820 <93% qnodes> time = 11116ms nps = 184942 eps = 145160
    Tree: nodes = 647510 depth = 14 pps = 1929 visits = 21449 
          qsearch_calls = 626264 search_calls = 0
    move e2e3
    Bye Bye

#### Modifying Scorpio.ini for NN use

Once you make sure everything is working you can save the command line options in scorpio.ini
so that you just specify ./scorpio next time. For something that looks like AlphaZero's MCTS+NN 
search and evaluation use the following options

    montecarlo               1
    frac_alphabeta           0
    backup_type              4
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
