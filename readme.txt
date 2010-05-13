Scorpio 2.5 cluster beta
------------------------

Changes
-------
    * Cluster version with YBW parallel search. It doesnt scale well at all
      and probably have tons of bugs. I just put this up on my website for those
      who want to help me with testing. 
 
      The way it works is like this. Lets say if you 2 quads connected by ethernet,
      then you start two processes on each computer using "mpirun -pernode" or some other
      command. Then you set the "mt" option in ini file to "auto" so that it detects the
      number of processors automatically. You can also opt to start a process for each processor
      in which case the mt should be set to 1. 8 processes and 0 threads will be started in this case.

      New Parameters:
	
        CLUSTER_SPLIT_DEPTH (default 8) . Recommended value if you have dual core
        cpus is 12. And if you quads probably larger... Experiment with this to
        get the correct values for your machines.

	SMP_SPLIT_DEPTH (default 4)  -> plies below which split is not allowed.

      The "make cluster" command will compile you a cluster version of scorpio.
      See the makefile for details and change accordingly. It can be compiled for windows
      also using MPICH library. Dann Corbit did that already.

      Good luck and prepare yourself for a huge trouble.
      It probably would have been a wise choice to work on it more before releasing but
      I want to move on working on something else.

    * The serial version is absolutely same us 2.5 so please do not test it unless
      you have a cluster. Infact I have taken out most of the changes I made to make
      it play the same as the old version.
     
      
Thanks
------
    Dann Corbit
    Gerhard Schwager
    Oliver Deuville
    Gunther Simon
    Leo Dijksman 
    Ed Shroeder 
    Pradu Kanan 
    
    The following people helped me debug the smp engine by
    running scorpio on their multi processor machine.
       Shaun Brewer
       Werner Schule 
       Klaus Friedel
       Andrew Fan
    
    Faster compiles:
       Jim Ablet
       Bryan Hoffman
    
    Andres Valverde
       CCT11 operator
       And all friends from Olivier's Live broadcasts :)
BOOK
----
    Salvo Spitaleri is the book author for scorpio. 
    Thanks also goes to Oliver Deuville for the book which
    earlier version use.

Personality
-----------
    scorpio supports multi personality for its opening, middle game
    and endgame. You can also use one personality for the whole of the
    game.
    eg. personality mid
           or
        multi_personality opn mid end

EGBBs
-----
    Scorpio uses its own endgame bitbases upto 5 pieces.
   
    Installation 
    ------------
       First You have to download the 5men bitbases from Leo Dijksman's WBEC site 
       http://dcorbit2008.corporate.connx.com/chess-engines/scorpio/ . 
       The egbbs are 225mb in size. Then put them anywhere in your computer and set the egbb_path , 
       egbb_cache_size and egbb_load_type in the scorpio ini file. Suggested cache size for 4 men 
       egbbs is minimum 4mb, and for 5 piece minimum 16mb.

    For programmers
    ---------------
       For those who want to add support to the bitbases, please take a look at the probe.cpp.
       There are two files egbbdll.dll and egbbso.so that you can use to probe the bitbases in
       Windows and Linux. These files should be put in the same directory as the egbbs (not anywhere else!).
       So what you need to do is load these libraries at run time and use their functions. Your engines
       size hardly increases unlike the case if you used Nalimov's egtbs. Plus the engine don't have to be
       recompiled whenever updates for the egbbs are released. If your engine added supprot for the very first
       version of the egbbdll.dll , which btw is completely different from the current design, can still use
       the latest available egbbdll.
        
       The 4men bitbases are loaded to RAM by default so they are fast to use. You can also load the 5 men
       egbb's to RAM but they require too much RAM(225mb). So you might prefer to probe them on disk like i do.
       But make sure that you don't call them near the leaves of the search tree if they are not loaded on RAM.
               
     
       After egbbdll.dll is loaded and we have to get the address of 
       probe_egbb and load_egbb functions. New functions are provided for 5men
       egbbs , probe_egbb_5men and load_egbb_5men. So if you want to use 5men
            load_egbb = (PLOAD_EGBB) GetProcAddress(hmod,"load_egbb_5men");
	    probe_egbb = (PPROBE_EGBB) GetProcAddress(hmod,"probe_egbb_5men");
       Then we can call these functions like any other standard functions.
      
      
       typedef int (*PLOAD_EGBB) (char* path,int cache_size,int load_options);
      
       Parameters:
          path         => it is the directory where the bitbases are located
          cache_size   => size of cache used in bytes
          load_options => options to load some bitbase in to RAM. By default the 
                          4 men are loaded in to RAM.
       Returns:
          true if loading succeeds false otherwise. 

       typedef int (*PPROBE_EGBB) (int player, int w_king, int b_king,
			   int piece1, int square1,
			   int piece2, int square2,
			   int piece3, int square3);
       Parameters:
          player  => side to move
          w_king  => square of white king (remeber to use A1 = 0 ... H8 = 63)
          b_king  => square of black king
          piece1  => type of piece 
          square1 => square of piece1 etc...
       Returns:
          0         => draw
          >0        => win
          <0        => loss
         _NOTFOUND  => bitbase is not included

       Scores are modified according to many factors.
          score = WIN_SCORE 
		  - 10 * distance(SQ6488(w_king),SQ6488(b_king)) 
		  + (5 - all_c) * 1000;
          This type of scoring helps in making progress. 
           ->3men endings are prefered over 5men.
           ->Cornering the losing king is a good idea
          Special endings like kbnk are also taken care of.


       NOTE : 
            Those who supported the egbbs does not have to use the method scorpio uses
            for probing. It is upto the programmer to decide what to do with them. 
            
       
                