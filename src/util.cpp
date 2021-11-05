#include <sstream>
#include "scorpio.h"

static const char piece_name[] = "_KQRBNPkqrbnpZ";
static const char rank_name[] = "12345678";
static const char file_name[] = "abcdefgh";
static const char col_name[] = "WwBb";
static const char cas_name[] = "KQkq";
static const char cas_frc_name[] = "AaBbCcDdEeFfGgHh";
static const char start_fen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/*
print to logfile / stdout
*/
static FILE* log_file = 0;
static char log_name[MAX_STR];

void remove_log_file() {
    if(log_file) {
        fclose(log_file);
        remove(log_name);
        log_file = 0;
        log_on = false;
    }
}

#define PRINT(pf,mstr) {        \
    va_start(ap, format);       \
    vfprintf(pf, mstr, ap);     \
    va_end(ap);                 \
    fflush(pf);                 \
}

void print_log(const char* format,...) {
    if(!log_on || !log_file) return;
    l_lock(lock_io);
    va_list ap;
    PRINT(log_file,format);
    l_unlock(lock_io);
}

void print_std(const char* format,...) {
    CLUSTER_CODE(if(PROCESSOR::host_id != 0) return;)
    l_lock(lock_io);
    va_list ap;
    PRINT(stdout,format);
    l_unlock(lock_io);
}

void print(const char* format,...) {
    l_lock(lock_io);
    va_list ap;
    CLUSTER_CODE(if(PROCESSOR::host_id == 0))
    {
        PRINT(stdout,format);
    }
    if(log_on && log_file)
        PRINT(log_file,format); 
    l_unlock(lock_io);
}

void print_info(const char* format,...) {
    char str[1024];
    sprintf(str,"# %s",format);

    l_lock(lock_io);
    va_list ap;
    CLUSTER_CODE(if(PROCESSOR::host_id == 0))
    {
        PRINT(stdout,str);
    }
    if(log_on && log_file)
        PRINT(log_file,str);    
    l_unlock(lock_io);
}

void print_cluster(const char* str) {
    print(str);
#ifdef CLUSTER
    if(use_abdada_cluster
        && PROCESSOR::n_hosts > 1
        && PROCESSOR::host_id > 0
        ) {
        PROCESSOR::send_string(str);
    }
#endif
}

#undef PRINT
/*
move to string conversions
*/
void sq_str(const int& sq,char* s) {
    *s++ = file_name[file(sq)];
    *s++ = rank_name[rank(sq)];
    *s = 0;
}
void str_sq(const char* s,int& sq) {
    sq = SQ(s[1]-'1', s[0]-'a');
}
void mov_strx(const MOVE& move,char* s) {
    *s++ = file_name[file(m_from(move))];
    *s++ = rank_name[rank(m_from(move))];
    *s++ = file_name[file(m_to(move))];
    *s++ = rank_name[rank(m_to(move))];
    if(m_promote(move)) {
        *s++ = piece_name[PIECE(m_promote(move)) + 6];
    }
    *s = 0;
}
void SEARCHER::mov_str(const MOVE& move,char* s) {
    MOVE tmove = move;
    if((variant == 1) && is_castle(move)) {
        int to = m_to(move);
        if(to == G1)      to = frc_squares[1];
        else if(to == C1) to = frc_squares[2];
        else if(to == G8) to = frc_squares[4];
        else if(to == C8) to = frc_squares[5];
        tmove &= ~TO_FLAG;
        tmove |= (to << 8);
    }
    mov_strx(tmove,s);
}
void mov_san(const MOVE& move,char* s) {
    if(is_castle(move)) {
        if(m_to(move) > m_from(move)) strcpy(s,"O-O");
        else strcpy(s,"O-O-O");
        return;
    }
    if(PIECE(m_piece(move)) != pawn) 
        *s++ = piece_name[PIECE(m_piece(move))];
    *s++ = file_name[file(m_from(move))];
    *s++ = rank_name[rank(m_from(move))];
    if(m_capture(move)) *s++='x';
    *s++ = file_name[file(m_to(move))];
    *s++ = rank_name[rank(m_to(move))];
    if(m_promote(move)) {
        *s++ = '=';
        *s++ = piece_name[PIECE(m_promote(move))];
    }
    *s = 0;
}
void str_movx(MOVE& move,char* s) {
    int promote,from,to;
    s[0] = (char)tolower(s[0]);
    s[2] = (char)tolower(s[2]);
    s[4] = (char)tolower(s[4]);
    if((s[0] >= 'a') && (s[0] <= 'h') &&
        (s[1] >= '1') && (s[1] <= '8') &&
        (s[2] >= 'a') && (s[2] <= 'h') &&
        (s[3] >= '1') && (s[3] <= '8'));
    else {
        move = 0;
        return;
    }
    from = SQ(s[1]-'1', s[0]-'a');
    to = SQ(s[3]-'1', s[2]-'a');
    switch(s[4]) {
       case 'n':promote = knight;break;
       case 'b':promote = bishop;break;
       case 'r':promote = rook;break;
       case 'q':promote = queen;break;
       default: promote = blank;
    }
    move = from | (to<<8) | (promote<<24); 
}
void SEARCHER::str_mov(MOVE& move, char* s) {
    if(variant == 1 && 
        ( (player == white && castle & WSLC_FLAG) || 
          (player == black && castle & BSLC_FLAG) )
        ) {
        char sc[8]={0}, lc[8]={0};
        if(player == white) {
            if(castle & WSC_FLAG) {
                sq_str(frc_squares[0], sc);
                sq_str(frc_squares[1], &sc[2]);
            }
            if(castle & WLC_FLAG) {
                sq_str(frc_squares[0], lc);
                sq_str(frc_squares[2], &lc[2]);
            }
        } else {
            if(castle & BSC_FLAG) {
                sq_str(frc_squares[3], sc);
                sq_str(frc_squares[4], &sc[2]);
            }
            if(castle & BLC_FLAG) {
                sq_str(frc_squares[3], lc);
                sq_str(frc_squares[5], &lc[2]);
            }
        }
        if(!strcmp(s,lc) || strstr(s,"O-O-O")) {
            if(player == white)
                move = frc_squares[0] | (C1 << 8) | (wking<<16) | CASTLE_FLAG;
            else
                move = frc_squares[3] | (C8 << 8) | (bking<<16) | CASTLE_FLAG;
            return;
        } else if(!strcmp(s,sc) || strstr(s,"O-O")) {
            if(player == white)
                move = frc_squares[0] | (G1 << 8) | (wking<<16) | CASTLE_FLAG;
            else
                move = frc_squares[3] | (G8 << 8) | (bking<<16) | CASTLE_FLAG;
            return;
        }
    }
    str_movx(move,s);
}
/*
Print elements of board
*/
void print_sq(const int& sq) {
    char f[6];
    sq_str(sq,f);
    print("%s",f);
}
void print_pc(const int& pc) {
    print("%c",piece_name[pc]);
}
void print_move(const MOVE& move) {
    char str[12];
    mov_strx(move,str);
    print(str);
}
void print_move_full(const MOVE& move) {
    char f[6],t[6];
    sq_str(m_from(move),f);
    sq_str(m_to(move),t);
    print("%s %s %c %c %c ep = %d cst=%d",f,t,piece_name[m_piece(move)],
        piece_name[m_capture(move)],piece_name[m_promote(move)],is_ep(move),is_castle(move));
}
void print_bitboard(uint64_t b){
    char hh[256];
    strcpy(hh,""); 
    for(int i=7;i>=0;i--) {
        for(int j=0;j<8;j++) {
            if(b & BB(SQ(i,j))) strcat(hh,"1 ");
            else strcat(hh,"0 ");
        }
        strcat(hh,"\n");
    }
    strcat(hh,"\n");
    print(hh);
}
void SEARCHER::print_board() const {
    int i,j;

    print("\n");
    for(i = 7; i >= 0; i--) {
        print("\t");
        for(j = 0;j < 8; j++) {
            print("%c",piece_name[board[SQ(i,j)]]);
        }
        print("\n");
    }

    char fen[MAX_FEN_STR];
    get_fen(fen);
    print(fen);
    print("\n");

#ifdef  MYDEBUG
    print("%d %d %d %d\n",piece_c[white],piece_c[black],pawn_c[white],pawn_c[black]);
    print("play = %d opp = %d ep = %d cas = %d fif = %d hkey 0x" FMTU64 "\n\n",player,
        opponent,epsquare,castle,fifty,hash_key);
    PLIST current;
    char  str[4];
    for(i = wking;i <= bpawn;i++) {
        current = plist[i];
        print_pc(i);
        print(": %d :",man_c[i]);
        while(current) {
            sq_str(current->sq,str);
            print("%5s",str);
            current = current->next;
        }
        print("\n");
    }
#endif
}
void SEARCHER::print_history() {
    for(int i = 0;i < hply;i++) {
        print("%d. ",i);
        print_move(hstack[i].move);
        print(" cst=%d ep=",hstack[i].castle);
        print_sq(hstack[i].epsquare);
        print(" fif=%d chk=%d",hstack[i].fifty,hstack[i].checks);
        print(" hkey 0x" FMTU64,hstack[i].hash_key);
        print("\n");
    }
}
void SEARCHER::print_stack() {
    for(int i = 0;i < ply;i++) {
        print("%d. ",i);
        print_move(stack[i].current_move);
        print(" depth=%d",stack[i].depth);
        print("\n");
    }
}
static void get_date(char* buffer) {
    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer,80,"%d-%m-%Y %I:%M:%S",timeinfo);
}
void SEARCHER::print_game(int res, FILE* fw, const char* event, 
                const char* whitep, const char* blackp, int Round) {
    int i = 0;
    char mvstr[12];
    char str[16];
    char date[32];
    char buffer[4096*8];
    int bcount;

    get_date(date);
    if(res == R_DRAW) strcpy(str,"1/2-1/2");
    else if(res == R_WWIN) strcpy(str,"1-0");
    else if(res == R_BWIN) strcpy(str,"0-1");
    else strcpy(str,"*");

    bcount = 0;

    if(event)  bcount += sprintf(&buffer[bcount],"\n[Event \"%s\"]\n",event);
    bcount += sprintf(&buffer[bcount],"[Date \"%s\"]\n",date);
    if(Round)  bcount += sprintf(&buffer[bcount],"[Round \"%d\"]\n",Round);
    if(whitep) bcount += sprintf(&buffer[bcount],"[White \"%s\"]\n",whitep);
    if(blackp) bcount += sprintf(&buffer[bcount],"[Black \"%s\"]\n",blackp);
    bcount += sprintf(&buffer[bcount],"[Result \"%s\"]\n",str);
    bcount += sprintf(&buffer[bcount],"[PlyCount \"%d\"]\n",hply);
    bcount += sprintf(&buffer[bcount],"[FEN \"%s\"]\n",HIST_STACK::start_fen);
    if((((hply % 2) + player) % 2)) {
        i = 1;
        bcount += sprintf(&buffer[bcount],"\n1... ");
    }
    for(;i < hply;i++) {
        if(i % 16 == 0) bcount += sprintf(&buffer[bcount],"\n");
        mov_san(hstack[i].move,mvstr);
        if(i % 2 == 0) bcount += sprintf(&buffer[bcount],"%d.",i/2 + 1);
        bcount += sprintf(&buffer[bcount],"%s ",mvstr);
    }
    bcount += sprintf(&buffer[bcount],"\n\n");

    if(fw) {
        l_lock(lock_io);
        fwrite(buffer, bcount, 1, fw);
        fflush(fw);
        l_unlock(lock_io);
    } else {
        print_log("%s\n",buffer);
    }
}
void SEARCHER::print_allmoves() {
    gen_all_legal();
    for(int i = 0;i < pstack->count; i++) {
        print_move(pstack->move_st[i]);
        print("\n");
    }
}
/*
* MCTS IO functions
*/
void Node::print_xml(Node* n,int depth) {
    char mvstr[32];
    mov_strx(n->move,mvstr);

    print_log("<node depth=\"%d\" move=\"%s\" alpha=\"%d\" beta=\"%d\" "
        "visits=\"%d\" policy=\"%.2f\" score=\"%.2f\" prior=\"%.2f\">\n",
        depth,mvstr,int(n->alpha),int(n->beta),unsigned(n->visits),
        n->policy,float(n->score),n->prior);

    Node* current = n->child;
    while(current) {
        print_xml(current,depth+1);
        current = current->next;
    }

    print_log("</node>\n");
}
void SEARCHER::extract_pv(Node* n, bool rand) {
    Node* best;
    if(rand)
        best = Node::Random_select(n,hply);
    else {
        bool has_ab_ = (n == root_node && has_ab);
        best = Node::Best_select(n, has_ab_);
    }
    if(best) {
        pstack->pv[ply] = best->move;
        pstack->pv_length = ply+1;
        ply++;
        extract_pv(best,false);
        best->set_pvmove();
        ply--;
    }
}
Node* Node::print_tree(Node* root,bool has_ab_, int max_depth,int depth) {
    char str[16];
    int total = 0;
    bool has_ab = (has_ab_ && depth == 0);
    Node* bnode = Node::Best_select(root, has_ab);
    Node* current = root->child;

    if(depth == 0) {
        if(has_ab)
            print_info("Move      (Q,P,Q+P)      Policy  Visits                  PV\n");
        else
            print_info("Move  Q   Policy  Visits                  PV\n");
        print_info("----------------------------------------------------------------------------------\n");
    }

    if(!current && depth > 0) {
        print("\n");
    }
    while(current) {
        if((depth == 0 || bnode == current) ) {
            mov_strx(current->move,str);
            if(depth == 0) {
                if(has_ab && (rollout_type == MCTS)) {
                    double uct = (1 - current->score);
                    double uctp = (1 - current->prior);
                    double avg = 0.5 * ((1 - frac_abprior) * uct 
                                        + frac_abprior * uctp + 
                                        MIN(uct,uctp));
                    print_info("%2d   (%.3f,%0.3f,%0.3f) %6.2f %7d   %s",
                        total+1,
                        uct,
                        uctp,
                        avg,
                        100*current->policy,
                        unsigned(current->visits),
                        str
                        );
                } else {
                    double uct = (rollout_type == MCTS) ? 
                            (1 - current->score) : logistic(-current->score);
                    print_info("%2d  %0.3f %6.2f %7d   %s",
                        total+1,
                        uct,
                        100*current->policy,
                        unsigned(current->visits),
                        str
                        );
                }
            } else
                print(" %s",str);
            print_tree(current,has_ab,max_depth,depth+1);
            total++;
        }

        current = current->next;
    }

    return bnode;
}
/*
Check if move is legal
*/
int SEARCHER::is_legal(MOVE& move) {
    if(m_promote(move)) {
        int prom = COMBINE(player,m_promote(move));
        move &= ~PROMOTION_FLAG;
        move |= (prom << 24);
    }
    /*generate root moves here*/
    const MOVE comp_flag = (variant == 1) ? FROM_TO_PROM_CAS : FROM_TO_PROM;
    pstack->count = 0;
    gen_all();
    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        if((move & comp_flag) == (pstack->current_move & comp_flag)) {
            do_move(pstack->current_move);
            if(attacks(player,plist[COMBINE(opponent,king)]->sq)) {
                undo_move();
                return false;
            }
            undo_move();
            move = pstack->current_move;
            return true;
        }
    }
    return false;
}
/*
Print principal variation (PV)
*/
void SEARCHER::print_pv(int score) {

    /*print pv*/
    MOVE  move;
    int i;
    char mv_str[10];
    char pv[1024];

    /*convert to correct mate score*/
    if(score > MATE_SCORE - WIN_PLY * MAX_PLY) 
        score = 10000 - (ceil(double(MATE_SCORE - score) / WIN_PLY));
    else if(score < -MATE_SCORE + WIN_PLY * MAX_PLY) 
        score = -10000 + (ceil(double(MATE_SCORE + score) / WIN_PLY));

    /*print what we have*/
    unsigned tm = (get_time() - start_time);
    uint64_t nds = (montecarlo && root_node) ? unsigned(playouts) : nodes;
    unsigned nps = 1000 * ((double)nds / tm);
    int depth = ((montecarlo && rollout_type == MCTS) ?
        ((Node::sum_tree_depth + 1) / (root_node->visits + 1)) :
        search_depth);
    int seld = (montecarlo ? Node::max_tree_depth : seldepth);
    int hashfull = ((!montecarlo && tm > 1000) ? PROCESSOR::hashfull() : 0);

    if(PROTOCOL == UCI) {
        sprintf(pv,"info depth %d seldepth %d score cp %d time %d "
            "nodes " FMT64 " nps %d hashfull %d tbhits %d pv",
            depth,
            seld,
            score,
            tm,
            (long long)nds,
            nps,
            hashfull,
            egbb_probes);
    } else {
        sprintf(pv,"%02d %d %d " FMT64 " %d %d %d %d",
            depth,
            score,
            tm/10,
            (long long)nds,
            seld,
            nps,
            hashfull,
            egbb_probes);
    }

    for(i = 0;i < stack[0].pv_length;i++) {
        move = stack[0].pv[i];
        strcpy(mv_str,"");
        mov_str(move,mv_str);
        strcat(pv," ");
        strcat(pv,mv_str);
        if(move) PUSH_MOVE(move);
        else PUSH_NULL();
    }
    /*add moves from hash table*/
    int dummy;
    while(ply < MAX_PLY - 1) {
        move = 0;
        if(!(montecarlo && frac_abprior == 0)) {
            PROBE_HASH(player,hash_key,0,0,dummy,dummy,move,
                -MATE_SCORE,MATE_SCORE,dummy,dummy,dummy,0);
        }
        if(!move || !is_legal_fast(move) || draw())
            break;

        stack[0].pv[i] = move;
        strcpy(mv_str,"");
        mov_str(move,mv_str);
        strcat(pv, " ");
        strcat(pv, mv_str);
        if(move) PUSH_MOVE(move);
        else PUSH_NULL();
        i++;
    }
    /*undo moves*/
    for (int j = 0; j < i ; j++) {
        move = hstack[hply - 1].move;
        if(move) POP_MOVE();
        else POP_NULL();
    }

    /*send pv to master node*/
    strcat(pv,"\n");
    if(pv_print_style == 0)
        print_cluster(pv);

#ifdef CLUSTER
    /*send PV moves to all nodes*/
    if(use_abdada_cluster && PROCESSOR::n_hosts > 1
        && search_depth > 8
        ) {

        PV_MESSAGE pv;
        pv.pv_length = stack[0].pv_length;
        memcpy(pv.pv, stack[0].pv, stack[0].pv_length * sizeof(MOVE));

        for(int i = 0;i < PROCESSOR::n_hosts;i++) {
            if(i != PROCESSOR::host_id)
                PROCESSOR::send_pv(i,pv);
        }
        PROCESSOR::node_pvs[PROCESSOR::host_id] = pv;
    }
#endif
}
/*
Check for repeatition inside tree and fifty move draws
*/
int SEARCHER::draw(int one_repeat) const {

    if(fifty >= 100) {
        if(fifty > 100) return true;
        if(!hstack[hply - 1].checks) return true;
    }

    if(hply >= 2) {
        int repeat = 0;
        for(int i = hply - 2;i >= hply - fifty;i -= 2) {
            if(hstack[i].hash_key == hash_key) {
                repeat++;
                if(repeat >= 2 || ply > 1 || one_repeat)
                    return true;
            }
        }
    }

    return false;
}
/*
Check if game is finished and print result
*/
int SEARCHER::print_result(bool output) {
    bool legal_move = false;

    pstack->count = 0;
    gen_all();
    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        do_move(pstack->current_move);
        if(attacks(player,plist[COMBINE(opponent,king)]->sq)) {
            undo_move();
            continue;
        }
        undo_move();
        legal_move = true;
        break;
    }

    if(!legal_move) {
        if(hstack[hply - 1].checks) {
            if(player == white) {
                if(output) print("0-1 {Black mates}\n");
                return R_BWIN;
            } else {
                if(output) print("1-0 {White mates}\n");
                return R_WWIN;
            }    
        } else {
            if(output) print("1/2-1/2 {Stalemate}\n");
            return R_DRAW;
        }      
    } else {
        int  repetition = 0;
        if(hply >= 2) {
            for(int i = hply - 2;i >= hply - fifty;i -= 2) {
                if(hstack[i].hash_key == hash_key)
                    repetition++;
            }
        }

        if(repetition >= 2) {
            if(output) print("1/2-1/2 {Draw by repetition}\n");
            return R_DRAW;
        } else if(fifty >= 100) { 
            if(output) print("1/2-1/2 {Draw by fifty move rule}\n");
            return R_DRAW;
        } else if(pawn_c[white] + pawn_c[black] == 0
            && piece_c[white] + piece_c[black] <= 3) {
                if(output) print("1/2-1/2 {Draw by insufficent material}\n");
                return R_DRAW;
        }    
    }
    if(hply >= MAX_HSTACK - 4) {
        if(output) print("1/2-1/2 {Draw by foolishness.}\n");
        return R_DRAW;
    }
    return R_UNKNOWN;
}  
/*
perft : search simulator for debuging purpose
*/
uint64_t SEARCHER::perft(int depth) {
    int stop_ply = ply;
    nodes = 0;
    pstack->depth = depth;
    pstack->gen_status = !ply ? GEN_RESET : GEN_START;

    gen_all_legal();

    while(true) {
        while(true) {
            if(!get_move()) {
                break;
            }

            PUSH_MOVE(pstack->current_move);

            pstack->depth = (pstack - 1)->depth - 1;
            pstack->gen_status = GEN_START;

            if(pstack->depth <= 0) {
                nodes++;
                break;
            }
            if(ply >= MAX_PLY - 1) {
                break;
            }
        }

        if(stop_ply == ply)
            break;
        POP_MOVE();
    }

    return nodes;
}
/*
Initialize board data
*/
void SEARCHER::init_data() {
    int i,sq,pic;

    ply = 0;
    pstack = stack + ply;
    pcsq_score[white].zero();
    pcsq_score[black].zero();
    piece_c[white] = 0;
    piece_c[black] = 0;
    pawn_c[white] = 0;
    pawn_c[black] = 0;
    memset(man_c,0,sizeof(man_c));
    all_man_c = 0;
    all_bb = 0;
    pieces_bb[white] = 0;
    pieces_bb[black] = 0;
    pawns_bb[white] = 0;
    pawns_bb[black] = 0;
    hash_key = 0;
    pawn_hash_key = 0;

    for(i = wking;i <= bpawn;i++)
        plist[i] = 0;
    for(sq = A1;sq <= H8;sq++) {
        if(!(sq & 0x88)) { 
            list[sq]->sq = sq;
            list[sq]->prev = 0;
            list[sq]->next = 0;
            pic = board[sq];
            if(pic != blank) {
                pcAdd(pic,sq);
                hash_key ^= PC_HKEY(pic,sq);
                if(PIECE(pic) == pawn) {
                    pawn_hash_key ^= PC_HKEY(pic,sq);
                    pawn_c[PCOLOR(pic)]++;
                    pawns_bb[PCOLOR(pic)] |= BB(sq);
                } else {
                    piece_c[PCOLOR(pic)]+=piece_cv[pic];
                    pieces_bb[PCOLOR(pic)] |= BB(sq);
                }
                all_bb |= BB(sq);
                pcsq_score[PCOLOR(pic)].add(pcsq[pic][sq],pcsq[pic][sq + 8]);
                man_c[pic]++;
                all_man_c++;
            }
        }
    }
    if(epsquare)
        hash_key ^= EP_HKEY(epsquare);
    hash_key ^= CAST_HKEY(castle);

    /*frc*/
    PLIST current;
    for(int i = 0; i < 6; i++)
        frc_squares[i] = -1;
    for(int side = 0; side < 2; side++) {
        frc_squares[3*side] = plist[COMBINE(side,king)]->sq;
        current = plist[COMBINE(side,rook)];
        while(current) {
            if(rank(current->sq) == rank(frc_squares[3*side])) {
                if(current->sq > frc_squares[3*side]) {
                    if((side == 0 && (castle & WSC_FLAG)) ||
                       (side == 1 && (castle & BSC_FLAG)))
                            frc_squares[3*side+1] = current->sq;
                } else {
                    if((side == 0 && (castle & WLC_FLAG)) ||
                       (side == 1 && (castle & BLC_FLAG)))
                            frc_squares[3*side+2] = current->sq;
                }
            }
            current = current->next;
        }
    }

    /*nnue*/
#ifdef NNUE_INC
    if(use_nnue)
        nnue[hply].accumulator.computedAccumulation = 0;
#endif
}
/*
Set board from FEN string
*/
void SEARCHER::set_board(const char* fen_str) {
    int i,r,f,sq,move_number;
    int ksq[2];

    strncpy(HIST_STACK::start_fen,fen_str,MAX_FEN_STR);

    for(sq = A1;sq <= H8;sq++) {
        if(!(sq & 0x88)) {
            board[sq] = blank;
        } else {
            sq += 0x07;
        }
    }

    const char* p = fen_str,*pfen;
    for(r = RANK8;r >= RANK1; r--) {
        for(f = FILEA;f <= FILEH;f++) {
            sq = SQ(r,f);
            if((pfen = strchr(piece_name,*p)) != 0) {
                board[sq] = int(strchr(piece_name,*pfen) - piece_name);
                if(*p == 'K')
                    ksq[0] = sq;
                else if(*p == 'k')
                    ksq[1] = sq;
            } else if((pfen = strchr(rank_name,*p)) != 0) {
                for(i = 0;i < pfen - rank_name;i++) {
                    board[sq + i] = blank;
                    f++;
                }
            } 
            p++;
        }
        p++;
    }

    /*player*/
    if((pfen = strchr(col_name,*p)) != 0)
        player = ((pfen - col_name) >= 2);
    opponent = invert(player);
    p++;
    p++;
    /*castling rights*/
    castle = 0;
    if(*p == '-') {
        p++;
    } else {
        if((*p >= 'A' && *p <= 'H') || (*p >= 'a' && *p <= 'h')) {
            while((pfen = strchr(cas_frc_name,*p)) != 0) {
                int idx = pfen - cas_frc_name;
                if(idx & 1) {
                    int rfrom = SQ(RANK8, idx >> 1);
                    if(ksq[1] < rfrom)
                        castle |= BSC_FLAG;
                    else
                        castle |= BLC_FLAG;
                } else {
                    int rfrom = SQ(RANK1, idx >> 1);
                    if(ksq[0] < rfrom)
                        castle |= WSC_FLAG;
                    else
                        castle |= WLC_FLAG;
                }
                p++;
            }
        } else {
            while((pfen = strchr(cas_name,*p)) != 0) {
                castle |= (1 << (pfen - cas_name));
                p++;
            }
        }
    }

    /*epsquare*/
    p++;
    if(*p == '-') {
        epsquare = 0;
        p++;
    } else {
        epsquare = int(strchr(file_name,*p) - file_name);
        p++;
        epsquare += 16 * int(strchr(rank_name,*p) - rank_name);
        p++;
    }

    /*fifty & hply*/
    p++;
    if(*p && *(p+1) && isdigit(*p) && ( isdigit(*(p+1)) || *(p+1) == ' ' ) ) {
        sscanf(p,"%d %d",&fifty,&move_number);
        if(move_number <= 0) move_number = 5;
    } else {
        fifty = 0;
        move_number = 5;
    }
    hply = 2 * (move_number - 1) + (player == black);

    /*zero history of moves*/
    for(int i = 0; i < hply; i++) {
        hstack[i].move = 0;
        hstack[i].hash_key = 0;
        hstack[i].checks = 0;
    }

    /*initialize other stuff*/
    init_data();    
}
/*
Convert a position in to FEN string
*/
void SEARCHER::get_fen(char* fen) const {
    int count;
    int f,r,sq;

    /*pieces*/
    for(r = 7; r >= 0 ; --r) {
        count = 0;
        for (f = 0; f <= 7 ; f++) {
            sq = SQ(r,f);
            if (board[sq]) {
                if(count){
                    *fen++ = rank_name[count-1];
                    count = 0;
                }
                *fen++ = piece_name[board[sq]];
            } else count++;
        }
        if(count) *fen++ = rank_name[count-1];
        if(r!=0) *fen++ = '/';
    }

    /*player*/
    *fen++ = ' ';
    if(player == white) *fen++ = 'w';
    else *fen++ = 'b';
    *fen++ = ' ';

    /*castling*/
    if(!castle) *fen++ = '-';
    else {
        if(castle & WSC_FLAG) *fen++ = 'K';
        if(castle & WLC_FLAG) *fen++ = 'Q';
        if(castle & BSC_FLAG) *fen++ = 'k';
        if(castle & BLC_FLAG) *fen++ = 'q';
    }
    *fen++ = ' ';

    /*enpassant*/
    if (!epsquare) *fen++ = '-';
    else {
        *fen++ = file_name[file(epsquare)];
        *fen++ = rank_name[rank(epsquare)];
    }

    *fen = 0;

    /*fifty & hply*/
    char str[32];
    int move_number = (hply - (player == black)) / 2 + 1;
    sprintf(str," %d %d",fifty,move_number);
    strcat(fen,str);
}
/*
new board
*/
void SEARCHER::new_board() {
    set_board(start_fen);
}
/*
get mirror image of board
*/
void SEARCHER::mirror() {
    int sq,sq1,temp;
    for (sq = 0;sq < 0x38; sq++) {
        if(!(sq & 0x88)) {
            sq1 = MIRRORR(sq);
            temp = board[sq1];
            board[sq1] = board[sq];
            board[sq] = temp;
            if(board[sq] != blank) {
                if(board[sq] >= bking) 
                    board[sq] = board[sq] - 6;
                else if(board[sq] >= wking) 
                    board[sq] = board[sq] + 6;
            }
            if(board[sq1] != blank) {
                if(board[sq1] >= bking) 
                    board[sq1] = board[sq1] - 6;
                else if(board[sq1] >= wking) 
                    board[sq1] = board[sq1] + 6;
            }
            list[sq]->sq = sq;
            list[sq1]->sq = sq1;
            list[sq]->prev = 0;
            list[sq]->next = 0;
            list[sq1]->prev = 0;
            list[sq1]->next = 0;
        }
    }
    player = invert(player);
    opponent = invert(opponent);

    if(epsquare)
        epsquare = SQ(7 - rank(epsquare) , file(epsquare));
    if(castle) {
        int t_castle = castle;
        castle = 0;
        if(t_castle & BSC_FLAG) castle |= WSC_FLAG;
        if(t_castle & BLC_FLAG) castle |= WLC_FLAG;
        if(t_castle & WSC_FLAG) castle |= BSC_FLAG;
        if(t_castle & WLC_FLAG) castle |= BLC_FLAG;
    }

    /*initialize other stuff*/
    init_data();
}
/*
SEARCHER constructor 
*/
SEARCHER::SEARCHER() : board(&temp_board[36])
{
    int sq;
    for (sq = 0;sq < 128; sq++) {
        list[sq] = temp_list + sq;
    }
    for(sq = 0;sq < 36;sq++)
        temp_board[sq] = elephant;
    for(sq = 164;sq < 192;sq++)
        temp_board[sq] = elephant;
    for(sq = A1;sq < A1 + 128;sq++) {
        if(sq & 0x88)
            board[sq] = elephant;
    }
    stop_searcher = 0;
    finish_search = false;
    skip_nn = false;
    master = 0;
    l_create(lock);
    used = false;
    n_workers = 0;
    for(int i = 0; i < MAX_CPUS;i++)
        workers[i] = NULL;
    processor_id = 0;
#ifdef CLUSTER
    host_workers.clear();
    n_host_workers = 0;
#endif
    root_node = 0;
    root_key = 0;
#ifdef NNUE_INC
    aligned_reserve<NNUEdata>(nnue,MAX_HSTACK);
#endif
}
/*
SEARCHER copier
*/
void SEARCHER::COPY(SEARCHER* srcSearcher) {
    int i,sq;
    for(sq = A1;sq <= H8; sq++) {
        if(!(sq & 0x88)) {
            board[sq] = srcSearcher->board[sq];
            list[sq]->sq = sq;
            if(srcSearcher->list[sq]->prev)
                list[sq]->prev = list[srcSearcher->list[sq]->prev->sq];
            else
                list[sq]->prev = 0;
            if(srcSearcher->list[sq]->next)
                list[sq]->next = list[srcSearcher->list[sq]->next->sq];
            else
                list[sq]->next = 0;
        } else {
            sq += 0x07;
        }
    }

    for(i = wking;i <= bpawn;i++) { 
        if(srcSearcher->plist[i])
            plist[i] = list[srcSearcher->plist[i]->sq];
        else
            plist[i] = 0;
    }

    player = srcSearcher->player;
    opponent = srcSearcher->opponent;
    castle = srcSearcher->castle;
    epsquare = srcSearcher->epsquare;
    fifty = srcSearcher->fifty;
    hply = srcSearcher->hply;
    ply = srcSearcher->ply;
    pstack = stack + ply;
    hash_key = srcSearcher->hash_key;
    pawn_hash_key = srcSearcher->pawn_hash_key;
    all_bb = srcSearcher->all_bb;
    pieces_bb[white] = srcSearcher->pieces_bb[white];
    pieces_bb[black] = srcSearcher->pieces_bb[black];
    pawns_bb[white] = srcSearcher->pawns_bb[white];
    pawns_bb[black] = srcSearcher->pawns_bb[black];
    pawn_c[white] = srcSearcher->pawn_c[white];
    pawn_c[black] = srcSearcher->pawn_c[black];
    piece_c[white] = srcSearcher->piece_c[white];
    piece_c[black] = srcSearcher->piece_c[black];
    pcsq_score[white] = srcSearcher->pcsq_score[white];
    pcsq_score[black] = srcSearcher->pcsq_score[black];
    memcpy(man_c,srcSearcher->man_c,sizeof(man_c));
    memcpy(frc_squares,srcSearcher->frc_squares,sizeof(frc_squares));
    all_man_c = srcSearcher->all_man_c;
    root_node = srcSearcher->root_node;
    root_key = srcSearcher->root_key;
#ifdef NNUE_INC
    if(use_nnue)
        memcpy(nnue, srcSearcher->nnue, (hply + 1) * sizeof(NNUEdata));
#endif
    /*history stack*/
    memcpy(&hstack[0],&srcSearcher->hstack[0], (hply + 1) * sizeof(HIST_STACK));

    PHIST_STACK dhstack,shstack;
    for(i = 0;i < (hply + 1);i++) {
        dhstack = &hstack[i];
        shstack = &srcSearcher->hstack[i];
        if(shstack->pCapt)
            dhstack->pCapt = list[shstack->pCapt->sq];
        if(shstack->pProm)
            dhstack->pProm = list[shstack->pProm->sq];
    }

    /*killers*/
    for(i = 0;i < MAX_PLY;i++) {
        stack[i].killer[0] = srcSearcher->stack[i].killer[0];
        stack[i].killer[1] = srcSearcher->stack[i].killer[1];
        stack[i].refutation = srcSearcher->stack[i].refutation;
    }

    /*stack copying: only important staff*/
    PSTACK dstack,sstack;
    for(i = 0;i < (ply + 1);i++) {
        dstack = &stack[i];
        sstack = &srcSearcher->stack[i];

        dstack->current_move = sstack->current_move;
        dstack->count = sstack->count;
        dstack->hash_move = sstack->hash_move;
        dstack->hash_flags = sstack->hash_flags;
        dstack->hash_depth = sstack->hash_depth;
        dstack->hash_score = sstack->hash_score;
        dstack->hash_eval = sstack->hash_eval;
        dstack->extension = sstack->extension;
        dstack->reduction = sstack->reduction;
        dstack->mate_threat = sstack->mate_threat;
        dstack->singular = sstack->singular;
        dstack->best_move = sstack->best_move;
        dstack->best_score = sstack->best_score;
        dstack->flag = sstack->flag;
        dstack->depth = sstack->depth;
        dstack->alpha = sstack->alpha;
        dstack->beta = sstack->beta;
        dstack->search_state = sstack->search_state;
        dstack->node_type = sstack->node_type;
        dstack->next_node_type = sstack->next_node_type;
        dstack->o_alpha = sstack->o_alpha;
        dstack->o_beta = sstack->o_beta;
        dstack->o_depth = sstack->o_depth;
        dstack->static_eval = sstack->static_eval;
        dstack->improving = sstack->improving;
    }
}
/*
time
*/
int get_time() {
#ifdef _WIN32
    timeb tb;
    ftime(&tb);
    return int(tb.time * 1000 + tb.millitm);
#else
    timeval tb;
    gettimeofday(&tb, NULL);
    return int(tb.tv_sec * 1000 + tb.tv_usec / 1000);
#endif
}
/*
log file
*/
static void init_log() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    int id = 0;
    CLUSTER_CODE(id = PROCESSOR::host_id);
    sprintf(log_name,"log/log-%d%02d%02d-%02d%02d%02d-%d.txt",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,id);
    if((log_file = fopen(log_name ,"w")) == 0)
        log_on = false;
    if(log_file)
        setbuf(log_file,NULL);

}
/*
input/output from pipe/consol
*/
#ifdef _WIN32
static HANDLE inh;
static int pipe;
#endif

void init_io() {

    /*unbuffered input/output*/
    setbuf(stdout,NULL);
    setbuf(stdin,NULL);
    signal(SIGINT,SIG_IGN);
    srand((unsigned)time(NULL) CLUSTER_CODE(+ PROCESSOR::host_id));
    rand();
    
    l_create(lock_io);
    init_log();

    /*pipe/consol*/
#ifdef _WIN32
    DWORD dw;
    inh = GetStdHandle(STD_INPUT_HANDLE);
    pipe = !GetConsoleMode(inh, &dw);
#endif

}

#ifdef _WIN32
#    include <conio.h>
int bios_key(void) {

#   ifdef FILE_CNT
    if (stdin->_cnt > 0)
        return stdin->_cnt;
#   endif

    if (pipe) {
        DWORD dw;
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL)) 
            return 1;
        return dw;
    } else {
        return _kbhit();
    }
}
#else
int bios_key(void) {
    fd_set readfds;
    struct timeval  timeout;

    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    select(16, &readfds, 0, 0, &timeout);

    return (FD_ISSET(fileno(stdin), &readfds));
}
#endif

bool read_line(char* buffer) {
    char* pbuffer = buffer;
#ifdef _WIN32
    if(!pipe && _kbhit()) {
        *pbuffer++ = (char)_getche();
        if(buffer[0] == '\r' || buffer[0] == '\n') return false;
    }
#endif
    if(fgets(pbuffer,4*MAX_FILE_STR,stdin)) {
        print_log("<%012d>",get_time() - scorpio_start_time);
        print_log(buffer);
        return true;
    }   
    return false;
}
/*
Set affinity and print number of physical/logical processors
*/
int set_affinity(int ncores) {
    int cores,active;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    cores = info.dwNumberOfProcessors;

    if(ncores) {
        DWORD_PTR dwProcessAffinity, dwSystemAffinity;
        GetProcessAffinityMask(GetCurrentProcess(), 
            &dwProcessAffinity, &dwSystemAffinity);

        DWORD_PTR mask = 0;
        int count = 0;
        for(int i = 0; i < cores && count < ncores; i+=2) {
            if(dwProcessAffinity & (DWORD_PTR(1) << i)) {
                mask |= (DWORD_PTR(1) << i);
                count++;
            }
        }
        for(int i = 1; i < cores && count < ncores; i+=2) {
            if(dwProcessAffinity & (DWORD_PTR(1) << i)) {
                mask |= (DWORD_PTR(1) << i);
                count++;
            }
        }
        SetProcessAffinityMask(GetCurrentProcess(), mask);
        active = count;
    } else
        active = cores;
#elif defined(__ANDROID__) || defined(__APPLE__)
    cores = sysconf(_SC_NPROCESSORS_ONLN);
    active = cores;
#else
    cores = sysconf(_SC_NPROCESSORS_ONLN);
    if(ncores) {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        int count = 0;
        for(int i = 0; i < cores && count < ncores; i+=2) {
            CPU_SET(i, &mask);
            count++;
        }
        for(int i = 1; i < cores && count < ncores; i+=2) {
            CPU_SET(i, &mask);
            count++;
        }
        sched_setaffinity(0, sizeof(cpu_set_t), &mask);
        active = CPU_COUNT(&mask);
    } else
        active = cores;
#endif
    print("Number of cores %d of %d\n",active,cores);
    return cores;
}
/*
get core and node process is running on
*/
void get_core_node(int& core, int& node) {
#ifdef _WIN32
    core = GetCurrentProcessorNumber();
    GetNumaProcessorNode(core,(UCHAR*)&node);
#else
#ifdef SYS_getcpu
    syscall(SYS_getcpu, &core, &node, NULL);
#endif
#endif
}
/*
Break down a string into tokens
*/
#ifdef _MSC_VER
# define strtok_r strtok_s
#endif
int tokenize(char *str, char** tokens, const char *str2) {
    int nu_tokens = 0;
    tokens[nu_tokens] = strtok_r(str, str2, &str);
    while (tokens[nu_tokens++] != NULL) {
        tokens[nu_tokens] = strtok_r(NULL, str2, &str);
    }
    return nu_tokens;
}
#ifdef _MSC_VER
# undef strtok_r
#endif
/*
Chess Clock
*/
CHESS_CLOCK::CHESS_CLOCK() {
    mps = 10;
    p_inc = 0;
    o_inc = 0;
    p_time = 60000;
    o_time = 60000;
    max_st = MAX_NUMBER;
    max_sd = MAX_PLY;
    max_visits = MAX_NUMBER;
    infinite_mode = 0;
    pondering = 0;
}
bool CHESS_CLOCK::is_timed() {
    return !infinite_mode && (max_st == MAX_NUMBER) && (max_sd == MAX_PLY);
}
void CHESS_CLOCK::set_stime(int hply, bool output) {

    output = output && (SEARCHER::pv_print_style == 0);

    /*fixed time/depth*/
    if(max_st != MAX_NUMBER) {
        search_time = max_st;
        maximum_time = max_st;
        if(output)
            print_info("[st = %dms, mt = %dms , hply = %d]\n",search_time,maximum_time,hply);
        return;
    }
    if(max_sd != MAX_PLY) {
        search_time = MAX_NUMBER;
        maximum_time = MAX_NUMBER;
        if(output)
            print_info("[sd = %d , hply = %d]\n",max_sd,hply);
        return;
    }
    if(max_visits != MAX_NUMBER) {
        search_time = MAX_NUMBER;
        maximum_time = MAX_NUMBER;
        if(output)
            print_info("[sv = %d , hply = %d]\n",max_visits,hply);
        return;
    }

    /*
    set search time and maximum time allowed
    */
    int moves_left, est_moves_left;
    int move_no = (hply / 2);
    int pp_time = p_time;
    if(p_time > 5000) p_time -= 1500;
    else p_time = int(0.7 * p_time);
    if(pondering) p_time /= 4;

    if(move_no <= 35)
        est_moves_left = 50 - move_no;
    else
        est_moves_left = 15;

    if(!mps) {
        moves_left = est_moves_left;
        search_time = p_time / moves_left + p_inc;
        maximum_time = p_time / 2;
    } else {
        if(PROTOCOL == UCI)
            moves_left = mps;
        else
            moves_left = mps - (move_no % mps);
        if(moves_left > est_moves_left)
            moves_left = est_moves_left;

        search_time = p_time / moves_left;
        if(moves_left != 1)
            search_time = int(0.95 * search_time);

        if(moves_left == 1) {
            maximum_time = p_time;
        } else if(moves_left == 2) {
            maximum_time = (3 * p_time) / 4;
        } else if(moves_left == 3) {
            maximum_time = (2 * p_time) / 3;
        } else if(moves_left <= 8) {
            maximum_time = p_time / 4;
        } else {
            maximum_time = p_time / 2;
        }
    }
    maximum_time -= 30;

    if(SEARCHER::first_search)
        search_time = 1.5 * search_time;

    if(montecarlo)
        search_time = 1.3 * search_time;

    int delta = (p_time - o_time) * 2 / moves_left;
    if(o_time < p_time)
        search_time += delta;
    else {
        delta = -delta / 2;
        if(delta > search_time / 2)
            delta = search_time / 2;
        search_time -= delta;
    }

    p_time = pp_time;

    /*
    print time
    */
    if(output)
        print_info("[st = %dms, mt = %dms , hply = %d , moves_left %d]\n",
        search_time,maximum_time,hply,moves_left);
}
void SEARCHER::check_quit() {
    int time_used = get_time() - start_time;

    /*
    poll nodes
    */
    if(time_used) {
        poll_nodes = int(int64_t(nodes) / (time_used / 100.0f));
        poll_nodes /= PROCESSOR::n_processors;
        poll_nodes = MAX(poll_nodes, 5000);
        poll_nodes = MIN(poll_nodes, 100000);
    }

    /*
    check input
    */
    if(bios_key()) {

        /*process commands*/
        char*  commands[MAX_STR];
        do {
            static char buffer[4*MAX_FILE_STR];
            if(!read_line(buffer))
                break;
            commands[tokenize(buffer,commands)] = NULL;
            if(!parse_commands(commands)) {
                abort_search = 1;
                return;
            }

        } while (bios_key());
    }

    /*fixed depth/nodes*/
    if(chess_clock.max_sd != MAX_PLY ||
       chess_clock.max_visits != MAX_NUMBER)
        return;

    /*infinite search mode?*/
    if(chess_clock.infinite_mode)
        return;

    /*avoid exceeding maximum time buffer*/
    if(time_used >= chess_clock.maximum_time) {
        abort_search = 1;
        return;
    }

    /*don't stop if fail low @ root is not resolved*/
    if(root_failed_low)
        return;

    /*root unstable*/
    if(time_used < time_factor * chess_clock.search_time)
        return;

    /*avoid exceeding allocated search time*/
    if(time_used >= chess_clock.search_time) {
        abort_search = 1;
        return;
    }
}
/*
BOOK
*/
typedef struct BOOK_E {
    HASHKEY hash_key;
    uint16_t wins;
    uint16_t losses;
    uint16_t draws;
    uint16_t learn;
    BOOK_E() {
        wins = 0;
        losses = 0;
        draws = 0;
        learn = 100;
        hash_key = 0;
    }
}*PBOOK_E;

typedef struct BOOK_MOVE {
    BOOK_E book_e;
    MOVE move;
    int played;
}*PBOOK_MOVE;

static FILE* book_file;
static int w_positions;
static int b_positions;

/*
Assume little endian byte order
*/
static uint64_t read_bytes(int count,FILE* f) {
    uint64_t x = 0;
    uint8_t* c = (uint8_t*) &x;
    for(int i = 0; i < count; i++) {
        c[i] = ((uint8_t) fgetc(f));
    }
    return x;
}

static void read_entry(PBOOK_E pbook_e,int middle,FILE* f = book_file) {

    fseek(f,8 + middle * 16,SEEK_SET);

    pbook_e->hash_key = (uint64_t) read_bytes(8,f);
    pbook_e->wins = (uint16_t) read_bytes(2,f);
    pbook_e->losses = (uint16_t) read_bytes(2,f);
    pbook_e->draws = (uint16_t) read_bytes(2,f);
    pbook_e->learn = (uint16_t) read_bytes(2,f);
}

void load_book() {
    book_file = fopen("book.dat","rb");
    if(!book_file) {
        book_loaded = false;
        return;
    }
    w_positions = (int) read_bytes(4,book_file);
    b_positions = (int) read_bytes(4,book_file);
    book_loaded = true;
}

static int get_book_pos(HASHKEY hash_key,int color,BOOK_E& r_book_e) { 
    int lower, middle, upper;
    BOOK_E book_e;

    if(color == white) {
        lower = 0;
        upper = w_positions;
    } else {
        lower = w_positions;
        upper = lower + b_positions;
    }
    while ( upper >= lower  ) {
        middle = ( upper + lower ) / 2;
        read_entry(&book_e,middle);
        if(book_e.hash_key == hash_key) { 
            r_book_e = book_e;
            return true;
        } else if(book_e.hash_key < hash_key) {
            lower = middle + 1;
        } else {
            upper = middle - 1;
        }
    };
    return false;
} 

MOVE SEARCHER::get_book_move() {
    int i,j,count,side = player;
    int played,total_played = 0,score,best_score;
    MOVE best_move;
    PBOOK_E pbook_e; 
    BOOK_MOVE book_moves[MAX_MOVES],temp;
    char mv_str[10];

    memset(&book_moves,0,sizeof(book_moves));
    count = pstack->count;

    for(i = 0;i < count; i++) {
        pbook_e = &book_moves[i].book_e;
        book_moves[i].move = pstack->move_st[i];
        PUSH_MOVE(pstack->move_st[i]);
        if(!draw() && get_book_pos(hash_key,side,*pbook_e)) {
            book_moves[i].played = pbook_e->wins + pbook_e->losses + pbook_e->draws;
            total_played += book_moves[i].played;
        } else {
            book_moves[i].played = 0;
        }
        POP_MOVE();
    }

    for(i = 0;i < pstack->count; i++) {
        for(j = i + 1;j < pstack->count; j++) {
            if(book_moves[j].played > book_moves[i].played) {
                temp = book_moves[i];
                book_moves[i] = book_moves[j];
                book_moves[j] = temp;
            }
        }
    }

    if(pv_print_style == 0) {
        if(total_played)
            print("  Move   Played     +        -        =     Learn  Sortv\n");
    }

    best_score = 0;
    best_move = 0;
    for(i = 0;i < pstack->count; i++) {
        pbook_e = &book_moves[i].book_e;
        if(pbook_e->hash_key) {
            played = pbook_e->wins + pbook_e->losses + pbook_e->draws;
            score = 50 * (2 * pbook_e->wins + pbook_e->draws) / played;
            score = int(score * (float(played)/total_played) * (pbook_e->learn / 100.0f));
            if(score) {
                score += ((15 * rand()) / RAND_MAX);
                if(score > best_score) {
                    best_score = score;
                    best_move = book_moves[i].move;
                }
            }
            if(pv_print_style == 0) {
                mov_str(book_moves[i].move,mv_str);
                print("%6s %8d %7.2f%% %7.2f%% %7.2f%% %6d %6d\n",mv_str,played,
                    100 * pbook_e->wins / float(played),100 * pbook_e->losses / float(played),
                    100 * pbook_e->draws / float(played),pbook_e->learn,score);
            }
        }
    }
    return best_move;
}

void SEARCHER::show_book_moves() {
    gen_all_legal();
    get_book_move();
}

/*
SAN
*/
bool SEARCHER::san_mov(MOVE& move,char* s) {
    char* c;
    int piece = pawn,promote = blank,f_file = -1,f_rank = -1,from = 0, to,len;
    if((c = strchr(s,'+')) != 0) *c = 0;
    else if((c = strchr(s,'#')) != 0) *c = 0;
    else if((c = strchr(s,'?')) != 0) *c = 0;
    else if((c = strchr(s,'!')) != 0) *c = 0;

    if(!strcmp(s,"o-o") || !strcmp(s,"O-O") || !strcmp(s,"0-0")) {
        if(player == white) {piece = king; from = E1;to = G1;}
        else {piece = king; from = E8;to = G8;}
        goto END;
    } else if(!strcmp(s,"o-o-o") || !strcmp(s,"O-O-O") || !strcmp(s,"0-0-0")) {
        if(player == white) {piece = king; from = E1;to = C1;}
        else {piece = king; from = E8;to = C8;}
        goto END;
    } else if ((c = strchr(s,'=')) != 0) {
        promote = PIECE(strchr(piece_name,*(++c)) - piece_name);
        *(--c) = 0;
    }
    len = (int) strlen(s);
    to = SQ(s[len - 1]-'1', s[len - 2]-'a'); 
    len -= 2;
    s[len] = 0;
    if(!len) goto END;
    if(s[len - 1] == 'x') s[--len] = 0;
    if(!len) goto END;
    if((s[len - 1] >= '1') && (s[len - 1] <= '8')) { f_rank = (s[len - 1]-'1'); s[--len] = 0; }
    if((s[len - 1] >= 'a') && (s[len - 1] <= 'h')) { f_file = (s[len - 1]-'a'); s[--len] = 0; }
    if(!len) goto END;
    c = (char*) strchr(piece_name,s[len - 1]);
    if(c) piece = PIECE(c - piece_name);

END:
    /*generate moves*/
    MOVE cmove;
    pstack->count = 0;
    gen_all();
    for(int i = 0;i < pstack->count; i++) {
        pstack->current_move = pstack->move_st[i];
        cmove = pstack->current_move;
        if(to == m_to(cmove)) {
            if(piece != PIECE(m_piece(cmove))) continue;
            if(promote && promote != PIECE(m_promote(cmove))) continue;
            if(is_castle(cmove) && from != m_from(m_from(cmove))) continue;
            if(f_file != -1 && f_file != file(m_from(cmove))) continue;
            if(f_rank != -1 && f_rank != rank(m_from(cmove))) continue;

            do_move(cmove);
            if(attacks(player,plist[COMBINE(opponent,king)]->sq)) {
                undo_move();
                continue;
            }
            undo_move();
            move = cmove;
            return true;
        }
    }
    return false;
}
static int compare(const void * a, const void * b) {
    HASHKEY k1 = ((BOOK_E*)a)->hash_key;
    HASHKEY k2 = ((BOOK_E*)b)->hash_key;
    if(k1 > k2) return 1;
    else if(k1 < k2) return -1;
    else return 0;
}
bool SEARCHER::build_book(char* path,char* book,int BOOK_SIZE,int BOOK_DEPTH,int color) {
    FILE* f = fopen(path,"rt");
    if(!f) return false;

    BOOK_E* entries[2];
    int    n_entries[2];
    char   buffer[MAX_FILE_STR];
    char   *commands[MAX_STR],*command,*c;
    int    i,result = 0,command_num;
    int    comment = 0,line = 0,game = 0;
    uint16_t weight;
    MOVE   move;
    char   fen[MAX_FEN_STR];
    char* pc;
    bool illegal = false;

    for(i = 0;i < 2;i++) {
        entries[i] = new BOOK_E[BOOK_SIZE];
        n_entries[i] = 0;
    }

    while(fgets(buffer,MAX_FILE_STR,f)) {
        line++;
        if(buffer[0] == '[' && !comment) {
            if(strncmp(buffer + 1, "Result ",7) == 0) {
                if(!strncmp(buffer + 9,"1-0",3)) result = R_WWIN;
                else if(!strncmp(buffer + 9,"0-1",3)) result = R_BWIN;
                else if(!strncmp(buffer + 9,"1/2-1/2",7)) result = R_DRAW;
                else result = R_UNKNOWN;
                game++;
                print("Game %d\t\r",game);
                new_board();
                illegal = false;

            } else if(strncmp(buffer + 1, "FEN ",4) == 0) {
                buffer[(strlen(buffer)-1-2)]=0;
                strcpy(fen,buffer+6);
                set_board(fen);
            }
            continue;
        }
        if(illegal) continue;
        if(isspace(buffer[0])) continue;

        commands[tokenize(buffer,commands," \n\r\t")] = NULL;
        command_num = 0;
        while((command = commands[command_num++]) != 0) {
            if(strchr(command,'{')) comment++;
            if(strchr(command,'}')) comment--;
            else if(comment == 0) {
                if((pc = strchr(command,'.')) != 0) {
                    if(*(pc+1) == ' ' || *(pc+1) == 0 || *(pc+1) == '.') continue;
                    else command = pc + 1;
                }
                if(strchr(command,'*')) continue;
                if(strchr(command,'-') && strchr(command,'1')) continue;
                /*move weight*/
                weight = 1;
                if((c = strchr(command,'?')) != 0) {
                    *c = 0;
                    weight >>= 2;
                    while(*(++c) == '?') weight >>= 2;
                }
                if((c = strchr(command,'!')) != 0) {
                    *c = 0;
                    weight <<= 2;
                    while(*(++c) == '!') weight <<= 2;
                }
                /*SAN move*/
                if(!san_mov(move,command)) {
                    print("Incorrect move %s at game %d line %d\n",command,game,line);
                    print_board();
                    illegal = true;
                    break;
                }
                do_move(move);
                ply++;

                player = invert(player);
                if(ply < BOOK_DEPTH && result != R_UNKNOWN && player != color) {
                    for(i = 0;i < n_entries[player];i++) {
                        if(entries[player][i].hash_key == hash_key)
                            break;
                    }
                    if(i == n_entries[player]) {
                        if(i < BOOK_SIZE) {
                            n_entries[player]++;
                            entries[player][i].hash_key = hash_key;
                        } else {
                            print("Book buffer overflow\n");
                            return false;
                        }
                    }
                    switch(result) {
                    case R_WWIN: if(player == white) entries[player][i].wins++; 
                                 else entries[player][i].losses++; 
                                 break;
                    case R_BWIN: if(player == black) entries[player][i].wins++; 
                                 else entries[player][i].losses++; 
                                 break;
                    case R_DRAW: entries[player][i].draws++; 
                        break;
                    }
                    entries[player][i].learn *= weight;
                }
                player = invert(player);
            }
        }
    }

    fclose(f);

    f = fopen(book,"wb");
    print("\nw_positions %d\nb_positions %d\n",n_entries[white],n_entries[black]);
    print("Sorting...\n");
    qsort(entries[white],n_entries[white],sizeof(BOOK_E),compare);
    qsort(entries[black],n_entries[black],sizeof(BOOK_E),compare);
    print("Writing...\n");
    fwrite(&n_entries[white],sizeof(int),1,f);
    fwrite(&n_entries[black],sizeof(int),1,f);
    fwrite(entries[white],sizeof(BOOK_E),n_entries[white],f);
    fwrite(entries[black],sizeof(BOOK_E),n_entries[black],f);
    print("Finished\n");
    fclose(f);

    for(i = 0;i < 2;i++) 
        delete[] entries[i];

    return true;
}
void merge_books(char* path1,char* path2,char* path,double w1 = 1.0,double w2 = 1.0) {
    FILE* f1 = fopen(path1,"rb");
    FILE* f2 = fopen(path2,"rb");
    FILE* f = fopen(path,"wb");
    if(!f1 || !f2 || !f) return;

    BOOK_E entry[2];
    int c1[2],c2[2],c[2]={0},index[2],end[2],result;

    print("Merging...\n");

    fread(c1,sizeof(int),2,f1);
    fread(c2,sizeof(int),2,f2);
    fwrite(c,sizeof(int),2,f);

    for(int i = 0;i < 2;i++) {
        entry[0].hash_key = 0;
        entry[1].hash_key = 0;
        if(i == 0) {
            c[0] = 0;
            c[1] = 0;
            index[0] = 0;
            index[1] = 0;
            end[0] = c1[0];
            end[1] = c2[0];
        } else {
            index[0] = c1[0];
            index[1] = c2[0];
            end[0] = c1[0] + c1[1];
            end[1] = c2[0] + c2[1];
        }
        result = 0;
        while(true) {
            if(index[0] < end[0] && (result <= 0 || index[1] == end[1])) { 
                read_entry(&entry[0],index[0]++, f1);
                entry[0].learn = uint16_t(entry[0].learn * w1);
            }
            if(index[1] < end[1] && (result >= 0 || index[0] == end[0])) {
                read_entry(&entry[1],index[1]++, f2);
                entry[1].learn = uint16_t(entry[1].learn * w2);
            }
            result = compare(&entry[0],&entry[1]);

            if(result == 0) {
                entry[0].wins += entry[1].wins;
                entry[0].losses += entry[1].losses;
                entry[0].draws += entry[1].draws;
                entry[0].learn = uint16_t(entry[0].learn * w1 + entry[1].learn * w2);
                fwrite(&entry[0],sizeof(BOOK_E),1,f); c[i]++;
            } else {
                if((result < 0 || index[1] == end[1]) && entry[0].hash_key) {
                    fwrite(&entry[0],sizeof(BOOK_E),1,f); c[i]++;
                }
                if((result > 0 || index[0] == end[0]) && entry[1].hash_key) {
                    fwrite(&entry[1],sizeof(BOOK_E),1,f); c[i]++;
                } 
            }
            if(index[0] == end[0] && index[1] == end[1])
                break;
        }

    }
    rewind(f);
    fwrite(c,sizeof(int),2,f);
    print("%d %d positions from %s\n",c1[0],c1[1],path1);
    print("%d %d positions from %s\n",c2[0],c2[1],path2);
    print("%d %d positions  to  %s\n",c[0],c[1],path);
    print("Finished\n");

    fclose(f);
    fclose(f1);
    fclose(f2);
}

/*
Process PGN/EPD in parallel
*/
bool ParallelFile::open(const char* path, bool mem) {
    f = fopen(path,"r");
    if(!f)
        return false;
#if !defined(_WIN32) && !defined(__ANDROID__)
    if(mem) {
        print("Started loading file: %s\n", path);
        fseek(f, 0L, SEEK_END);
        long numbytes = ftell(f);
        fseek(f, 0L, SEEK_SET);
        print("Loading file of size %.2f MB ...\n",
            double(numbytes)/(1024*1024));
        memmap_file = (char*)malloc(numbytes);
        fread(memmap_file, sizeof(char), numbytes, f);
        print("Finished Loading file.\n");
        fclose(f);

        f = fmemopen(memmap_file, strlen(memmap_file), "r");
    } else
#endif
    {
        memmap_file = 0;
    }

    l_create(lock);
    count = 0;
    return true;
}
void ParallelFile::close() {
    fclose(f);
#if !defined(_WIN32) && !defined(__ANDROID__)
    if(memmap_file) {
        free(memmap_file);
        memmap_file = 0;
        print("Unloaded file!\n");
    }
#endif
}
void ParallelFile::rewind() {
    ::rewind(f);
}
bool PGN::next(char* moves, bool silent) {

    l_lock(lock);

    char buffer[4 * MAX_FILE_STR];
    bool is_header = true;
    strcpy(moves, "");
    while(fgets(buffer,4 * MAX_FILE_STR,f)) {
        strcat(moves, buffer);

        /*peek next char*/
        int c = fgetc(f);
        ungetc(c, f);
        if(c == '[') {
            if(!is_header) {
                count++;
                if(!silent && (SEARCHER::pv_print_style == 0))
                    print("Game %d\t\r",count);
                l_unlock(lock);
                return true;
            }
        } else
            is_header = false;
    }

    l_unlock(lock);

    return false;
}
bool EPD::next(char* moves, bool silent) {

    l_lock(lock);
    if(fgets(moves,4 * MAX_FILE_STR,f)) {
        count++;
        if(!silent && (SEARCHER::pv_print_style == 0))
            print("Position %d\t\r",count);
        l_unlock(lock);
        return true;
    }
    l_unlock(lock);
    return false;
}
/*
PGN to epd
*/
void SEARCHER::pgn_to_epd(char* pgn, FILE* fb, int task) {

    char   buffer[32 * MAX_FILE_STR];
    char   *commands[32 * MAX_STR],*command;
    int    result = R_UNKNOWN,command_num;
    int    comment = 0,line = 0;
    MOVE   move;
    char   fen[MAX_FEN_STR];
    char* pc;
    bool illegal = false, has_score = false;
    float score;
    int   bestm;
    int   moves[MAX_MOVES];
    float probs[MAX_MOVES];
    float scores[MAX_MOVES];

#define TASK() {                                                \
    if(task == 0) {                                             \
        get_fen(fen);                                           \
        if(result == R_WWIN) strcat(fen," 1-0");                \
        else if(result == R_BWIN) strcat(fen," 0-1");           \
        else strcat(fen," 1/2-1/2");                            \
        l_lock(lock_io);                                        \
        fprintf(fb,"%s\n", fen);                                \
        l_unlock(lock_io);                                      \
    } else if(task == 1) {                                      \
        get_fen(fen);                                           \
        if(result == R_WWIN) strcat(fen," 1-0");                \
        else if(result == R_BWIN) strcat(fen," 0-1");           \
        else strcat(fen," 1/2-1/2");                            \
        if(!has_score) score = eval(true);                      \
        if(player == black) score = -score;                     \
        score = logistic(score);                                \
        l_lock(lock_io);                                        \
        fprintf(fb,"%s %f\n", fen, score);                      \
        l_unlock(lock_io);                                      \
    } else if(task == 2) {                                      \
        get_fen(fen);                                           \
        if(result == R_WWIN) strcat(fen," 1-0");                \
        else if(result == R_BWIN) strcat(fen," 0-1");           \
        else strcat(fen," 1/2-1/2");                            \
        int nmoves;                                             \
        generate_and_score_moves(-MATE_SCORE, MATE_SCORE);      \
        manage_tree(true);                                      \
        SEARCHER::egbb_ply_limit = 8;                           \
        pstack->depth = search_depth;                           \
        search_mc(true);                                        \
        get_train_data(score,nmoves,moves,probs,scores,bestm);  \
        l_lock(lock_io);                                        \
        fprintf(fb,"%s %f %d ", fen, score, nmoves);            \
        for(int i = 0; i < nmoves; i++) {                       \
            if(train_data_type == 2)                            \
                fprintf(fb, "%d %f %f ",                        \
                    moves[i],probs[i],scores[i]);               \
            else if(train_data_type == 1)                       \
                fprintf(fb, "%d %f ",moves[i],scores[i]);       \
            else                                                \
                fprintf(fb, "%d %f ",moves[i],probs[i]);        \
        }                                                       \
        fprintf(fb, "%d", bestm);                               \
        fprintf(fb,"\n");                                       \
        l_unlock(lock_io);                                      \
    } else if(task == 3) {                                      \
        l_lock(lock_io);                                        \
        write_input_planes(fb);                                 \
        l_unlock(lock_io);                                      \
    }                                                           \
}

    std::stringstream iss;
    iss << pgn;
    std::string bufferl;
    while( std::getline(iss,bufferl) ) {

        strcpy(buffer, bufferl.c_str());

        line++;

        if(buffer[0] == '[' && !comment) {
            if(strncmp(buffer + 1, "Result ",7) == 0) {
                if(!strncmp(buffer + 9,"1-0",3)) result = R_WWIN;
                else if(!strncmp(buffer + 9,"0-1",3)) result = R_BWIN;
                else if(!strncmp(buffer + 9,"1/2-1/2",7)) result = R_DRAW;
                else result = R_UNKNOWN;

                new_board();
                illegal = false;

            } else if(strncmp(buffer + 1, "FEN ",4) == 0) {
                buffer[(strlen(buffer)-2)]=0;
                strcpy(fen,buffer+6);
                set_board(fen);
            }
            continue;
        }
        if(illegal) continue;
        if(isspace(buffer[0])) continue;

        commands[tokenize(buffer,commands," \n\r\t")] = NULL;
        command_num = 0;

        while((command = commands[command_num++]) != 0) {

            has_score = false;
            if(strchr(command,'{')) comment++;
            else if(strchr(command,'}')) comment--;
            else if(comment == 1) {
                if(!strcmp(command,"[%%eval")) {
                    command = commands[command_num++];
                    double score;
                    if(command[0] == '#') {
                        if(command[1] == '-') score = -MATE_SCORE;
                        else score = MATE_SCORE;
                    } else {
                        score = 100 * atof(command);
                    }
                    has_score = true;
                }
            } else if(comment == 0) {
                if((pc = strchr(command,'.')) != 0) {
                    if(*(pc+1) == ' ' || *(pc+1) == 0 || *(pc+1) == '.') continue;
                    else command = pc + 1;
                }
                if(strchr(command,'*')) {
                    if(result == R_UNKNOWN) {
                        new_board();
                        illegal = false;
                        result = R_UNKNOWN;
                    }
                    break;
                }
                if(strchr(command,'-') && strchr(command,'1')) {

                    if(result == R_UNKNOWN) {
                        if(!strncmp(command,"1-0",3)) result = R_WWIN;
                        else if(!strncmp(command,"0-1",3)) result = R_BWIN;
                        else if(!strncmp(command,"1/2-1/2",7)) result = R_DRAW;

                        int shply = hply;
                        while(hply > 0) undo_move();
                        for(int i = 0; i < shply; i++) {
                            MOVE& move = hstack[hply].move;
                            TASK();
                            do_move(move);
                        }

                        new_board();
                        illegal = false;
                        result = R_UNKNOWN;
                        break;
                    }

                    continue;
                }

                /*SAN move*/
                if(!san_mov(move,command)) {
                    print("Incorrect move %s at line %d\n",command,line);
                    print_board();
                    illegal = true;
                    break;
                }

                /*Write fen with score and best move*/
                if(result != R_UNKNOWN) {
                    TASK();
                }

                /*make move*/
                do_move(move);
            }
        }

    }

#undef TASK

}
/*
EPD to nn
*/
void SEARCHER::epd_to_nn(char* fen, FILE* fb, int task) {

    float score;
    int   bestm;
    int   moves[MAX_MOVES];
    float probs[MAX_MOVES];
    float scores[MAX_MOVES];
    char  epd[4*MAX_FEN_STR];

#define TASK() {                                                \
    if(task == 0) {                                             \
        score = eval();                                         \
        if(player == black) score = -score;                     \
        score = logistic(score);                                \
        l_lock(lock_io);                                        \
        fprintf(fb,"%s %f\n", fen, score);                      \
        l_unlock(lock_io);                                      \
    } else if(task == 1) {                                      \
        int nmoves;                                             \
        generate_and_score_moves(-MATE_SCORE, MATE_SCORE);      \
        manage_tree(true);                                      \
        SEARCHER::egbb_ply_limit = 8;                           \
        pstack->depth = search_depth;                           \
        search_mc(true);                                        \
        get_train_data(score,nmoves,moves,probs,scores,bestm);  \
        l_lock(lock_io);                                        \
        fprintf(fb,"%s %f %d ", fen, score, nmoves);            \
        for(int i = 0; i < nmoves; i++) {                       \
            if(train_data_type == 2)                            \
                fprintf(fb, "%d %f %f ",                        \
                    moves[i],probs[i],scores[i]);               \
            else if(train_data_type == 1)                       \
                fprintf(fb, "%d %f ",moves[i],scores[i]);       \
            else                                                \
                fprintf(fb, "%d %f ",moves[i],probs[i]);        \
        }                                                       \
        fprintf(fb, "%d",bestm);                                \
        fprintf(fb,"\n");                                       \
        l_unlock(lock_io);                                      \
    } else if(task == 2) {                                      \
        l_lock(lock_io);                                        \
        write_input_planes(fb);                                 \
        l_unlock(lock_io);                                      \
    } else if(task == 3) {                                      \
        char *p = strrchr(epd, ' ');                            \
        if(p && *(p + 1)) {                                     \
            MOVE move;                                          \
            str_mov(move,p + 1);                                \
            *p = 0;                                             \
            int pksq = plist[COMBINE(player,king)]->sq;         \
            if(is_legal(move)                                   \
                && !is_cap_prom(move)                           \
                && !attacks(opponent,pksq)                      \
            ) {                                                 \
                l_lock(lock_io);                                \
                fprintf(fb,"%s\n", epd);                        \
                l_unlock(lock_io);                              \
            }                                                   \
        }                                                       \
    } else if(task == 4) {                                      \
        int sc = eval();                                        \
        mirror();                                               \
        int sce = eval();                                       \
        if(sc == sce);                                          \
        else {                                                  \
            print("*****WRONG RESULT*****\n");                  \
            print("[ %s ] \nsc = %6d sc1 = %6d\n",fen,sc,sce);  \
            print("**********************\n");                  \
        }                                                       \
    } else if(task == 5) {                                      \
        SEARCHER::first_search = true;                          \
        SEARCHER::old_root_score = 0;                           \
        SEARCHER::root_score = 0;                               \
        SEARCHER cp;                                            \
        cp.COPY(this);                                          \
        find_best();                                            \
        cp.copy_root(this);                                     \
        COPY(&cp);                                              \
        if(SEARCHER::pv_print_style == 0)                       \
            print("**********************\n");                  \
    } else if(task == 6) {                                      \
        char word[32] = {0};                                    \
        print("%s",epd);                                        \
        for(int d = 1; d <= 5; d++) {                           \
            perft(d);                                           \
            print("D%d " FMT64 "\n",d,nodes);                   \
            sprintf(word,"" FMT64 "",(long long)(nodes));       \
        }                                                       \
        if(strstr(epd, word) == 0)                              \
            print("Error\n");                                   \
    }                                                           \
}

    strcpy(epd, fen);
    fen[strlen(fen) - 1] = 0;
    set_board(fen);
    get_fen(fen);

    TASK();

#undef TASK

}
/*
pseudo random numbers generated using rand();
*/
CACHE_ALIGN const HASHKEY piece_hkey[14][64] = {
        //blank
    {
        UINT64(0x888450823e5c9d6d),   UINT64(0x857ba5ef038c054c),   UINT64(0x45ea2d0f2cb9015f),   UINT64(0xdc97eb7e2fef8e21),   UINT64(0xa1f239a0b898b793),   UINT64(0x129b3fe9d3fcd504),   UINT64(0xfc3b0f3bf7620267),   UINT64(0x7b77b6b2dba2b499),   
        UINT64(0x3ed0b05c35a4f9df),   UINT64(0x9cbacde75a48f20a),   UINT64(0x180b28575af4f2e8),   UINT64(0x2e5e2363ae8c9305),   UINT64(0x8f6ae1d5ad64ad0e),   UINT64(0xf3d1bdb0d242de42),   UINT64(0xcf4f0129f7a85425),   UINT64(0x8479a5307ab74e79),   
        UINT64(0xfc6b1533321934ad),   UINT64(0xb64245ca47d71ff2),   UINT64(0x4027e7b188eea501),   UINT64(0xa48fa36ad480ab13),   UINT64(0xe6a710d6f9216a8d),   UINT64(0x6bbd3825aee682a0),   UINT64(0x0de325204985999b),   UINT64(0x4f843defbd83da7f),   
        UINT64(0x0374946126dde77b),   UINT64(0xfb0b536103c2243f),   UINT64(0xb3c4312827a9f218),   UINT64(0xd607bf0fc123a0bb),   UINT64(0x9a05ec88bfb36d2f),   UINT64(0xa5407399ee41f28b),   UINT64(0xf6134f983b6644c1),   UINT64(0x3fede3890cb4931d),   
        UINT64(0xbe4e75bd0029e583),   UINT64(0xb8d5b3ccc17ffcb2),   UINT64(0x41f7f549d10acf4e),   UINT64(0x6bac118fad477593),   UINT64(0x9def329cfe322ae5),   UINT64(0x472c20c79234bd35),   UINT64(0x5497c15e79b82383),   UINT64(0x347fc7e67d10fa2b),   
        UINT64(0xbbaaeb3a95858e10),   UINT64(0xaef78ea8bdaf400f),   UINT64(0x3a0007cb3418c2fa),   UINT64(0x45bd3e08aa7766d3),   UINT64(0x3de35a3181284cf7),   UINT64(0xe61011f21b6d4915),   UINT64(0xdc559a361f2bfe40),   UINT64(0x59c57e6222b9e563),   
        UINT64(0xfc9c9c8aa40b6f03),   UINT64(0xa52d8d2873595e9e),   UINT64(0x03cd8c24c93d2b24),   UINT64(0x9b25b3994a3981d9),   UINT64(0xf38ac97222d03a80),   UINT64(0xa009d405b2a15c65),   UINT64(0xbfedff770cf2864c),   UINT64(0xe65d11e74c691ae5),   
        UINT64(0x94a626bb50a83752),   UINT64(0xdc6b1538f931f2ab),   UINT64(0x5a9616354e82d40a),   UINT64(0xe2cb3581ed4ec8a6),   UINT64(0xbfc73f37115872ed),   UINT64(0xc2960bb7b9d0b5a2),   UINT64(0x3d4ef6ad8b01946f),   UINT64(0x71471f1326b4fdb4)
    },
        //wking
    {
        UINT64(0x1bcb6fd7b25a7984),   UINT64(0x99a9889f86d62107),   UINT64(0x4963dee00bd6de9e),   UINT64(0xd6dd2743baefe660),   UINT64(0x75c210a5dd1fb881),   UINT64(0xba5fb6a64586340f),   UINT64(0xfb42d43bb04db764),   UINT64(0xd2369252c74aca38),   
        UINT64(0x02a04e86e4723e37),   UINT64(0x8ab9481b620d0986),   UINT64(0xf9a54cac9f487706),   UINT64(0x52265cc0401073ce),   UINT64(0x87fdbad08ef80ad4),   UINT64(0x3d103078fe161033),   UINT64(0xd9009df7d30f925b),   UINT64(0x2be10a04e03462ba),   
        UINT64(0x685925aea0d3669a),   UINT64(0x95113a847408477f),   UINT64(0xe3bdd0689147db1b),   UINT64(0x975b2e5b769e4bde),   UINT64(0x576508570665714f),   UINT64(0x6d1f7bffe8dea456),   UINT64(0xefbb61cd81010b77),   UINT64(0xa04edc991b13abeb),   
        UINT64(0x42db5410a230def3),   UINT64(0xde9e37eac6a3024e),   UINT64(0xd194f5c5f0e930eb),   UINT64(0x226b311834c1f022),   UINT64(0xc45b0b059fdc15b2),   UINT64(0x77a3f05419884508),   UINT64(0x9633605dbf9eca51),   UINT64(0xff2a62af9c66f95d),   
        UINT64(0x38cc17eb4e52211a),   UINT64(0xa09438b929a28dd5),   UINT64(0xc326a7fbf8212d37),   UINT64(0x93d032bc041cfd4f),   UINT64(0xe1cbc9756304ae8e),   UINT64(0xec22a9f84b44a99a),   UINT64(0x6644eb9dd4658673),   UINT64(0xee10a53a2ec38a08),   
        UINT64(0xbda1869b684f86fc),   UINT64(0xb43d48d1bff69af6),   UINT64(0xd513ea66780689f6),   UINT64(0xe5de99ed610aafbd),   UINT64(0xfa3b40ad38f8b9cc),   UINT64(0x7e5f1ff7621164a3),   UINT64(0x3478d978d51725de),   UINT64(0x8ae18f9b161b14c7),   
        UINT64(0xcfb238390ad46d19),   UINT64(0xe9ca3eb034f7f81b),   UINT64(0x99318526cd134ccf),   UINT64(0x3c150a52e1e177ea),   UINT64(0x76d699bfea838724),   UINT64(0x222afd0613f36c7d),   UINT64(0x39948a6dfff42b85),   UINT64(0x180ec5c64af864d8),   
        UINT64(0x9845073aaa5ba506),   UINT64(0x5922168999aa61af),   UINT64(0xb11965c35b615da3),   UINT64(0x1c6cecb446309ef7),   UINT64(0x4882b36e666292a6),   UINT64(0x41371ca13a3bb3c5),   UINT64(0xae29e4321c01f5cf),   UINT64(0x42ed1e601dbf0660)
    },
        //wpawn
    {
        UINT64(0x8da18412df7537ec),   UINT64(0x6cb2b16ba9fa62a2),   UINT64(0x3ab9b3c712ee6d01),   UINT64(0x22aa3b1b2e02fb29),   UINT64(0x52ed7fc5cf85af35),   UINT64(0x5ae20c2f4ac4d1df),   UINT64(0x2827404ee1454d18),   UINT64(0xe40380de01ef82e4),   
        UINT64(0x371fdcd1b9017907),   UINT64(0xc640585d39ffc4e9),   UINT64(0x5ee4d9623bd92ab0),   UINT64(0x6dab10f1791b3469),   UINT64(0x099d15c0934e0107),   UINT64(0x0e09901fb9323b6f),   UINT64(0x9a67aec32906c231),   UINT64(0xe95ba7a681622dd1),   
        UINT64(0xa33878c3d6726828),   UINT64(0x3db5837dcb3c11fc),   UINT64(0x81e1e009caa34c28),   UINT64(0xf6b85f21ce3718c4),   UINT64(0xdf0096e647d04826),   UINT64(0x6cd98d0a40330ae1),   UINT64(0x06426ca22610dcdb),   UINT64(0xbad1a631578980f5),   
        UINT64(0xef9428138a0ce434),   UINT64(0xf9f3852649daa356),   UINT64(0x75fce1183c6f6319),   UINT64(0x42d544388a4f80ec),   UINT64(0x957ec8eb52107af2),   UINT64(0xba9c54d112bf58e1),   UINT64(0xdf1b4eb4e4ee9a50),   UINT64(0xe864132734b20904),   
        UINT64(0x831cc7678327ada2),   UINT64(0x63a21908f1f042f8),   UINT64(0x3015aa6c3b4203e3),   UINT64(0x2e100882d7d5c4b5),   UINT64(0x5287e350d446b09d),   UINT64(0x838af7bdb55924e4),   UINT64(0xaff2c018942f3bbb),   UINT64(0x4283d680e943e214),   
        UINT64(0x0a0c3783c06ab901),   UINT64(0x11ff774fdcbcdbe6),   UINT64(0xfe30f70a0a433c1c),   UINT64(0x60d09e2e6df53f99),   UINT64(0x43a41105a41cdbad),   UINT64(0x7e9cf9a37f4dcd9e),   UINT64(0x52f5f4e094a264bb),   UINT64(0xdc64bba978fe4421),   
        UINT64(0x73fce8e90a11d170),   UINT64(0xf1b02bbf75e93aa9),   UINT64(0x2007fbbb39fbd90e),   UINT64(0x4f29e56a77d46535),   UINT64(0x0583c60488ee547e),   UINT64(0xa9580aff03f21989),   UINT64(0xeb0df0b4119989e4),   UINT64(0xf84ca79c563c9f8a),   
        UINT64(0xd3f9fb75b4280b24),   UINT64(0x258f90d3d8c8ddcb),   UINT64(0x919827ada4967e36),   UINT64(0x32297486e3d2e5c9),   UINT64(0x5d10a8f68208b3bd),   UINT64(0x5da0e61563e74f65),   UINT64(0xcf70a16fe12a2f3e),   UINT64(0x4de453060533c992)
    },
        //wknight
    {
        UINT64(0xb28ed203e8cd05e5),   UINT64(0x017f4ee31599565e),   UINT64(0x27b3d914b2200bc7),   UINT64(0xeb26c414dac9e2bc),   UINT64(0x427c50d094eb7cee),   UINT64(0xf18a5014c657deb2),   UINT64(0x89308dc56c68f6c4),   UINT64(0x8a86a8646732b8df),   
        UINT64(0xb1d79a0c3a717f91),   UINT64(0x3f37bf3c1ec13875),   UINT64(0x2e9185c824c8d524),   UINT64(0x51147f065f4db316),   UINT64(0x2e511675a38798e7),   UINT64(0x5125fe34fa371838),   UINT64(0x02ccb7db9fae7ee6),   UINT64(0xcb9166256de251fe),   
        UINT64(0xb3908742fe163698),   UINT64(0x9d1733469e129ba9),   UINT64(0x0a5c77e56d20a769),   UINT64(0xa7cf16ce54ec3804),   UINT64(0x78819a548a822052),   UINT64(0x72539ed5bf84767f),   UINT64(0x59c013ede2d41107),   UINT64(0x62b484c9368543de),   
        UINT64(0xe127033a6d919c7e),   UINT64(0x56f33fa3b208ab96),   UINT64(0x89c43e6e465b1fe3),   UINT64(0x516e3b812f6d4157),   UINT64(0x09793e09c76ff62f),   UINT64(0x8291a6a078877657),   UINT64(0xd212f2ed19771fff),   UINT64(0xf442c0ff8d37f451),   
        UINT64(0xa5c8910191ca585c),   UINT64(0xd8e76b4c5308c85c),   UINT64(0x9a8cf1ea9a8d5293),   UINT64(0x199377f34a128205),   UINT64(0xdd2b91ff2717b250),   UINT64(0x2ecd21a74312ff52),   UINT64(0x2be9811eeb35169a),   UINT64(0x3f8109ca9831fc8e),   
        UINT64(0xba72c4c434f7195f),   UINT64(0xac25e2b3e0a1b71e),   UINT64(0x701f4d062eedc0af),   UINT64(0xcabb51db66d794a7),   UINT64(0x3825370afbb3dbdc),   UINT64(0x0531a88639c2dc44),   UINT64(0x2b14d8bdd3edf618),   UINT64(0x40f7129cab02b5b1),   
        UINT64(0xe4048f6b1ce4b948),   UINT64(0xbdc78ee616cd6286),   UINT64(0xac189931d8161f20),   UINT64(0xbd8c8defd5b30ff9),   UINT64(0xae9ab4101b2ff3cc),   UINT64(0x30f8f9809d3d43ca),   UINT64(0xd0a20899bc03c4ab),   UINT64(0x8ebea97941d6d539),   
        UINT64(0x734cde3c4d2daeea),   UINT64(0x2f9b70a7ab2e2b3f),   UINT64(0xd8da0f4476416d05),   UINT64(0x3228980943d42b5e),   UINT64(0x0d793f9f65694f71),   UINT64(0xe03bb5a4257370c1),   UINT64(0xa66b2eb6d89a4bfc),   UINT64(0x6ed4b11524b21989)
    },
        //wbishop
    {
        UINT64(0xc71b0e7ad18250b0),   UINT64(0x52f75f965252c87a),   UINT64(0x601a2c18798c5a2f),   UINT64(0x137b0f4072e47359),   UINT64(0x5576dd96526fc2ec),   UINT64(0x93c081e73adf4aca),   UINT64(0x1ca6a6ef13d827a6),   UINT64(0xd7681ef5b2b38669),   
        UINT64(0x804f35064fe26714),   UINT64(0xfd876d48ecf1b86a),   UINT64(0x6773992e2e367da4),   UINT64(0x348a760f58480e15),   UINT64(0xd82170bf08c49bb4),   UINT64(0x28cd2c48d7c41ecc),   UINT64(0x29774d917d25a3ba),   UINT64(0xd92a5d915553b182),   
        UINT64(0x97e9d9fbe2df8f28),   UINT64(0xc81e4c6f5d2c40c7),   UINT64(0x84f5294b10e05c1d),   UINT64(0xc7c76671245c8fde),   UINT64(0x26f000717b9b6b14),   UINT64(0x7cf574f19171e772),   UINT64(0xda7cb601016c6b39),   UINT64(0xc39fb26f9ba61ee5),   
        UINT64(0x271c08553be06d11),   UINT64(0x07855bf203cdff4e),   UINT64(0xd0b2c9192acd3e87),   UINT64(0x405d8a01b1badfa5),   UINT64(0xf451f2315119a0aa),   UINT64(0xdbebdb52497fa5a9),   UINT64(0x98627556eb57869d),   UINT64(0x236ec8493d96ad84),   
        UINT64(0xa15731894d59ae87),   UINT64(0x1f4c3613258b8a40),   UINT64(0xfc5483c7b6225885),   UINT64(0x6d82961e1c732b80),   UINT64(0x66bed7505097f4e9),   UINT64(0xd7514f45c710c923),   UINT64(0xdc7101006b96aa51),   UINT64(0xdab07d27472c93b7),   
        UINT64(0xb05d852c7d14dd57),   UINT64(0x8e98e38d784520df),   UINT64(0x7da71baa4a263eec),   UINT64(0x1fc6ac0441526d27),   UINT64(0xadc6ce8cd8de2398),   UINT64(0x1885462f381028d7),   UINT64(0xa81d616069195533),   UINT64(0xe34074852bc96bb8),   
        UINT64(0x32515c8ede6c01e1),   UINT64(0x46f872b45843ebf4),   UINT64(0x192b76dacf82ae46),   UINT64(0x8864dcf3a51d7e79),   UINT64(0x6922f9b25e68f650),   UINT64(0xbc75d41a9a750b7f),   UINT64(0xfe98f86e59513f19),   UINT64(0xab0ccd6d8166d027),   
        UINT64(0x79c5da5ef48c959b),   UINT64(0x7d2dd295257ace4c),   UINT64(0x9ba75fd73c822150),   UINT64(0x1292524bc3d53df5),   UINT64(0x41c4473931a51f02),   UINT64(0xf96f48dd4d804019),   UINT64(0x20619c572073174a),   UINT64(0xaa663c9d63d98888)
    },
        //wrook
    {
        UINT64(0xbfce5e477cb4458b),   UINT64(0x7402d21508c6c536),   UINT64(0xd3b4fa225952377c),   UINT64(0x96cef9ae07f2c341),   UINT64(0x74e4afe6cd33626f),   UINT64(0x4eec9135cafb4666),   UINT64(0xe8d1861c79b41300),   UINT64(0xc44f6aa20011c5c0),   
        UINT64(0xb80ecc90407404d0),   UINT64(0xf117831321305906),   UINT64(0x2052cae48c41eb6f),   UINT64(0xf835151c69a9a3a6),   UINT64(0x2116686dac2412af),   UINT64(0x7f675beb087a076e),   UINT64(0x0daff434a78d4bed),   UINT64(0x00ce75fa07576e9c),   
        UINT64(0x56cc09be6fee6f1a),   UINT64(0xbbb2c18759279d95),   UINT64(0xf174358b2e019984),   UINT64(0x4bc98f19f628c692),   UINT64(0xe554270d283bd9aa),   UINT64(0x842742edc09c1df9),   UINT64(0x90bfe12b6bf7eeb3),   UINT64(0xb13b19350a8cfc4a),   
        UINT64(0xc8faea338417fb2e),   UINT64(0xf591dea263c942bf),   UINT64(0xfa90ac6825e55647),   UINT64(0x29cbb2ca3ed74a15),   UINT64(0x82115d31602f53a2),   UINT64(0xbb12f876e448af17),   UINT64(0xf3518e41c8b0396a),   UINT64(0x7e8fd513fd6f66dd),   
        UINT64(0x7e5115cf28f67d64),   UINT64(0x1db84feda21934e4),   UINT64(0x37345552ce2114fb),   UINT64(0x29054812f0977765),   UINT64(0x0c49851425e6f9a7),   UINT64(0x8e7f7828f3f2d29a),   UINT64(0xcbd1420d0774ca1e),   UINT64(0x11ba1ea761d621cd),   
        UINT64(0x05543f8c6fe3fa28),   UINT64(0xad40426c70474d67),   UINT64(0xe18fe247200d1e15),   UINT64(0x441ab3b99303c75a),   UINT64(0xb291035bb4bb5c21),   UINT64(0xb6fffc2ec0d68b97),   UINT64(0xdd57db180a453d4e),   UINT64(0xa5e911725af12875),   
        UINT64(0x696b312449c9487c),   UINT64(0x9e2ab1ba5aec9332),   UINT64(0x8b089e06286155be),   UINT64(0xb8dabb856fb376f2),   UINT64(0x2424dcbb6fb8ad4a),   UINT64(0x4736965d563a50e8),   UINT64(0x413ab68263a31c6f),   UINT64(0x74df2588a88c1a94),   
        UINT64(0xb2ecaaad49648474),   UINT64(0x0c2e832bfc4d0b30),   UINT64(0xc6c7ccb64379d255),   UINT64(0xe18ece5ea175abce),   UINT64(0x29f9ff93e7dc1bb1),   UINT64(0x91a3cd510aada5ad),   UINT64(0x629baaa0f6d39c67),   UINT64(0x1d40c9ae8a4b68cc)
    },
        //wqueen
    {
        UINT64(0x6930963a2d83d1b8),   UINT64(0x5f8984ef419598d2),   UINT64(0x964be082a192c2eb),   UINT64(0x884a906dcb9428b3),   UINT64(0xa1ce21922a567cb8),   UINT64(0xf8769d91394bc1c6),   UINT64(0xebf9359ca01c2c12),   UINT64(0x52e501794aed9126),   
        UINT64(0x269e8f7a33476e06),   UINT64(0x31d0312b181dee8b),   UINT64(0x57f7623b530ba5c5),   UINT64(0x943c6b3db913920a),   UINT64(0xfb37f150d6c74716),   UINT64(0x575c5eac22f8ca5b),   UINT64(0x96be2014e504d2c0),   UINT64(0x5925a770338beb8c),   
        UINT64(0xeebf7f5af06193ae),   UINT64(0x6cbcd51f02a60e53),   UINT64(0x57a16df59ca5cede),   UINT64(0x30fd41d8a3f0c261),   UINT64(0xb6b5dbf7bd835d56),   UINT64(0x9750ec5af7a31a55),   UINT64(0x8cd213bd2c97deb4),   UINT64(0x572e7329e6d9064d),   
        UINT64(0xb64beba635582c14),   UINT64(0x2300dc44169b5a28),   UINT64(0x6125c3ab93c3be62),   UINT64(0xdfe068eae463aee7),   UINT64(0xa6bf26da45d02857),   UINT64(0x0c6ef39e07829ae2),   UINT64(0x1c2885fe3fa163a7),   UINT64(0xf64e036f6461929d),   
        UINT64(0x1d3ddaa277c05231),   UINT64(0xd313df6be1513488),   UINT64(0x74f46bdc42aac734),   UINT64(0x4343a2e1881edbf4),   UINT64(0xc2d3dd1adc2501ca),   UINT64(0x3dbf43e05c5a2bf4),   UINT64(0xdc52569510ee8943),   UINT64(0x0a45ac5a73cce112),   
        UINT64(0xaadeeab404832512),   UINT64(0x040417e13547b0f8),   UINT64(0xdea2302bd4c18569),   UINT64(0x53df600a918d617f),   UINT64(0x6c8bd147686beeb7),   UINT64(0xd70964153ab59cc2),   UINT64(0xb60c02350d90a9a9),   UINT64(0x8398e974f81aee28),   
        UINT64(0x5bd9fdfbfa1bea58),   UINT64(0xac465687df68d481),   UINT64(0xed77e803cad224ca),   UINT64(0x401642b55f14ffa6),   UINT64(0x06a832fb4c3fa9f8),   UINT64(0xe4a34bd84b2b3446),   UINT64(0xcccf0925f5193feb),   UINT64(0xebddb3daeae77ebe),   
        UINT64(0x5249b9f78ad580b6),   UINT64(0xe285befc8446e62d),   UINT64(0x6f039931f7477756),   UINT64(0x9545c7517a55c32a),   UINT64(0x8e22187f292efebc),   UINT64(0xa941408fab9bc9be),   UINT64(0x6a6189e3b9dca693),   UINT64(0xac0cbc58ffa64c97)
    },
        //bking
    {
        UINT64(0xf7c9db2247102277),   UINT64(0x8073c6b5255f4f8e),   UINT64(0x5ba72c89426d5bbe),   UINT64(0xd315d08fcf69b9ef),   UINT64(0xe53b3c686ef7f306),   UINT64(0xadc69688e8716d2b),   UINT64(0x1c658fc06930261a),   UINT64(0x9cd0e98beee742da),   
        UINT64(0xf1865c93ef7bf7f5),   UINT64(0xbf9918208e598d38),   UINT64(0xf5295682b6b3f3e7),   UINT64(0x18c87782cc24b780),   UINT64(0x608def3931cd422b),   UINT64(0xab14561bdde01fd5),   UINT64(0xd3ea5582dbacd372),   UINT64(0xc0d7fa0329924a92),   
        UINT64(0x464bb3a10f5afa22),   UINT64(0xe82419c66a472f42),   UINT64(0xa74513da54ebab6b),   UINT64(0x7c8abfbd67542989),   UINT64(0xa61cfd00c891a758),   UINT64(0xadda84c841269cc4),   UINT64(0xa6fafc066d6bbe7c),   UINT64(0x99220a5db42aa72f),   
        UINT64(0xf696ff7c1ec1a503),   UINT64(0xa9ba196740e3e9c9),   UINT64(0x4a3a3a3370880e19),   UINT64(0x7cc3ef738ffffc5c),   UINT64(0x5e6386fc331bf809),   UINT64(0xe467f257d1cd3148),   UINT64(0xe42f74dbbe4a7092),   UINT64(0x93519f6c6a0de303),   
        UINT64(0x86a5acd3acd77a30),   UINT64(0x6646bb1d5bd3b56d),   UINT64(0xb75cfcb413de6e70),   UINT64(0xcb65cb9a44a98f6d),   UINT64(0x9765913448720e93),   UINT64(0xe67929fcb2e5a573),   UINT64(0x283c00e8ba24baff),   UINT64(0x81fb945068b14bc5),   
        UINT64(0xaa85ad7392145356),   UINT64(0xb6cc2c7b53e77fd1),   UINT64(0x2fa5a4a92c64dc28),   UINT64(0x533cb807d28e39d6),   UINT64(0x99bf241fed0f049b),   UINT64(0x6709c7722c4d349a),   UINT64(0x5581e307a91cd582),   UINT64(0x9ef7cc9ca2e67f12),   
        UINT64(0x2425c3e52a8284b5),   UINT64(0x82335bacc656ec1f),   UINT64(0x3441be23bef56aa9),   UINT64(0x473f9b933ce25ed4),   UINT64(0x0fb52242111dbd9c),   UINT64(0x7023f01c13e915d8),   UINT64(0x8d9e46a887d3cccf),   UINT64(0xf7b04a741c1786e7),   
        UINT64(0x4364c30d97ffcfa2),   UINT64(0xfe1b3296b206a382),   UINT64(0x7122b899a40ac792),   UINT64(0x2bdf88340c159247),   UINT64(0x8e44d1cb76bbc165),   UINT64(0x48afb0289eea948a),   UINT64(0x0cfb3a6fe7adc10e),   UINT64(0x5371a8ac0b8b0628)
    },
        //bpawn 
    {
        UINT64(0x582221d08c79a506),   UINT64(0x69a975f6bcc335ab),   UINT64(0x098ebb864c01a134),   UINT64(0x8a58a7238512cd36),   UINT64(0x4033da390038e699),   UINT64(0x64445bad5b0b38d4),   UINT64(0x885eded79710f45a),   UINT64(0x73bad8e9a79e751c),   
        UINT64(0x264e22ad5c3137dd),   UINT64(0x825a68836084094d),   UINT64(0x06b06f09cb59dd14),   UINT64(0x8e0128fc087d3248),   UINT64(0x732095f5c6564d2e),   UINT64(0x9cf733c9cfd0001a),   UINT64(0xcc7c28cdd1a4a943),   UINT64(0x6e8d85c3190a6def),   
        UINT64(0x7bf90f6157f8dfb8),   UINT64(0x12d0d20d00ab5ca1),   UINT64(0xf826f889aef39e6c),   UINT64(0x2b99d9d859f3624d),   UINT64(0xa691b7f8f687a8ee),   UINT64(0xc72c2fc647c6a588),   UINT64(0xff829856b894514c),   UINT64(0x82bdb8e09622092e),   
        UINT64(0x996408862f74cb3c),   UINT64(0x6ba5ca9c274355e2),   UINT64(0x9695eb4f58529caa),   UINT64(0xf29df9744f4be0b3),   UINT64(0xad068566f932dbf9),   UINT64(0x4f65aa3401c87a8b),   UINT64(0x64ade32a92cb0b6d),   UINT64(0x5642051a26134a4f),   
        UINT64(0xdb108932db5b82a0),   UINT64(0xa6390992aa4123d1),   UINT64(0xd8358d2a61dbc9f0),   UINT64(0xa893774d67d88811),   UINT64(0x7f06e3309fef6141),   UINT64(0xa214f20d4a364f56),   UINT64(0xa1d6b35755377293),   UINT64(0xbe83b4994c229c27),   
        UINT64(0xf5d09e9b0fb63a33),   UINT64(0xa180d8cb38c52e33),   UINT64(0xf7626f0ecb15c992),   UINT64(0x2e5ab2c14ba68e9f),   UINT64(0x6032f7b51bc5870c),   UINT64(0x9d693fd57c3d6b5e),   UINT64(0xa701b9df7308bc1b),   UINT64(0xb2adfaf91af35d71),   
        UINT64(0xc4d6b3b0361f74d3),   UINT64(0x28d9cbb95057d64e),   UINT64(0x7b2db9b5ecea369c),   UINT64(0xaf7e7f2f32ba9abc),   UINT64(0x0653805fbb737975),   UINT64(0x1d204eb8ab1395de),   UINT64(0x77f0755ab5f1a65a),   UINT64(0xb7ff2b64afbe7d4e),   
        UINT64(0x89c5d0bfb0037677),   UINT64(0x54d71a8a5a2d4770),   UINT64(0x01ed0e3df5e43a49),   UINT64(0x9b83ec16f4556767),   UINT64(0x1269fb4871a41cec),   UINT64(0x605719ac3339ae53),   UINT64(0x67b0ac959e66b719),   UINT64(0x2817d2b8559927bf)
    },
        //bknight
    {
        UINT64(0x8ec18f1460e086a7),   UINT64(0x96128143b061d768),   UINT64(0xadca9ac9ae70728d),   UINT64(0xa93b3139be2ff8c7),   UINT64(0xabbfc4d4e338b8b1),   UINT64(0x29581c8e73ba5501),   UINT64(0x262cdd31cbdcca11),   UINT64(0xf14af5a210b3022d),   
        UINT64(0x9a7de096808782fe),   UINT64(0x99fc42e40b3df70a),   UINT64(0x9354c321851da88c),   UINT64(0xc40e7eb973bde0a3),   UINT64(0x0cf78957bd82f15d),   UINT64(0xf76cf946af68a36d),   UINT64(0x7fbbde466d0cef73),   UINT64(0x20ee32c0519477e1),   
        UINT64(0x864f6b6b355bc1af),   UINT64(0x19ab1083567132b0),   UINT64(0x3a0f1d52e2ddd720),   UINT64(0x33527139756d92ea),   UINT64(0xb31beaafd484935a),   UINT64(0xeaad80e4d623f4e0),   UINT64(0x7eb0f6fe78309a63),   UINT64(0x07a9a8c2505f968d),   
        UINT64(0x863b1993f69043fd),   UINT64(0x62abf472ae58c2b4),   UINT64(0x5f00e24f87430157),   UINT64(0x6b96a9fd4fe6ca2c),   UINT64(0x7eb019ea0934ad66),   UINT64(0x31d060c2f613beeb),   UINT64(0x9eec493aeb441f78),   UINT64(0x47c7c08910127ac1),   
        UINT64(0xf3067c8fb66cb8c2),   UINT64(0xb9d2c15b45392bf6),   UINT64(0xf946928eecc358f2),   UINT64(0xf9f4cb0a534b7c1f),   UINT64(0x66bfa4dff7bafb14),   UINT64(0x61fa93a294eb79dd),   UINT64(0x636a10315445833d),   UINT64(0x9d85db4589c2cc76),   
        UINT64(0x964764fa5489cee9),   UINT64(0xd809c5607081f05c),   UINT64(0x40a08eacb4f534e8),   UINT64(0xe96157469276de1b),   UINT64(0xcdef57d76daf9f4a),   UINT64(0x388f56cf7126194e),   UINT64(0xadd3530c617498b3),   UINT64(0x0163249affe1cb87),   
        UINT64(0x38748e2cd810d7f3),   UINT64(0x8121813d5e0bcf4d),   UINT64(0xb604040a5cd1d7e1),   UINT64(0x61faf6990a3f799e),   UINT64(0x298b9323e85faec4),   UINT64(0xb700c33d6b4a9498),   UINT64(0x980d2b8bf9946fcd),   UINT64(0x047228bc397a6c33),   
        UINT64(0x10f4fddd31ffba76),   UINT64(0xf4a16367715a1635),   UINT64(0x0e2aad6e78f386bc),   UINT64(0xe25b1e09f0b450c9),   UINT64(0x4a99b4c61b078a8f),   UINT64(0x189f2aaa1729ff59),   UINT64(0x7fc9c0a45c2793f2),   UINT64(0x26a70e8de571039d)
    },
        //bbishop
    {
        UINT64(0x983017be47643499),   UINT64(0x8896e72c08db0105),   UINT64(0x4e2287a379d96f09),   UINT64(0x22e57be1ac6012e2),   UINT64(0x28e7760b7d180a8f),   UINT64(0xe269b8bbf51d31f2),   UINT64(0xe417951f89b51a7f),   UINT64(0x172935c5e5c5044b),   
        UINT64(0x6b9dc51fc39fee99),   UINT64(0xee6677d22b252ab0),   UINT64(0x89de9a19b8205d8f),   UINT64(0xf31867caf385e0d0),   UINT64(0x301b3d2e2071f7fa),   UINT64(0xdcdd9821d349820b),   UINT64(0xd4f12a3bf4050143),   UINT64(0x0ea2190a82cfcaaa),   
        UINT64(0x63d6f08ef2a3dd47),   UINT64(0xe19a97b95c3a0daf),   UINT64(0x64c5938688ca44c7),   UINT64(0xb0dcd6f05363a1a1),   UINT64(0xdec3a2f58fa8d7db),   UINT64(0x07c6fbb416dd0b0d),   UINT64(0x34ce364d76615d01),   UINT64(0x338db41346827989),   
        UINT64(0xcca4357563357488),   UINT64(0xa0b48b7a9ac5147d),   UINT64(0x94433a84197a135f),   UINT64(0xa9d6141e5f71e707),   UINT64(0xe7680c5674418590),   UINT64(0x98102b942d5006a6),   UINT64(0x8c326f5c55d557f1),   UINT64(0x488aadc8ffaae69a),   
        UINT64(0xef0f43ba512b29d4),   UINT64(0x9ffbe907855bba1c),   UINT64(0x0457d23194b55ab9),   UINT64(0xa6b1fbe148a1e1d7),   UINT64(0x73981811c4f51d4c),   UINT64(0x2f92364c65a4b549),   UINT64(0x4f3e29c68970003f),   UINT64(0x04aa2664ed2f96f4),   
        UINT64(0x8d72576157adc6b8),   UINT64(0x464f2acb67bcba8e),   UINT64(0xfe27d2d2ce22c569),   UINT64(0x8078bca81c9ee689),   UINT64(0xb8fc20563becb695),   UINT64(0x6ee465eff1a756ab),   UINT64(0x653e8adeca81668a),   UINT64(0x85bfa9915151cb93),   
        UINT64(0x8187642bab778b54),   UINT64(0xa3f2c6c93011d35d),   UINT64(0xe08c967176cb5dba),   UINT64(0x9fdd3ae12d0f01bb),   UINT64(0x5065105f15036ec7),   UINT64(0x512d193acf2eb247),   UINT64(0xe23ccf8cacda8c67),   UINT64(0x0cb1448a6ceb9dd6),   
        UINT64(0x0c7a15369d1520dd),   UINT64(0xd36229be0c2e1413),   UINT64(0x7aa3997b1959a429),   UINT64(0x268cd91c9ed31cae),   UINT64(0xfedbee14d406c390),   UINT64(0x41f040b2d95a2fda),   UINT64(0x328ec6ecbf1122db),   UINT64(0x33c7803c62b2ac00)
    },
        //brook
    {
        UINT64(0x78f5609de3255c1c),   UINT64(0x441ed6402ecf3ec3),   UINT64(0xc85f0f635e5bf5e9),   UINT64(0x027f442be143b1c7),   UINT64(0xc0b2b7acd2f6bd72),   UINT64(0xace13fc601d57fe8),   UINT64(0xf86700f0b2b998e5),   UINT64(0xeefdbf64827555b8),   
        UINT64(0x8f35af18ec984fed),   UINT64(0x7f8107ddfcdbb87e),   UINT64(0xf1158b421880c35e),   UINT64(0x1b4703404d75910f),   UINT64(0xf693554998446a44),   UINT64(0x47b131ebb2135436),   UINT64(0xeb6470feccad79f2),   UINT64(0x065100b1fc5c088a),   
        UINT64(0x1b17579cbaf0afc1),   UINT64(0x67879a3ee2a609de),   UINT64(0x98123c7498d896a2),   UINT64(0x8960ec0dad75b4b3),   UINT64(0x1490fe9a3513a7b1),   UINT64(0x25e033c3d493284d),   UINT64(0x2a2224939d459c66),   UINT64(0x0a11e4e37c2a9c63),   
        UINT64(0x54274efac483821c),   UINT64(0x2fa774441128ef7f),   UINT64(0x4f24df3d0b176a03),   UINT64(0xd7841ae72b8c2585),   UINT64(0xf336947b6b7a3db7),   UINT64(0x868cb038061c99fe),   UINT64(0x2dc84ddf009ea01a),   UINT64(0x7132b8e9ec7d4019),   
        UINT64(0xb7b34b82deb6a318),   UINT64(0x2f9c9727a3497a81),   UINT64(0x0b31416299d14e82),   UINT64(0xbdf28ee2a97b6f79),   UINT64(0x92980e969cbf4929),   UINT64(0xec43b19aef02d1d8),   UINT64(0x6f9b226766d6bcd8),   UINT64(0x2198a407220af5e0),   
        UINT64(0xd4d93ca0304316e0),   UINT64(0x0039119bab16c108),   UINT64(0x2ac07ad15abe6255),   UINT64(0x17c889f5bfbe2629),   UINT64(0x4f61bd01ff9cf62e),   UINT64(0x1ed096c6c4617bb3),   UINT64(0xd08bada6a44de0e1),   UINT64(0x126b39ed6ee39fd6),   
        UINT64(0x9a97767beb732c36),   UINT64(0x923596ec670a9ebc),   UINT64(0x0e8f7a3a82f69766),   UINT64(0x124d151764cb7952),   UINT64(0x99e83de11e7d8ac0),   UINT64(0xf70d4c40f15ecf29),   UINT64(0x72c737ac89e59f67),   UINT64(0xb86490e01db31c78),   
        UINT64(0x47dd319c10646eee),   UINT64(0x0f015a1e9f48854a),   UINT64(0x641fc5b42335c9d2),   UINT64(0x4641285ffc51d955),   UINT64(0x4f38c7045dc0c12e),   UINT64(0xf4b24956286ba818),   UINT64(0xb547bfbe0542ef14),   UINT64(0x7c213bd3d4fe732a)
    },
        //bqueen
    {
        UINT64(0x2d99fe8337436a70),   UINT64(0xb3920d0feadddce0),   UINT64(0x4247af59ac18a66c),   UINT64(0x4b30d727ce7babb6),   UINT64(0x6429438909f4729a),   UINT64(0x7e26913c9c81af22),   UINT64(0x21636af4c90ab881),   UINT64(0x4a70288e626310b4),   
        UINT64(0xf2cdad51a2923c3a),   UINT64(0x553403971d0174b4),   UINT64(0xe7c1ddeaba5f6137),   UINT64(0x04c24029e72c8fa0),   UINT64(0x42680579ae19917c),   UINT64(0x3a4fb833e265922c),   UINT64(0xaa5d66df7d25b4c0),   UINT64(0x3ea321c62ddb13bf),   
        UINT64(0x9a98c963d963765c),   UINT64(0xb059faa3da54037e),   UINT64(0xabbd296ceb293bef),   UINT64(0xca0681a09d43325f),   UINT64(0x578bab6db1e5f41d),   UINT64(0x54614ca439e5cce2),   UINT64(0x5ef4e02176fe1bd3),   UINT64(0x86de554254f8a95c),   
        UINT64(0x4c4c88f4099ad1fa),   UINT64(0xf16cc35f562237fa),   UINT64(0x606d8bca783a5c81),   UINT64(0xe6c8d16841d6b3e5),   UINT64(0x86239a297ffdef1c),   UINT64(0xf9ae243ebf1a0132),   UINT64(0x7cf5ed1379c02332),   UINT64(0xc267fdfc2e29797e),   
        UINT64(0x4d7a50b9322f31cd),   UINT64(0x879c924b37a0d967),   UINT64(0x079b05721c37738e),   UINT64(0x36def91df7789b46),   UINT64(0xc8c76a3e34383fec),   UINT64(0xb1770d1d83a5dfcc),   UINT64(0xb6c8cc633e99cc48),   UINT64(0xe9f9523c73f2a37b),   
        UINT64(0x7e040b86d569f4a2),   UINT64(0xf1af3261a72f780b),   UINT64(0xc93275f83ee7b2ed),   UINT64(0x7b79363f31755b3c),   UINT64(0x672789aa91dfc754),   UINT64(0x4ebb62e48ff420a8),   UINT64(0xfb0297b344fa02f7),   UINT64(0xd20df5be1836ca8e),   
        UINT64(0x862c75ee332417d9),   UINT64(0x44d19c3743962dac),   UINT64(0x1bd488b5e9741425),   UINT64(0x0a729e4b9b1266a3),   UINT64(0xed1cb179c1ef93ee),   UINT64(0xac0947df8c7b8b80),   UINT64(0x3df44a3bead40c10),   UINT64(0xf733efccbf6fb258),   
        UINT64(0xd6a67ddd0b0ba9e8),   UINT64(0x7d66f1187f49ee18),   UINT64(0xcf67356982a7eef7),   UINT64(0x47a026e2e6cfd4fe),   UINT64(0x23b80f6499553ca9),   UINT64(0x214d2223d2fd1052),   UINT64(0xe53c3b68ccdc43db),   UINT64(0xe45be564a3f2eb5b)
    },
        //elephant
    {
        UINT64(0xaaa5763e66de8cd6),   UINT64(0xf9d8da2b25a6e79e),   UINT64(0xb9a494d613305fd2),   UINT64(0xf821f1e605a696f0),   UINT64(0x2c53836fe7308b48),   UINT64(0x33a1fcafe7c2efe0),   UINT64(0x95548d7c6ec72c95),   UINT64(0x5328f753612e0f7d),   
        UINT64(0xbbed9e9a2cad08c0),   UINT64(0x7f676b8dc834f393),   UINT64(0x34ab496391dbfe5d),   UINT64(0xefb25d97464bbac4),   UINT64(0x2da1518e8b12f6e0),   UINT64(0xaf210c8adae0f430),   UINT64(0x2124302ceb8ecced),   UINT64(0x8640845766eb0e8b),   
        UINT64(0xf8e35eb5391aae58),   UINT64(0xc8f9ab7893e516ce),   UINT64(0xbf8e7bbfb7dbe3f0),   UINT64(0x67f5f8b9dc6c40e6),   UINT64(0xc2bc073fd33f6e5d),   UINT64(0x8ab279e51175390c),   UINT64(0xab8e77462dab5e86),   UINT64(0x9d9acf40948c0ab3),   
        UINT64(0x7c9bd631019b0960),   UINT64(0x0fec9d5c0e52922c),   UINT64(0xf0e5ab7bdd03021b),   UINT64(0xe1cc3ab14ff00067),   UINT64(0xbc37352fe2ec72fe),   UINT64(0xf5dc8d37b6e88482),   UINT64(0x7b036548ef59cc79),   UINT64(0x44d2490f3c4ec509),   
        UINT64(0xb8ec402d3eb52333),   UINT64(0xaee3f1023b03030d),   UINT64(0xeb5d33af9c07491e),   UINT64(0x009effa314399b7d),   UINT64(0x032e7cd8a08082d4),   UINT64(0x80944065162d2f64),   UINT64(0x3f0f4a0a42d801d0),   UINT64(0x4b741f14ce891a03),   
        UINT64(0x627b0ae55e41553d),   UINT64(0x3e99d5ad68a613d5),   UINT64(0xd44583977ebf1e6f),   UINT64(0xcfb26894c7640400),   UINT64(0x1e56121febd65347),   UINT64(0xfd0d53d8daff1dca),   UINT64(0xc7eb3555e2a6880c),   UINT64(0xb74fcd142ceb8dfd),   
        UINT64(0x5ece63527daa6b7e),   UINT64(0xbcaf1139a6543c6c),   UINT64(0x2c23eb33f263a338),   UINT64(0x6175bf8dd9850fef),   UINT64(0x590ad0f95c78db92),   UINT64(0x5b8967a6bb25c78c),   UINT64(0x200c5d8a49c7759f),   UINT64(0xe0c7936065c269b6),   
        UINT64(0xb45d94ca2c2c970b),   UINT64(0x2c7adb3be0d192bf),   UINT64(0x99421beb43d04ad6),   UINT64(0x38d1dfb5dbed1de9),   UINT64(0xac61e70587e5af42),   UINT64(0xd02918ac07afd0c8),   UINT64(0xc7b49a3d13fdac72),   UINT64(0xc44a110f78731d4f)
    }
};
CACHE_ALIGN const HASHKEY ep_hkey[8] = {
    UINT64(0x691ff0fe173166d1),
        UINT64(0x0c9ffc9f7517308d),
        UINT64(0xf1dadc21e7caab3c),
        UINT64(0x243ddd28e3c1415b),
        UINT64(0xfc7ac1767865c9b4),
        UINT64(0xfa395131cfcc28ba),
        UINT64(0x02bb21afa638b262),
        UINT64(0x328272d766106861)
};
CACHE_ALIGN const HASHKEY cast_hkey[16] = {
    UINT64(0x1acfc1c33a776c55),
        UINT64(0xd81d91c2b209cac0),
        UINT64(0xd60350521b17095a),
        UINT64(0x069a34fd3316220d),
        UINT64(0xa43eca99107230b9),
        UINT64(0xaa470d58784ee3b2),
        UINT64(0xa88d40807224727f),
        UINT64(0x5700a137de071dba),
        UINT64(0xf3d0e075d72d5b2d),
        UINT64(0x247ee060253714f5),
        UINT64(0x964ecf4cbff8a00e),
        UINT64(0xcb4e04bc9710fde5),
        UINT64(0x7056e26904914686),
        UINT64(0x3929ffe0c63f87b3),
        UINT64(0xb83b5f1645e16d09),
        UINT64(0x3036c8524b6c27c2)
};

CACHE_ALIGN const HASHKEY fifty_hkey[100] = {
    UINT64(0x60015f27ba684567),
    UINT64(0x0b70634f536c5cff),
    UINT64(0xd8731843d6ced8ba),
    UINT64(0x97feb01f757be146),
    UINT64(0x8c159cf2b6e2231b),
    UINT64(0x6b3d34d68fc5255a),
    UINT64(0x483ccb6dc26ac4c9),
    UINT64(0x993486809e0357b7),
    UINT64(0x38ac7d0747c2e125),
    UINT64(0x8addf984ef325ae9),
    UINT64(0xd5f018277330d0cd),
    UINT64(0xafaa837bceaf8611),
    UINT64(0x42ce3d69c0e17521),
    UINT64(0x501bcd2b6109dde9),
    UINT64(0x25e1020c55b82dfc),
    UINT64(0x2dcf608680bd2a97),
    UINT64(0x07d01c880a17d78f),
    UINT64(0xffd0cf198055fcb0),
    UINT64(0x866b87d73dc78d3c),
    UINT64(0xa50d4b790ac5a45c),
    UINT64(0x760f3a7aaebf40fb),
    UINT64(0x13d206fe862e2529),
    UINT64(0xcfb50af04751b77c),
    UINT64(0x08b859e2a51f2861),
    UINT64(0xf7c80592537836a8),
    UINT64(0xf3839fe145d6b5eb),
    UINT64(0xafe00fe73a270bf7),
    UINT64(0x303d74055a520ae5),
    UINT64(0xa379cd1788a9b148),
    UINT64(0xef9254a501355d23),
    UINT64(0x2d939a5a2f3b4564),
    UINT64(0xcce3acab92ac82c5),
    UINT64(0x716dcbc226da813b),
    UINT64(0xb816ae8212a3f09e),
    UINT64(0x14001327a794d9ac),
    UINT64(0xcd54d383e80ff03b),
    UINT64(0x62a1139e87d55648),
    UINT64(0xccaca1be8ed0016f),
    UINT64(0xb43215e85b20b07d),
    UINT64(0x66c39a1b5807811c),
    UINT64(0x820a2f1c270c18f8),
    UINT64(0x18dc7e844c012aca),
    UINT64(0x4fad1d41e7599938),
    UINT64(0xb40e0e9d46511a34),
    UINT64(0xcff703b7394545fa),
    UINT64(0x91aa9082faf65486),
    UINT64(0x474d7fb42d8e6a55),
    UINT64(0xd5cd54528bf46373),
    UINT64(0x3659fa443e3fff36),
    UINT64(0x8c27056c66affcf0),
    UINT64(0xddabfa700ee70d15),
    UINT64(0xff78a5d81542b5a9),
    UINT64(0x768f5a4678f4058a),
    UINT64(0xc23257deb069e80a),
    UINT64(0xe93f488f761c47a8),
    UINT64(0x8ffff6d3cf50ab8e),
    UINT64(0xe7793cc39c5e9d09),
    UINT64(0xb4b3bde93908a233),
    UINT64(0x00d2f208a5971e7f),
    UINT64(0x1700c0a553276e47),
    UINT64(0xcc4532664fef8c4a),
    UINT64(0x787c8b31d2826823),
    UINT64(0x98a2bda82adb4807),
    UINT64(0x6103c4bef88328b9),
    UINT64(0xbe0e4e660e45763e),
    UINT64(0x635b9635299ce867),
    UINT64(0x863d0cd51d38b9a3),
    UINT64(0xcee13c2b0d1bda34),
    UINT64(0xb553da99008f8110),
    UINT64(0x177d255848390401),
    UINT64(0x48f9432f5bcf6e78),
    UINT64(0x0b3e0bc53fc97e23),
    UINT64(0x60a107159ee1256a),
    UINT64(0xc81f747ffc634f25),
    UINT64(0xba63040bc0a9413a),
    UINT64(0xd8a3c91e3c061690),
    UINT64(0xd7ed9fc46514f49b),
    UINT64(0xe87c74225dac8ecf),
    UINT64(0x4a5171d66609ac5e),
    UINT64(0xe64382ea70be8ab2),
    UINT64(0x051e888694a589d8),
    UINT64(0x8b4b589f47e14a82),
    UINT64(0xb6c01d3cd9311a87),
    UINT64(0x05f9a964b1a8a88a),
    UINT64(0xb1804b714cc67044),
    UINT64(0xd6a9cd5e0540e74e),
    UINT64(0xba55742f5722bdad),
    UINT64(0x7489b35542c188ec),
    UINT64(0x85d747524da72b8c),
    UINT64(0x45dc426654b397c0),
    UINT64(0xe7669718192445e4),
    UINT64(0x6d7d9133feb4e063),
    UINT64(0x5993b618127ae6b4),
    UINT64(0xf3730cd9835743af),
    UINT64(0x617b7d0b75d13207),
    UINT64(0xa9109d2ba1540e34),
    UINT64(0x90649a709b69d089),
    UINT64(0x5b1a5b3008d893c1),
    UINT64(0x59680fa3975c4a67),
    UINT64(0x2195992a7ab567ec)
};
