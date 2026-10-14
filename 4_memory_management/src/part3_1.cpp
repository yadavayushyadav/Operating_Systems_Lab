#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <chrono>

using namespace std;

struct TLBEntry {
    int vpn;
    int frame;
    uint64_t timestamp;
};

int get_vpn(const string & line) {
    long long temp = atoll(line.c_str());
    return (temp % 4294967296LL) / 4096;
}

int main(int argc, char * argv[]) {
    auto t_start = chrono::steady_clock::now();
    string filename;
    if (argc == 1) {
        cout << "No filename passed, using blender_memoryTrace.txt as default. Make sure to pass the filename as filename=blender_memoryTrace.txt" << endl << endl;
        filename = "../data/blender_memoryTrace.txt";
    }
    else filename = argv[1];

    ifstream file(filename);
    if (!file) {
        cerr << filename << " does not exist" << endl;
        return 1;
    }

    string line;
    getline(file, line);

    const int NUM_PAGES = 1 << 20;
    const int TLB_SIZE = 1 << 7;

    vector<int> page_table(NUM_PAGES, -1);
    vector<uint64_t> times_visited(NUM_PAGES, 0);
    vector<TLBEntry> tlb(TLB_SIZE, TLBEntry{-1, -1, 0});
    vector<long long> tlb_contains(NUM_PAGES, -1);

    int next_free = 0;
    uint64_t cycles = 0;
    uint64_t H = 1469598103934665603ULL;
    uint64_t total_accesses = 0;
    uint64_t total_tlb_hits = 0;
    uint64_t timestamp = 1;

    while (getline(file, line)) {
        int vpn = get_vpn(line);
        int frame = -1;

        uint64_t temp = UINT64_MAX;
        int lru_index = -1;

        if (tlb_contains[vpn] != -1) {
            cycles += 201;
            total_tlb_hits++;
            tlb[tlb_contains[vpn]].timestamp = timestamp++;
            frame = tlb[tlb_contains[vpn]].frame;
        }
        else {
            for (int i = 0; i < TLB_SIZE; i++) {
                if (tlb[i].timestamp < temp) {
                    temp = tlb[i].timestamp;
                    lru_index = i;
                }
            }
            if (page_table[vpn] == -1) {
                page_table[vpn] = next_free++;
            }
            if (tlb[lru_index].vpn != -1) tlb_contains[tlb[lru_index].vpn] = -1;
            frame = page_table[vpn];
            tlb[lru_index].vpn = vpn;
            tlb[lru_index].frame = frame;
            tlb[lru_index].timestamp = timestamp++;
            cycles += 401;
            tlb_contains[vpn] = lru_index;
        }

        times_visited[vpn]++;
        H ^= frame;
        H *= 1099511628211ULL;
        total_accesses++;
    }

    string base = filename.substr(filename.find_last_of('/') + 1);
    base = base.substr(0, base.find_last_of('.'));
    string dir = "../results/part3/single_level_tlb/";
    filesystem::create_directories(dir);
    string csv = dir + base + "_pagecounts.csv";
    filesystem::remove(csv);
    ofstream out(csv);
    out << "vpn,count\n";
    for (int i = 0; i < NUM_PAGES; i++) if (times_visited[i] != 0) out << i << "," << times_visited[i] << "\n";

    auto t_end = chrono::steady_clock::now();
    double sim_seconds = chrono::duration<double>(t_end - t_start).count();

    uint64_t pt_entries = NUM_PAGES;
    uint64_t pt_bytes   = pt_entries * sizeof(int);
    double util = 100.0 * next_free / pt_entries;

    double hit_rate = 100.0 * total_tlb_hits / total_accesses;

    cout << "PT Entries Allocated \t : \t" << pt_entries << endl;
    cout << "Page Table Size \t : \t" << pt_bytes << " bytes (" << pt_bytes / 1024.0 << " KB)" << endl;
    cout << "Total Accesses \t\t : \t" << total_accesses << endl;
    cout << "Frames Used \t\t : \t" << next_free << endl;
    cout << "Total Cycles \t\t : \t" << cycles << endl;
    cout << "Page Table Utilisation \t : \t" << util << "%" << endl;
    cout << "Total TLB Hits \t\t : \t" << total_tlb_hits << endl;
    cout << "Total TLB Misses \t : \t" << total_accesses - total_tlb_hits << endl;
    cout << "TLB Hit Rate \t\t : \t" << hit_rate << "%" << endl;
    cout << "Hash \t\t\t : \t" << H << endl;
    cout << "Simulation Time \t : \t" << sim_seconds << " s" << endl;

    return 0;
}