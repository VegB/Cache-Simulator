/*
  好的这次的目标是优化。可以说是毫无头绪了。
 1、prefetch
     前两次访问这个块的时候，等差数列看下一个在不在这个块里面
     一个地方miss了，就把后面的也load进来，相当于每次load的大一些？
     但是，这个算几次的时间呢？
 2、bypass
     比如连续miss五次之后就开始bypass？？
     -----
     或者存一下嘛，存下来之前被替换过的块，还有之前被bypass过的块。
     如果当前的set是满的，然后访问miss了，而且当前访问的这个块并没有出现在
     -----
     但是呢，这么搞的问题在于，cache满了之后的第一次访问，通通都会被bypass
 3、LFU
     加一个visit_cnt
 4、感觉是要先判断一下访问存储的模式，然后再确定采用什么样的cache策略
 emmmmm根本看不懂自己写了啥【。
 5、突然想到一个问题。之前从下层读进来的时候是不是直接复制了……那么dirty bit之类的岂不是也复制了？？？
     replace的时候有初始化。行吧我还是挺严谨的
 6、bypass也要有时间噢。要算进去的！
 7、就是时间这个事情一直不是很理解。难道可以一边访存一边干其他的事情吗？
 8、在一开始的策略里面，设置好如果要prefetch的话，需要prefetch几块
 9、一开始也要对cache设置好替换策略
 10、还有一个问题，prefetch的时候，怎么让下层也不算那些数啥的
 11、蜜汁seg fault
 12、还有一个问题。loadlinefromlower之前，得判断一下这个块是不是在当前的cache里面吧'
     明白了。bypass的时候，再readfromcache好像会有问题。因为getcacheline的时候有问题。因为没有更新index，我猜。
 13、得到一个line的起始地址！
 14、每次访问就要更新所有valid line里面的recency啥的。
 15、再次访问到同一个块时（hit），更新irr
 16、因为有prefetch，所以有特别多的访问，也有特别多的set操作，这就很僵硬啊。prefetch的时候不更新set吧。 
*/
#include <iostream>
#include "cache.h"
#include "def.h"
using namespace std;

#define INF 10000000
//#define DEBUG
//#define LOG
#define PREFETCH
//#define BYPASS
//#define USE_LIRS

/* asked for 'byte_num' bytes starting from addr */
void Cache::HandleRequest(uint64_t addr, int byte_num, int read_or_write,
                          char *content, int &hit, int &time, int not_prefetch) {
    int lower_hit, lower_time;
    int bypassed = NO;
    time = 0;
    CacheAddress addr_info = SetAddrInfo(addr);
    if(not_prefetch){
        if(_config.replace_policy == LIRS){
            UpdateAllLIRS(addr_info);
        }
    }
    if(read_or_write == READ){
        /* Missed */
        if(!CacheHit(addr_info)){
            hit = NO;
            if(not_prefetch){ // ？？？
                PrefetchStrategy(addr);
            }
            if(!FoundEmptyLine(addr_info) && BypassCondition(addr_info)){
                /* Request the line directly from memory */
                _mem->HandleRequest(addr, byte_num, READ, content, lower_hit, lower_time, YES);
                bypassed = YES;
            }
            else{
                /* Request the line from lower layer */
                LoadLineFromLower(addr, addr_info, lower_hit, lower_time, YES);
            }
            
            if(not_prefetch){
                time += _latency.bus_latency + _latency.hit_latency + lower_time;
                _stats.access_time += _latency.bus_latency + _latency.hit_latency;
            }
        }
        /* Hit */
        else{
            hit = YES;
            if(not_prefetch){
                if(_config.replace_policy == LIRS){
                        UpdateLIRS(addr_info);
                }
            }
            if(not_prefetch){
                time += _latency.bus_latency + _latency.hit_latency;
                _stats.access_time += time;
            }
        }
        if(!bypassed){
            ReadFromCache(addr_info, byte_num, content);
        }
    }
    else if(read_or_write == WRITE){
        /* Hit */
        if(CacheHit(addr_info)){
            hit = YES;
            if(not_prefetch){
                if(_config.replace_policy == LIRS){
                    UpdateLIRS(addr_info);
                }
            }
            if(_config.write_policy == WRITE_THROUGH){
                /* write directly to lower layer */
                _lower->HandleRequest(addr, byte_num, WRITE, content, lower_hit, lower_time, YES);
                
                if(not_prefetch){
                    time += _latency.bus_latency + _latency.hit_latency + lower_time;
                    _stats.access_time += _latency.bus_latency + _latency.hit_latency;
                }
            }
            else if(_config.write_policy == WRITE_BACK){
                if(not_prefetch){
                    time += _latency.bus_latency + _latency.hit_latency;
                    _stats.access_time += time;
                }
            }
            
            WriteToCache(addr_info, byte_num, content);
        }
        /* Missed */
        else{
            hit = NO;
            if(not_prefetch){ // ？？？
                PrefetchStrategy(addr);
            }
            
            if(!FoundEmptyLine(addr_info) && BypassCondition(addr_info)){
                bypassed = YES;
                
                /* Request the line directly from memory */
                _mem->HandleRequest(addr, byte_num, WRITE, content, lower_hit, lower_time, YES);
                
                if(not_prefetch){
                    time += _latency.bus_latency + _latency.hit_latency + lower_time;
                    _stats.access_time += _latency.bus_latency + _latency.hit_latency;
                }
            }
            else{
                if(_config.write_allocate_policy == WRITE_ALLOCATE){
                    /* load and write into current cache */
                    LoadLineFromLower(addr, addr_info, lower_hit, lower_time, YES);
                    WriteToCache(addr_info, byte_num, content);
                    
                    if(not_prefetch){
                        time += _latency.bus_latency + _latency.hit_latency + lower_time;
                        _stats.access_time += _latency.bus_latency + _latency.hit_latency;
                    }
                }
                else if(_config.write_allocate_policy == NO_WRITE_ALLOCATE){
                    /* write to lower layer but not current layer */
                    _lower->HandleRequest(addr, byte_num, WRITE, content, lower_hit, lower_time, YES);
                    
                    if(not_prefetch){
                        time += _latency.bus_latency + _latency.hit_latency;
                        _stats.access_time += time;
                    }
                }
            }
        }
    }
    else{
        cerr << "[Cache]: Invalid operation. Expected 'READ' or 'WRITE'." << endl;
    }
#ifdef DEBUG
    PrintSet(addr_info);
#endif
    
    /* record in visited_tags */
    CacheSet* cache_set = GetCacheSet(addr_info);
    cache_set->visited_tags.insert(addr_info.tag);
    
    /* update visit status */
    if(not_prefetch){
        _total_visit += 1;
        _total_hit += hit;
    }
}

void Cache::SetConfig(CacheConfig cfg){
    _config.size = cfg.size;
    _config.associativity = cfg.associativity;
    _config.line_size = cfg.line_size;
    _config.set_num = (_config.size / _config.associativity) / _config.line_size;
    _config.write_policy = cfg.write_policy;
    _config.write_allocate_policy = cfg.write_allocate_policy;
    _config.replace_policy = cfg.replace_policy;
    _config.prefetch_num = cfg.prefetch_num;
    
    _offset_bit = 0;
    _set_bit = 0;
    int tmp = _config.set_num;
    while(tmp != 1){
        _set_bit += 1;
        tmp >>= 1;
    }
    tmp = _config.line_size;
    while(tmp != 1){
        _offset_bit += 1;
        tmp >>= 1;
    }
    _tag_bit = 64 - _offset_bit - _set_bit;
}

CacheAddress Cache::SetAddrInfo(uint64_t addr){
    CacheAddress addr_info;
    addr_info.offset = (int)getbit(addr, 0, _offset_bit - 1);
    addr_info.set = (int)getbit(addr, _offset_bit, _offset_bit + _set_bit - 1);
    addr_info.tag = getbit(addr, _offset_bit + _set_bit, 63);
#ifdef DEBUG
    cout << "addr: " << dec << addr <<", " <<  hex << addr << endl;
    cout << "tag: " <<hex << addr_info.tag << endl;
    cout << "set: " << hex << addr_info.set << endl;
    cout << "offset: " << hex << addr_info.offset << endl;
#endif
    return addr_info;
}

uint64_t Cache::getbit(uint64_t addr,int s,int e){
    uint64_t mask = 0xffffffffffffffff;
    mask = (mask >> s) << s;
    mask = (mask << (63 - e)) >> (63 - e);
    uint64_t rst = addr & mask;
    rst = rst >> s;
    return rst;
}

/* whether the data is in cache or not.
 if hit, update addr_info.index
 */
int Cache::CacheHit(CacheAddress& addr_info){
    CacheSet* cache_set = GetCacheSet(addr_info);
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        if(line.valid == NO){
            continue;
        }
        if(line.tag == addr_info.tag){
            addr_info.index = i;
            return YES;
        }
    }
    return NO;
}

/* whether there's an empty line in that set or not.
 if there is, update addr_info.index
 */
int Cache::FoundEmptyLine(CacheAddress& addr_info){
    CacheSet* cache_set = GetCacheSet(addr_info);
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        if(line.valid == NO){
            addr_info.index = i;
            return YES;
        }
    }
    return NO;
}

/* find LRU in set and store its index in addr_info.index */
void Cache::FindLRU(CacheAddress& addr_info){
    int pos = 0;
    uint64_t min_stamp = 0xfffffffffffffff;
    CacheSet* cache_set = GetCacheSet(addr_info);
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        if(line.time_stamp < min_stamp){
            min_stamp = line.time_stamp;
            pos = i;
        }
    }
    addr_info.index = pos;
}

/* find LFU in set and store its index in addr_info.index */
void Cache::FindLFU(CacheAddress& addr_info){
    int pos = 0;
    uint64_t min_visit_cnt = 0xfffffffffffffff;
    CacheSet* cache_set = GetCacheSet(addr_info);
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        if(line.visit_cnt < min_visit_cnt){
            min_visit_cnt = line.visit_cnt;
            pos = i;
        }
    }
    addr_info.index = pos;
}

void Cache::FindLIRS(CacheAddress& addr_info){
#ifdef LOG
    cout << "[" << _name << "]: In FindLIRS()" << endl;
#endif
    int pos = 0;
    uint64_t max_IRR = 0;
    uint64_t max_recency = 0;
    CacheSet* cache_set = GetCacheSet(addr_info);
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        if(line.IRR == INF){
            pos = i;
            break;
        }
        if(line.IRR > max_IRR || (line.IRR == max_IRR && line.recency > max_recency)){
            max_IRR = line.IRR;
            max_recency = line.recency;
            pos = i;
        }
    }
    addr_info.index = pos;
}

/* get the starting addr of the content in a line */
uint64_t Cache::GetAddrOfLine(CacheAddress& addr_info){
    return (addr_info.tag << (addr_info.set + addr_info.offset)) |
    (addr_info.set << addr_info.offset);
}

/* store the new line into current cache, might not be replacement */
void Cache::ReplaceLine(CacheAddress& addr_info, char* new_line, int& time){
#ifdef LOG
    cout << "[" << _name << "]: in ReplaceLine()" << endl;
#endif
    CacheLine* line = GetCacheLine(addr_info);

#ifdef LOG
    if(line->valid == YES){
         cout << "[" << _name << "]: Replace " << hex << line->tag << " with " << addr_info.tag << endl;
    }
#endif
    
    /* old_line modified?(Only with WRITE_BACK policy) */
    if(line->valid == YES && (line->modified == YES && _config.write_policy == WRITE_BACK)){
        int lower_time, lower_hit;
        uint64_t old_addr = GetAddrOfLine(addr_info);
        char* old_line = new char[_config.line_size];
        // memcpy((void*)old_line, (const void*)(line->line), _config.line_size);
        _lower->HandleRequest(old_addr, _config.line_size, WRITE, old_line, lower_hit, lower_time, YES);
        delete []old_line;
        time += lower_time;
    }
    
    /* replace with new line, set status */
    InitializeLine(line);
    SetLineValid(line, addr_info);
}

/* fill out certain fields when initialize lines */
void Cache::InitializeLine(CacheLine* line){
    // memcpy((void*)(line->line), new_line, _config.line_size);
    line->valid = NO;
    line->modified = NO;
    if(_config.replace_policy == LIRS){
        line->IRR = INF;
        line->recency = 0;
        line->visited_lines.clear();
    }
}

/* Turn a line from invalid to valid */
void Cache::SetLineValid(CacheLine* line, CacheAddress& addr_info){
    line->valid = YES;
    line->tag = addr_info.tag;
    line->visit_cnt = 1;
}

void Cache::VisitLine(CacheLine* line){
    line->time_stamp = _time_stamp;
    _time_stamp += 1;
    line->visit_cnt += 1;
}

/* read 'byte_num' bytes from cache and stores in 'content' */
void Cache::ReadFromCache(CacheAddress& addr_info, int byte_num, char* content){
    CacheLine* line = GetCacheLine(addr_info);
    // memcpy((void*)content, (const void*)((line->line) + addr_info.offset), byte_num);
    VisitLine(line);
}

/* store 'byte_num' bytes in content into cache */
void Cache::WriteToCache(CacheAddress& addr_info, int byte_num, char* content){
    CacheLine* line = GetCacheLine(addr_info);
    // memcpy((void*)((line->line) + addr_info.offset), (const void*)content, byte_num);
    VisitLine(line);
    line->modified = YES;
}

CacheLine* Cache::GetCacheLine(CacheAddress& addr_info){
    return &(_cache[addr_info.set].cache_lines[addr_info.index]);
}

CacheSet* Cache::GetCacheSet(CacheAddress& addr_info){
    return &(_cache[addr_info.set]);
}

void Cache::BuildCache(){
    /* initialize cache */
    _cache = new CacheSet[_config.set_num];
    for(int i = 0; i < _config.set_num; ++i){
        // _cache[i] is a CacheSet
        _cache[i].cache_lines = new CacheLine[_config.associativity];
        for(int j = 0; j < _config.associativity; ++j){
            // _cache[i].cache_lines[j] is a CacheLine
            _cache[i].cache_lines[j].line = new char[_config.line_size];
            InitializeLine(&(_cache[i].cache_lines[j]));
        }
    }
//    cout << "Cache set up!" << endl;
}

void Cache::ReleaseCache(){
    /* Release Cache */
    for(int i = 0; i < _config.set_num; ++i){
        for(int j = 0; j < _config.associativity; ++j){
            delete []_cache[i].cache_lines[j].line;
        }
        delete []_cache[i].cache_lines;
    }
    delete []_cache;
//    cout << "Cache released!" << endl;
}

void Cache::PrintSet(CacheAddress& addr_info){
    CacheSet* cache_set = GetCacheSet(addr_info);
    string blank = "    ";
    string ans[2] = {"NO", "YES"};
    for(int i = 0; i < _config.associativity; ++i){
        CacheLine line = *((cache_set->cache_lines) + i);
        cout << "Line " << i << ": "<< endl;
        cout << blank << "valid: " << ans[line.valid] << endl;
        cout << blank << "modified: " << ans[line.modified] << endl;
        cout << blank << "tag: " << hex << line.tag << endl;
    }
    cout << "-----------------------" << endl;
}

/* Write back modified blocks to memory before exit */
void Cache::FinalCheck(){
    for(int i = 0; i < _config.set_num; ++i){
        for(int j = 0; j < _config.associativity; ++j){
            if(_cache[i].cache_lines[j].valid == YES && _cache[i].cache_lines[j].modified == YES){
                uint64_t old_addr = (_cache[i].cache_lines[j].tag << (_set_bit + _offset_bit)) | (i << _offset_bit);
                int lower_hit, lower_time;
                char* old_line = new char[_config.line_size];
                // memcpy((void*)old_line, (const void*)(_cache[i].cache_lines[j].line), _config.line_size);
                _lower->HandleRequest(old_addr, _config.line_size, WRITE, old_line, lower_hit, lower_time, NO);
                delete []old_line;
            }
        }
    }
}

/* fetch the blocks that follows current block */
void Cache::PrefetchStrategy(uint64_t addr){
#ifndef PREFETCH
    return;
#endif
#ifdef LOG
    cout << "[" << _name << "]: In Prefetch()" << endl;
#endif
    uint64_t current_addr = addr & ~((1 << _offset_bit) - 1);  // the starting addr of current line addr is in
    int lower_hit, lower_time, i = 0;
    while(i < (_config.prefetch_num >> 1) + 1){
        current_addr += _config.line_size;
        CacheAddress addr_info = SetAddrInfo(current_addr);
        if(!CacheHit(addr_info)){
            LoadLineFromLower(current_addr, addr_info, lower_hit, lower_time, NO);
            i++;
            
            /* record in visited_tags */
            CacheSet* cache_set = GetCacheSet(addr_info);
            cache_set->visited_tags.insert(addr_info.tag);
        }
    }
    current_addr = addr & ~((1 << _offset_bit) - 1);  // the starting addr of current line addr is in
    i = 0;
    while(i < (_config.prefetch_num >> 1) - 1){
        current_addr -= _config.line_size;
        CacheAddress addr_info = SetAddrInfo(current_addr);
        if(!CacheHit(addr_info)){
            LoadLineFromLower(current_addr, addr_info, lower_hit, lower_time, NO);
            i++;
            
            /* record in visited_tags */
            CacheSet* cache_set = GetCacheSet(addr_info);
            cache_set->visited_tags.insert(addr_info.tag);
        }
        //        i++;
    }
}

/* Meet bypass conditions or not */
int Cache::BypassCondition(CacheAddress& addr_info){
#ifndef BYPASS
    return NO;
#endif
#ifdef LOG
    cout << "[" << _name << "]: In BypassCheck()" << endl;
#endif
    if(FoundEmptyLine(addr_info) == YES){
        return NO;
    }
    CacheSet* cache_set = GetCacheSet(addr_info);
    if(cache_set->visited_tags.find(addr_info.tag) != cache_set->visited_tags.end()){  // visited this line before
        return NO;
    }
    return YES;
}

/* Load a line into current cache from lower layer */
void Cache::LoadLineFromLower(uint64_t addr, CacheAddress& addr_info,
                              int& lower_hit, int& lower_time, int not_prefetch){
#ifdef LOG
    cout << "[" << _name << "]: in LoadLineFromLower()"<< endl;
#endif
    char* new_line = new char[_config.line_size];  // used to store the new block returned from lower cache
    uint64_t new_addr = addr & ~((1 << _offset_bit) - 1);  // the starting addr of the line
    _lower->HandleRequest(new_addr, _config.line_size, READ, new_line, lower_hit, lower_time, not_prefetch);
    
    if(!FoundEmptyLine(addr_info)){
        FindReplacement(addr_info);
    }
    ReplaceLine(addr_info, new_line, lower_time);
    
    delete []new_line;
}

void Cache::FindReplacement(CacheAddress& addr_info){
    if(_config.replace_policy == LRU){
        FindLRU(addr_info);
    }
    else if(_config.replace_policy == LFU){
        FindLFU(addr_info);
    }
    else if(_config.replace_policy == LIRS){
        FindLIRS(addr_info);
    }
}

double Cache::AMAT(){
    double miss_rate, hit_rate, ave_time;
    miss_rate = CalculateMissRate();
    hit_rate = 1 - miss_rate;
    double _lower_amat = _lower->AMAT();
    ave_time = hit_rate * _latency.hit_latency + miss_rate * _lower_amat;
//    cout << "ave_time = " << ave_time << endl;
//    cout << "Ave Hit Cost = " << hit_rate << " * " << _latency.hit_latency << " = " << hit_rate * _latency.hit_latency << endl;
//    cout << "Ave Miss Cost = " << miss_rate << " * " << _lower_amat << " = " << miss_rate * _lower_amat << endl;
    return ave_time;
}

/* cache hit and update IRR and recency */
void Cache::UpdateLIRS(CacheAddress& addr_info){
#ifdef LOG
    cout << "[" << _name << "]: Update LIRS after HIT." << endl;
#endif
    CacheLine* line = GetCacheLine(addr_info);
    line->IRR = line->visited_lines.size();
    line->recency = 0;
    line->visited_lines.clear();
}

/* update all cache lines' IRR and recency for a line visit */
void Cache::UpdateAllLIRS(CacheAddress& addr_info){
#ifdef LOG
    cout << "[" << _name << "]: Update all LIRS." << endl;
#endif
    uint64_t old_addr = GetAddrOfLine(addr_info);
    for(int i = 0; i < _config.set_num; ++i){
        for(int j = 0; j < _config.associativity; ++j){
            CacheLine* tmp_line = &(_cache[i].cache_lines[j]);
            if(tmp_line->valid == YES){
                tmp_line->visited_lines.insert(old_addr);
            }
        }
    }
}
