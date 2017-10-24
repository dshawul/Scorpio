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
    ['KNIGHT_OUTPOST_MG',16],
    ['KNIGHT_OUTPOST_EG',16],
    ['BISHOP_OUTPOST_MG',12],
    ['BISHOP_OUTPOST_EG',12],
    ['KNIGHT_MOB_MG',16],
    ['KNIGHT_MOB_EG',16],
    ['BISHOP_MOB_MG',16],
    ['BISHOP_MOB_EG',16],
    ['ROOK_MOB_MG',16],
    ['ROOK_MOB_EG',16],
    ['QUEEN_MOB_MG',16],
    ['QUEEN_MOB_EG',16],
    ['PASSER_MG',12],
    ['PASSER_EG',16],
    ['CANDIDATE_PP_MG',10],
    ['CANDIDATE_PP_EG',16],
    ['PAWN_DOUBLED_MG',8],
    ['PAWN_DOUBLED_EG',10],
    ['PAWN_ISOLATED_ON_OPEN_MG',15],
    ['PAWN_ISOLATED_ON_OPEN_EG',20],
    ['PAWN_ISOLATED_ON_CLOSED_MG',10],
    ['PAWN_ISOLATED_ON_CLOSED_EG',10],
    ['PAWN_WEAK_ON_OPEN_MG',12],
    ['PAWN_WEAK_ON_OPEN_EG', 8],
    ['PAWN_WEAK_ON_CLOSED_MG',6],
    ['PAWN_WEAK_ON_CLOSED_EG',6],
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
    ['ELO_HOME',20],
    ['ELO_DRAW',50],
    ['ELO_HOME_SLOPE_PHASE',0],
    ['ELO_DRAW_SLOPE_PHASE',0],
]

#frac = fraction of positions to consider
#prior = fraction of visited position with mse=0 to be applied as prior
frac = 0.01
prior = 0.1

#print parameters every n seconds
print_time = 10

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

    def getGradMSElinear(self,x0):
        delta = 1
        seed = randint(0,32767)
        self.sendParams(x0,False)
        grad = np.zeros(len(x0))

        x1 = np.copy(x0)
        U0 = self.getMSE(seed)
        for i in range(0,len(x0)): 
           # delta = x0[i] / 4
           # if delta < 1:
           #    delta = 1
           x1[i] = x0[i] + delta
           self.sendParam(x1,i,False)
           U1 = self.getMSE(seed)
           x1[i] = x0[i]
           self.sendParam(x1,i,False)
           grad[i] = (U1 - U0) / delta
        return grad

    def getGradMSEblock(self,x0): 
        seed = randint(0,32767)
        self.sendParams(x0,False)
        grad = np.zeros(len(x0))

        s = 'gmse ' + str(frac) + ' ' + str(prior) + ' ' + str(seed)
        lock.acquire()
        self.line = ''
        lock.release()
        self.send(s)
        while(not (self.hasInput and self.line != '')):
            continue
        grad = np.fromstring(self.line.rstrip(), dtype=np.float, sep=' ')
        self.hasInput = False
        return grad

    def sendParams(self,x0,logging=True): 
        s = '' 
        for i in range(0,len(x0)): 
            s = s + parameters[i][0] + ' ' + str(int(round(x0[i]))) + '\n'
        s= s[:-1]
        self.send(s,logging)

    def sendParam(self,x0,i,logging=True): 
        s = parameters[i][0] + ' ' + str(int(round(x0[i]))) + '\n'
        s= s[:-1]
        self.send(s,logging)

#print parameters
def print_params(x0):
    s = ''
    for i in range(0,len(x0)): 
        s = s + 'static PARAM ' + parameters[i][0] + ' = ' + str(int(round(x0[i]))) + ';\n'
    print s

    s = ''
    for i in range(0,len(x0)): 
        s = s + '[\'' + parameters[i][0] + '\',' + str(int(round(x0[i]))) + '],\n'
    print s

def GradientDescent(grad_func,x0,niter,alpha,start_t):
    gamma = 0.1
    gp = np.zeros(len(x0))
    d = np.zeros(len(x0))
    for i in range(0,niter): 
        print "==== Iteration ", i, " ====="
        g = grad_func(x0)
        # if(i > 1):
        #   gamma = (la.norm(g)**2) / (la.norm(gp)**2)
        d = -g + gamma * d
        x0 = x0 + alpha * d
        # gp = np.copy(g)
        if time.time() - start_t > print_time:
            print_params(x0)
            start_t = time.time()

def main(argv): 

    #launch engine
    myEngine = Engine()
    myEngine.start()
    time.sleep(1)
    myEngine.prepareEngine()
    time.sleep(2)
    
    start_t = time.time()
    

    #optimize parameters
    val = []
    for a in parameters:
        val.append(a[1])
    x0 = np.array(val)

    #stochastic gradient descent or conjugate gradient
    GradientDescent(myEngine.getGradMSEblock, x0, 10000, 1e4,start_t)

    sys.exit(0)

if __name__ == "__main__": 
    main(sys.argv[1:])
