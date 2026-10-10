#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <chrono>

using namespace std;

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

    vector<int> page_table(NUM_PAGES, -1);
    vector<uint64_t> times_visited(NUM_PAGES, 0);

    int next_free = 0;
    uint64_t cycles = 0;
    uint64_t H = 1469598103934665603ULL;
    uint64_t total_accesses = 0;

    while (getline(file, line)) {
        int vpn = get_vpn(line);

        if (page_table[vpn] == -1) {
            page_table[vpn] = next_free++;
        }
        int frame = page_table[vpn];

        times_visited[vpn]++;
        cycles += 400;
        H ^= frame;
        H *= 1099511628211ULL;
        total_accesses++;
    }

    string base = filename.substr(filename.find_last_of('/') + 1);
    base = base.substr(0, base.find_last_of('.'));
    string dir = "../results/part1/";
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

    cout << "PT Entries Allocated \t : \t" << pt_entries << endl;
    cout << "Page Table Size \t : \t" << pt_bytes << " bytes (" << pt_bytes / 1024.0 << " KB)" << endl;
    cout << "Total Accesses \t\t : \t" << total_accesses << endl;
    cout << "Frames Used \t\t : \t" << next_free << endl;
    cout << "Total Cycles \t\t : \t" << cycles << endl;
    cout << "Page Table Utilisation \t : \t" << util << "%" << endl;
    cout << "Hash \t\t\t : \t" << H << endl;
    cout << "Simulation Time \t : \t" << sim_seconds << " s" << endl;

    return 0;
}