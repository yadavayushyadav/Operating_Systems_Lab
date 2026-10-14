#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <list>
#include <cstdint>
#include <chrono>

using namespace std;

// Part IV with a single-level page table and a TLB: physical memory is smaller than virtual memory,
// so when all frames are in use a page fault replaces the least recently used (LRU) page. Every access
// costs the same as in Part III, and every replacement adds one disk access. Filling the frames that
// are free at the start costs nothing.

struct TLBEntry {
    int vpn;
    int frame;
    uint64_t timestamp;
};

int get_vpn(const string & line) {
    long long temp = atoll(line.c_str());
    return (temp % 4294967296LL) / 4096;
}

// Physical memory size as a fraction of the virtual memory, written as "0.25" or "1/1024".
double parse_fraction(const string & text) {
    size_t slash = text.find('/');
    if (slash == string::npos) return atof(text.c_str());
    double denominator = atof(text.c_str() + slash + 1);
    return denominator > 0 ? atof(text.substr(0, slash).c_str()) / denominator : 0;
}

int main(int argc, char * argv[]) {
    auto t_start = chrono::steady_clock::now();
    if (argc < 2 || argc > 3) {
        cerr << "Usage: " << argv[0] << " [trace file] <fraction>, e.g. make sim_part4_3 filename=gcc_memoryTrace.txt fraction=1/1024" << endl;
        return 1;
    }
    string filename, fraction_text;
    if (argc == 2) {
        cout << "No filename passed, using blender_memoryTrace.txt as default. Make sure to pass the filename as filename=blender_memoryTrace.txt" << endl << endl;
        filename = "../data/blender_memoryTrace.txt";
        fraction_text = argv[1];
    }
    else {
        filename = argv[1];
        fraction_text = argv[2];
    }

    const int NUM_PAGES = 1 << 20;
    const int TLB_SIZE = 1 << 7;
    const uint64_t DISK_ACCESS_CYCLES = 20000000;

    double fraction = parse_fraction(fraction_text);
    long long frames = (long long) (fraction * NUM_PAGES + 1e-9);
    if (!(fraction > 0) || frames < 1) {
        cerr << "Invalid fraction " << fraction_text << ": physical memory must have at least one frame (fraction >= 1/" << NUM_PAGES << ")" << endl;
        return 1;
    }
    const int NUM_FRAMES = frames < NUM_PAGES ? frames : NUM_PAGES;

    ifstream file(filename);
    if (!file) {
        cerr << filename << " does not exist" << endl;
        return 1;
    }

    string line;
    getline(file, line);

    vector<int> page_table(NUM_PAGES, -1);              // frame holding each page, -1 if the page is not in memory
    vector<int> frame_to_vpn(NUM_FRAMES, -1);           // page held by each frame
    list<int> lru;                                      // frames in use, most recently used at the front
    vector<list<int>::iterator> lru_position(NUM_FRAMES);
    vector<TLBEntry> tlb(TLB_SIZE, TLBEntry{-1, -1, 0});
    vector<long long> tlb_contains(NUM_PAGES, -1);

    int next_free = 0;
    uint64_t cycles = 0;
    uint64_t H = 1469598103934665603ULL;
    uint64_t total_accesses = 0;
    uint64_t total_tlb_hits = 0;
    uint64_t page_faults = 0;
    uint64_t replacements = 0;
    uint64_t timestamp = 1;

    while (getline(file, line)) {
        int vpn = get_vpn(line);
        int frame = -1;

        if (tlb_contains[vpn] != -1) {
            cycles += 201;
            total_tlb_hits++;
            tlb[tlb_contains[vpn]].timestamp = timestamp++;
            frame = tlb[tlb_contains[vpn]].frame;
        }
        else {
            frame = page_table[vpn];
            if (frame == -1) {
                page_faults++;
                if (next_free < NUM_FRAMES) {
                    frame = next_free++;
                }
                else {
                    frame = lru.back();
                    lru.pop_back();
                    int old_vpn = frame_to_vpn[frame];
                    page_table[old_vpn] = -1;
                    if (tlb_contains[old_vpn] != -1) {      // the replaced page must not stay in the TLB
                        tlb[tlb_contains[old_vpn]] = TLBEntry{-1, -1, 0};
                        tlb_contains[old_vpn] = -1;
                    }
                    replacements++;
                    cycles += DISK_ACCESS_CYCLES;
                }
                page_table[vpn] = frame;
                frame_to_vpn[frame] = vpn;
                lru.push_front(frame);
                lru_position[frame] = lru.begin();
            }

            uint64_t temp = UINT64_MAX;
            int lru_index = -1;
            for (int i = 0; i < TLB_SIZE; i++) {
                if (tlb[i].timestamp < temp) {
                    temp = tlb[i].timestamp;
                    lru_index = i;
                }
            }
            if (tlb[lru_index].vpn != -1) tlb_contains[tlb[lru_index].vpn] = -1;
            tlb[lru_index].vpn = vpn;
            tlb[lru_index].frame = frame;
            tlb[lru_index].timestamp = timestamp++;
            cycles += 401;
            tlb_contains[vpn] = lru_index;
        }

        if (lru_position[frame] != lru.begin()) lru.splice(lru.begin(), lru, lru_position[frame]);

        H ^= frame;
        H *= 1099511628211ULL;
        total_accesses++;
    }

    auto t_end = chrono::steady_clock::now();
    double sim_seconds = chrono::duration<double>(t_end - t_start).count();

    double fault_rate = 100.0 * page_faults / total_accesses;
    double replacement_rate = 100.0 * replacements / total_accesses;
    double hit_rate = 100.0 * total_tlb_hits / total_accesses;

    cout << "Physical Frames \t : \t" << NUM_FRAMES << " (" << NUM_FRAMES * 4ULL << " KB, fraction " << fraction_text << ")" << endl;
    cout << "Total Accesses \t\t : \t" << total_accesses << endl;
    cout << "Frames Used \t\t : \t" << next_free << endl;
    cout << "Page Faults \t\t : \t" << page_faults << endl;
    cout << "Page Fault Rate \t : \t" << fault_rate << "%" << endl;
    cout << "Page Replacements \t : \t" << replacements << endl;
    cout << "Replacement Rate \t : \t" << replacement_rate << "%" << endl;
    cout << "Total TLB Hits \t\t : \t" << total_tlb_hits << endl;
    cout << "Total TLB Misses \t : \t" << total_accesses - total_tlb_hits << endl;
    cout << "TLB Hit Rate \t\t : \t" << hit_rate << "%" << endl;
    cout << "Total Cycles \t\t : \t" << cycles << endl;
    cout << "Hash \t\t\t : \t" << H << endl;
    cout << "Simulation Time \t : \t" << sim_seconds << " s" << endl;

    return 0;
}
