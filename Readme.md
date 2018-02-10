### Scorpio

Scorpio is a strong chess engine at the grand master level. It can be used 
with GUIs that support the Winboard protocol natively such as Winboard and
Arena, or via the Wb2Uci adapter in Fritz and Shredder interfaces.

### Goals

The main goals of Scorpio are:

  * experiments with parallel search algorithms such as YBW and ABDADA 
    algorithms both on shared memory (SMP) and non-uniform memory 
    access (NUMA) systems. Also distributed parallel search on a loosely
    coupled cluster of computers is done using YBW.

  * use of native endgame bitbases (EGBBS) of upto 6 pieces

  * Experiments with montecarlo tree search (MCTS) for chess. It supports alpha-beta
    rollouts with LMR and null move in the tree part of the search, coupled with
    standard MCTS rollouts or standard alpha-beta search.
      
### Parallel

  * Cluster version of scorpio can be compiled with MPI libraries OpenMPI
    or MPICH. Work is distributed using YBW algorithm. To run Scorpio on
    cluster start one processor per node using "mpirun -pernode" and set
    "mt" option to "auto" in scorpio.ini so that it detects number of pro-
    cessors automatically. You can also start processes only but it will
    run slower. Experiment with split depth parameters for optimal perfor-
    mance.
       * CLUSTER\_SPLIT\_DEPTH (default 8)
       * SMP\_SPLIT\_DEPTH (default 4)    

### MCTS

  * The montecarlo tree search engine uses alpha-beta rollouts according to 
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

  * The parallel search for MCTS uses virtual loss to distribute work among threads in standard MCTS rollouts,
    and ABDADA like BUSY flag for alpha-beta rollouts. ABDADA and parallel MCTS from the Go world are very similar
    in nature.

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
            

### Acknowledgement

Thanks to the following persons for helping me with development
of Scorpio: Dann Corbit. Gerhard Schwager, Oliver Deuville, Gunther Simon,
Leo Dijksman, Ed Shroeder and Pradu Kanan, Shaun Brewer, Werner Schule,
Klaus Friedel, Andrew Fan, Andres Valverde, Bryan Hoffman, Martin Thorensen
    
### Fastest binaries

Jim Ablet compiles the fastest binaries for Windows, Linux and ARM. They are
available at his [homepage](http://jimablett.net63.net/)

### Book

Salvo Spitaleri is the book author for scorpio. 
Thanks also goes to Oliver Deuville for the book which
earlier version use.
