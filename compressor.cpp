#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include "compressor.h"
#include "huffman.h"

using namespace std;

mutex queueMutex;
condition_variable cv;
queue<pair<int, string>> chunkQueue;
vector<string> compressedChunks;
atomic<int> chunksProcessed(0);
int totalChunks = 0;

// string huffmanCompress(const string& input) {
//     string encoded;
//     int count = 1;
//     for (size_t i = 1; i <= input.size(); ++i) {
//         if (i < input.size() && input[i] == input[i - 1]) {
//             ++count;
//         } else {
//             encoded += input[i - 1];
//             encoded += to_string(count);
//             count = 1;
//         }
//     }
//     return encoded;
// }

void workerThread() {
    while (true) {
        pair<int, string> chunk;
        {
            unique_lock<mutex> lock(queueMutex);
            cv.wait(lock, [] { return !chunkQueue.empty() || chunksProcessed == totalChunks; });

            if (chunkQueue.empty()) return;

            chunk = chunkQueue.front();
            chunkQueue.pop();
        }

        string compressed = huffmanCompress(chunk.second);
        compressedChunks[chunk.first] = compressed;
        chunksProcessed++;
        cv.notify_all();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./compressor <input_file> <output_file>\n";
        return 1;
    }

    ifstream inFile(argv[1], ios::binary);
    if (!inFile) {
        cerr << "Failed to open input file.\n";
        return 1;
    }

    string buffer;
    int chunkId = 0;

    // Read input in chunks
    while (!inFile.eof()) {
        buffer.resize(CHUNK_SIZE);
        inFile.read(&buffer[0], CHUNK_SIZE);
        streamsize bytesRead = inFile.gcount();
        if (bytesRead == 0) break;
        buffer.resize(bytesRead);

        {
            lock_guard<mutex> lock(queueMutex);
            chunkQueue.emplace(chunkId, buffer);
        }
        ++chunkId;
        buffer.clear();
    }

    totalChunks = chunkId;
    compressedChunks.resize(totalChunks);

    vector<thread> workers;
    for (int i = 0; i < THREAD_COUNT; ++i) {
        workers.emplace_back(workerThread);
    }

    cv.notify_all();

    for (auto& t : workers) {
        t.join();
    }

    ofstream outFile(argv[2], ios::binary);
    if (!outFile) {
        cerr << "Failed to open output file.\n";
        return 1;
    }

    for (const auto& chunk : compressedChunks) {
        outFile.write(chunk.data(), chunk.size());
    }

    cout << "Compression complete. Wrote to " << argv[2] << "\n";
    return 0;
}