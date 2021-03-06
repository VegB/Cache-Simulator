#include "stdio.h"
#include "cache.h"
#include "memory.h"
#include "def.h"
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace std;

/* Memory and Cache */
Memory memory;
Cache level1, level2, level3;

/* Counting */
int hit_cnt = 0;
int request_cnt = 0;

void ConfigureCache(int line_size){
    CacheConfig cfg1;
    cfg1.size = (1 << 15);
    cfg1.associativity = 8;
    cfg1.line_size = line_size;
    cfg1.write_policy = WRITE_THROUGH;
    cfg1.write_allocate_policy = NO_WRITE_ALLOCATE;
    level1.SetConfig(cfg1);
}

/* Configure memory and cache */
void Configure(){
    level1.SetLower(&level2);
    level2.SetLower(&memory);
//    level2.SetLower(&level3);
//    level3.SetLower(&memory);
//    level1.SetLower(&memory);
    
    StorageStats s;
    s.access_time = 0;
    memory.SetStats(s);
    level1.SetStats(s);
    level2.SetStats(s);
    
    StorageLatency ml;
    ml.bus_latency = 0;
    ml.hit_latency = 100;
    memory.SetLatency(ml);
    
    StorageLatency l1, l2, l3;
    CacheConfig cfg1, cfg2, cfg3;
    l1.bus_latency = 0;
    l1.hit_latency = 4;
    cfg1.size = (1 << 15);
    cfg1.associativity = 8;
    cfg1.line_size = 64;
    cfg1.write_policy = WRITE_BACK;
    cfg1.write_allocate_policy = WRITE_ALLOCATE;
    
    l2.bus_latency = 6;
    l2.hit_latency = 5;
    cfg2.size = (1 << 18);
    cfg2.associativity = 8;
    cfg2.line_size = 64;
    cfg2.write_policy = WRITE_BACK;
    cfg2.write_allocate_policy = WRITE_ALLOCATE;
    
    l3.bus_latency = 0;
    l3.hit_latency = 20;
    cfg3.size = (1 << 23);
    cfg3.associativity = 8;
    cfg3.line_size =64;
    cfg3.write_policy = WRITE_BACK;
    cfg3.write_allocate_policy = WRITE_ALLOCATE;
    
    level1.SetLatency(l1);
    level2.SetLatency(l2);
    level3.SetLatency(l3);
    
    level1.SetConfig(cfg1);
    level2.SetConfig(cfg2);
    level3.SetConfig(cfg3);
    
    /* build cache */
    level1.BuildCache();
    level2.BuildCache();
    level3.BuildCache();
}

void HandleTrace(const char* trace){
    int hit, time;  // used to collect results
    uint64_t addr;
    char content[64];  // write content
    string request;
    ifstream fin;
    int w_cnt = 0;
    
    fin.open(trace);
    while(fin >> request >> hex >> addr){
//        cout << request << " " << dec << addr << endl;
        if(request == "r"){
            level1.HandleRequest(addr, 4, READ, content, hit, time);
        }
        else{
            level1.HandleRequest(addr, 4, WRITE, content, hit, time);
            w_cnt += 1;
        }
        request_cnt += 1;
        hit_cnt += hit;
    }
    fin.close();
    cout << "write number: " << w_cnt << endl;
}

int main(int argc, char * argv[]) {
    /* get file name from command */
    if(argc < 2){
        cerr << "Missing operand. Please specify the trace." << endl;
        return 0;
    }
    else if(argc > 2){
        cerr << "Too many operands. Please specify the trace only." << endl;
        return 0;
    }
    const char* trace = argv[1];
                               
    Configure();
    HandleTrace(trace);
    
    StorageStats s;
    level1.GetStats(s);
    printf("Total L1 access time: %d(cycles)\n", s.access_time);
    cout << "L1 miss rate: " << level1.CalculateMissRate() * 100 << "%" << endl;
    cout << "L1 dirty cnt: " << level1._dirty_cnt << endl;
    cout << "L1 write hit: " << level1._write_hit << endl;
    cout << "L1 replace dirty by write: " << level1._replace_dirty_by_w << endl;
    
    level2.GetStats(s);
    printf("Total L2 access time: %d(cycles)\n", s.access_time);
    cout << "L2 miss rate: " << level2.CalculateMissRate() * 100 << "%" << endl;
    cout << "L2 dirty cnt: " << level2._dirty_cnt << endl;
    cout << "L2 write hit: " << level2._write_hit << endl;
    cout << "L2 replace dirty by write: " << level2._replace_dirty_by_w << endl;
    
//    memory.GetStats(s);
//    printf("Total Memory access time: %dns\n", s.access_time);
    printf("Total Memory access cnt: %d\n", memory._visit_cnt);
    cout << "Miss Rate: " << (1 - (float)hit_cnt / request_cnt) * 100 << "%" << endl;
//    cout << "Miss time: " << request_cnt - hit_cnt << endl;
    return 0;
}
