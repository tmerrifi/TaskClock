
#ifndef TX_ESTIMATE
#define TX_ESTIMATE

#define TX_ESTIMATE_EWMA_WEIGHT 0.2F

//This is the minimum number of observations before we make a choice about what to do
#define TX_ESTIMATE_MIN_OBSERVATIONS 10

//we store this number of observations
#define TX_ESTIMATE_TOTAL_OBSERVVATIONS 10

#define TX_ESTIMATE_TOTAL_ENTRIES 32

struct tx_estimate{
    unsigned long ewma;
    uint64_t observations[TX_ESTIMATE_TOTAL_OBSERVVATIONS];
    uint64_t counter;
};

struct tx_estimator{
    struct tx_estimate entries[TX_ESTIMATE_TOTAL_ENTRIES];
};

static inline void tx_estimate_init(struct tx_estimator * txe){
    memset(txe,0,sizeof(struct tx_estimator));
}

static inline struct tx_estimate * __tx_get_entry(struct tx_estimator * txe, uint32_t identifier){
    int id = 0; 
    if (identifier){
        id = ((((identifier & 0xFF00)>>8)  & ((identifier & 0xFF0000)>>16)) % (TX_ESTIMATE_TOTAL_ENTRIES-1)) + 1;
    }
    return &txe->entries[id];
}

static inline void tx_estimate_add_observation(struct tx_estimator * txe, uint32_t identifier, uint64_t ticks){
    struct tx_estimate * tx = __tx_get_entry(txe, identifier);
    tx->ewma=(uint64_t)(ticks*TX_ESTIMATE_EWMA_WEIGHT + (1.0F-TX_ESTIMATE_EWMA_WEIGHT)*tx->ewma);
    tx->observations[tx->counter++ % TX_ESTIMATE_TOTAL_OBSERVVATIONS]=ticks;
}

static inline int64_t tx_estimate_next_observation_guess(struct tx_estimator * txe, uint32_t identifier){
    uint64_t total, result;
    struct tx_estimate * tx = __tx_get_entry(txe, identifier);
    if (tx->counter > TX_ESTIMATE_MIN_OBSERVATIONS){
        total=0;
        for (int i=0;i<TX_ESTIMATE_TOTAL_OBSERVVATIONS;++i){
            total+=abs(tx->observations[i]-tx->ewma);
        }
        result=(tx->ewma+(total/TX_ESTIMATE_TOTAL_OBSERVVATIONS));
    }
    else{
        result=-1;
    }
    return result;
}

#endif
