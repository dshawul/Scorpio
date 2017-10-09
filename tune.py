import sys
import time 
import subprocess
import threading 
import math
import scipy.optimize as opt
import numpy as np
# from numpy import linalg as la
from random import randint

#list of parameters to optimize
parameters = [ 
    ['ATTACK_WEIGHT',16],
    ['TROPISM_WEIGHT',8],
    ['PAWN_GUARD',16],
    ['HANGING_PENALTY',15],
    ['KNIGHT_OUTPOST',16],
    ['BISHOP_OUTPOST',12],
    ['KNIGHT_MOB',16],
    ['BISHOP_MOB',16],
    ['ROOK_MOB',16],
    ['QUEEN_MOB',16],
    ['PASSER_MG',12],
    ['PASSER_EG',16],
    ['PAWN_STRUCT_MG',12],
    ['PAWN_STRUCT_EG',16],
    ['ROOK_ON_7TH',12],
    ['ROOK_ON_OPEN',16],
    ['ROOK_SUPPORT_PASSED_MG',10],
    ['ROOK_SUPPORT_PASSED_EG',20],
    ['TRAPPED_BISHOP',80],
    ['TRAPPED_KNIGHT',50],
    ['TRAPPED_ROOK',90],
    ['QUEEN_MG',1050],
    ['QUEEN_EG',1050],
    ['ROOK_MG',500],
    ['ROOK_EG',500],
    ['BISHOP_MG',325],
    ['BISHOP_EG',325],
    ['KNIGHT_MG',325],
    ['KNIGHT_EG',325],
    ['PAWN_MG',80],
    ['PAWN_EG',100],
    ['BISHOP_PAIR_MG',16],
    ['BISHOP_PAIR_EG',16],
    ['MAJOR_v_P',180],
    ['MINOR_v_P',90],
    ['MINORS3_v_MAJOR',45],
    ['MINORS2_v_MAJOR',45],
    ['ROOK_v_MINOR',45],
    # ['ELO_HOME',20],
    # ['ELO_DRAW',50],
    # ['ELO_HOME_SLOPE_PHASE',0],
    # ['ELO_DRAW_SLOPE_PHASE',0],
    # ['ELO_HOME_SLOPE_KSAFETY',0],
    # ['ELO_DRAW_SLOPE_KSAFETY',0]
]

#frac = fraction of positions to consider
#prior = fraction of visited position with mse=0 to be applied as prior
frac = 0.01
prior = 0.1

#lock for printing
lock = threading.Lock()

#class to handle engine
class Engine(threading.Thread):
    def __init__(self):
        self.p = None
        self.line = None
        self.hasInput = False
        threading.Thread.__init__(self)

    def run(self):
        self.p = subprocess.Popen('./scorpio',
                             cwd='./',
                             shell=False,
                             stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE,
                             stderr=None)
        self.loop()

    def loop(self):
        while True:
            line = self.p.stdout.readline()
            if line != '':
                lock.acquire()
                self.line = line
                self.hasInput = True
                print 'Engine(' + str(int(time.time())) + ') >>> ' + self.line.rstrip()
                lock.release()
                continue
            else:
                break

    def send(self,s,logging=True):
        if(logging):
            lock.acquire()
            print 'Engine(' + str(int(time.time())) + ') <<< ' + s
            lock.release()
        sn = s + '\n'
        self.p.stdin.write(sn)

    def prepareEngine(self):
        self.send('xboard')
        self.send('protover 2')
        # self.send('loadepd tune/ruy_tune/data/quiescent_positions_with_results')
        self.send('loadepd tune/data/quiet-labeled.epd')
        # self.send('loadepd tune/merged.epd')
        self.send('sd=0 pvstyle=2 ELO_MODEL=0')
        #compute jacobian for a linear eval (comment this out if not)
        self.send('jacobian')
        time.sleep(60)

    def getMSE(self,seed): 
        s = 'mse ' + str(frac) + ' ' + str(prior) + ' ' + str(seed)
        lock.acquire()
        self.line = ''
        lock.release()
        self.send(s)
        while(not (self.hasInput and self.line != '')):
            continue
        E = float(self.line.rstrip())
        self.hasInput = False
        return E 

    def getGradMSE(self,x0):
        delta = 1
        seed = randint(0,32767)
        self.sendParams(x0)
        U0 = self.getMSE(seed)
        x1 = np.copy(x0)
        grad = np.zeros(len(x0))
        for i in range(0,len(x0)): 
           # delta = x0[i] / 4
           # if delta < 1:
           #    delta = 1
           x1[i] = x0[i] + delta
           self.sendParams(x1,False)
           U1 = self.getMSE(seed)
           x1[i] = x0[i]
           grad[i] = (U1 - U0) / delta
        return grad

    def sendParams(self,x0,logging=True): 
        s = '' 
        for i in range(0,len(x0)): 
            s = s + parameters[i][0] + ' ' + str(int(round(x0[i]))) + '\n'
            # s = s + parameters[i][0] + ' ' + str(x0[i]) + '\n'
        s= s[:-1]
        self.send(s,logging)

def GradientDescent(grad_func,x0,niter,alpha):
    gamma = 0.1
    gp = np.zeros(len(x0))
    d = np.zeros(len(x0))
    for i in range(0,niter): 
        g = grad_func(x0)
        # if(i > 1):
        #   gamma = (la.norm(g)**2) / (la.norm(gp)**2)
        d = -g + gamma * d
        x0 = x0 + alpha * d
        # gp = np.copy(g)

def main(argv): 
       
    #launch engine
    myEngine = Engine()
    myEngine.start()
    time.sleep(1)
    myEngine.prepareEngine()
    time.sleep(2)
    
    #optimize parameters
    val = []
    for a in parameters:
        val.append(a[1])
    x0 = np.array(val)

    #stochastic gradient descent or conjugate gradient
    GradientDescent(myEngine.getGradMSE, x0, 100000, 1e4)

    sys.exit(0)

if __name__ == "__main__": 
    main(sys.argv[1:])
