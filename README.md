### Table of Contents
1. [Scorpio](#scorpio)
2. [Installation](#install)
3. [Goals](#goals)
4. [Parallel](#parallel)
5. [MCTS](#mcts)
6. [Neural Networks](#nns)
7. [EGBB](#egbb)
8. [Acknowledgement](#ackn)
9. [Book](#book)

<a name="scorpio"></a>
### Scorpio

Scorpio is a strong chess engine that uses neural networks for evaluation. It can be used 
with GUIs that support either the Winboard or UCI protocol such as Winboard, Arena, Fritz and Shredder interfaces

<a name="install"></a>
### Installation

For detailed installation intructions, please take a look at [INSTALL.md](https://github.com/dshawul/Scorpio/blob/master/INSTALL.md)

<a name="goals"></a>
### Goals

The main goals of Scorpio are:

  * [Parallel](#parallel) : Experiments with parallel search algorithms such as YBW and ABDADA 
    algorithms both on shared memory (SMP) and non-uniform memory access (NUMA) systems. 
    Also distributed parallel search on a loosely coupled cluster of computers is done using YBW.

  * [MCTS](#mcts) : Experiments with montecarlo tree search (MCTS) for chess. It supports alpha-beta
    rollouts with LMR and null move in the tree part of the search, coupled with
    standard MCTS rollouts or standard alpha-beta search.

  * [Neural Networks](#nns) : Experiments with neural network evaluation ala AlaphaZero.

  * [EGBB](#egbb) : Experiments with encoding, compression and use of endgame bitbases (EGBBS) of upto 6 pieces
      
<a name="parallel"></a>
### Parallel

Cluster version of scorpio can be compiled with MPI libraries such as OpenMPI or MPICH. Work is distributed using YBW algorithm. To run Scorpio on
cluster start one processor per node using "mpirun -pernode" and set "mt" option to "auto" so that it detects number of cores automatically.
You can also start processes only but it will run slower. Experiment with split depth parameters for optimal performance.
   * CLUSTER\_SPLIT\_DEPTH (default 8)
   * SMP\_SPLIT\_DEPTH (default 4)    

<a name="mcts"></a>
### MCTS

The montecarlo tree search engine, when not using neural networks, uses alpha-beta rollouts according to 
[Huang paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2014/11/huang_rollout.pdf).
This is much stronger than standard MCTS becausee chess is full of tactics. Scorpio-MCTS that
stores all the tree in memory has become very close in strength to the standard alpha-beta searcher.
It can actually become same strength as the standard if we limit the amount of tree stored in memory via "treeht" parameter.
When the MCTS search runs out memory, it will spawn standard recursive alpha-beta search at the leaves
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

The neural network version of Scorpio works using egbbdll that provides neural network inference via tensorflow and/or TensorRT backends.
NN version has lots of dependencies, hence, the recommended way of installation is through the install
scripts as described in [INSTALL.md](https://github.com/dshawul/Scorpio/blob/master/INSTALL.md)

### Building egbbdll

Scorpio requires egbbdll to run with neural networks, both NN and NNUEs.
To build egbbdll follow the steps provided [here](https://github.com/dshawul/egbbdll)

### Building Scorpio

This is very straightforward:

    git clone https://github.com/dshawul/Scorpio.git
    cd Scorpio && ./build.sh && cd ..

This will build you a scorpio executable in the bin/ directory.

<a name="egbb"></a>
### EGBB

For details about Scorpio's EGBBs and how to probe them, please take a loot at [EGBB.md](https://github.com/dshawul/Scorpio/blob/master/EGBB.md)

### Logo
The ScorpioNN logo is designed by KanChess of TCEC who gratiously donated it to 
the Scorpio project, many thanks!
Thanks also to Graham Banks who designed the previous logo!

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
