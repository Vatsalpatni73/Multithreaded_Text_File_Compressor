#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

struct Node {
    char ch;
    uint64_t freq;
    unsigned int minByte;
    Node* left;
    Node* right;

    Node(char c, uint64_t f)
        : ch(c), freq(f), minByte(static_cast<unsigned char>(c)), left(nullptr), right(nullptr) {}

    Node(Node* l, Node* r)
        : ch('\0'), freq(l->freq + r->freq), minByte(min(l->minByte, r->minByte)), left(l), right(r) {}
};

// Every tree node owns a unique minimum byte value in its subtree. This makes
// the priority queue order deterministic, including for internal nodes.
struct Compare {
    bool operator()(const Node* a, const Node* b) const {
        if (a->freq != b->freq)
            return a->freq > b->freq;
        return a->minByte > b->minByte;
    }
};

void deleteTree(Node* node) {
    if (!node)
        return;
    deleteTree(node->left);
    deleteTree(node->right);
    delete node;
}

void countFreqChunk(const string& chunk, unordered_map<char, uint64_t>& localFreq) {
    for (char ch : chunk)
        ++localFreq[ch];
}

mutex freqMutex;
unordered_map<char, uint64_t> globalFreq;

void mergeFreqs(const unordered_map<char, uint64_t>& localFreq) {
    lock_guard<mutex> lock(freqMutex);
    for (const auto& entry : localFreq)
        globalFreq[entry.first] += entry.second;
}

void generateCodes(const Node* root, const string& code, unordered_map<char, string>& codes) {
    if (!root)
        return;
    if (!root->left && !root->right) {
        codes[root->ch] = code;
        return;
    }
    generateCodes(root->left, code + '0', codes);
    generateCodes(root->right, code + '1', codes);
}

class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount) : done(false) {
        for (size_t i = 0; i < threadCount; ++i)
            workers.emplace_back(&ThreadPool::worker, this);
    }

    ~ThreadPool() {
        shutdown();
    }

    void setCodeMap(const unordered_map<char, string>* map, vector<string>* output) {
        codeMap = map;
        encodedChunks = output;
    }

    void enqueue(size_t index, string chunk) {
        {
            lock_guard<mutex> lock(taskMutex);
            tasks.push({index, move(chunk)});
        }
        taskAvailable.notify_one();
    }

    void shutdown() {
        {
            lock_guard<mutex> lock(taskMutex);
            if (done)
                return;
            done = true;
        }
        taskAvailable.notify_all();
        for (thread& workerThread : workers) {
            if (workerThread.joinable())
                workerThread.join();
        }
    }

private:
    struct Task {
        size_t index;
        string chunk;
    };

    vector<thread> workers;
    queue<Task> tasks;
    mutex taskMutex;
    mutex outputMutex;
    condition_variable taskAvailable;
    bool done;
    const unordered_map<char, string>* codeMap = nullptr;
    vector<string>* encodedChunks = nullptr;

    void worker() {
        while (true) {
            Task task;
            {
                unique_lock<mutex> lock(taskMutex);
                taskAvailable.wait(lock, [this] { return done || !tasks.empty(); });
                if (done && tasks.empty())
                    return;
                task = move(tasks.front());
                tasks.pop();
            }

            string encoded;
            for (char ch : task.chunk)
                encoded += codeMap->at(ch);

            // Tasks complete out of order, so retain the source chunk index.
            lock_guard<mutex> lock(outputMutex);
            (*encodedChunks)[task.index] = move(encoded);
        }
    }
};

int main() {
    ifstream inFile("input.txt", ios::binary);
    if (!inFile) {
        cerr << "Failed to open input.txt\n";
        return 1;
    }

    const string text((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());
    inFile.close();

    ofstream outFile("compressed.huff", ios::binary);
    if (!outFile) {
        cerr << "Failed to create compressed.huff\n";
        return 1;
    }

    if (text.empty()) {
        outFile << "HUF1\n0\nDATA\n";
        cout << "Compression complete (empty input).\n";
        return 0;
    }

    globalFreq.clear();
    const size_t numThreads = min<size_t>(4, text.size());
    const size_t chunkSize = text.size() / numThreads;
    vector<string> chunks;
    chunks.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        const size_t start = i * chunkSize;
        const size_t length = (i + 1 == numThreads) ? text.size() - start : chunkSize;
        chunks.push_back(text.substr(start, length));
    }

    vector<thread> frequencyThreads;
    for (size_t i = 0; i < numThreads; ++i) {
        frequencyThreads.emplace_back([chunk = chunks[i]] {
            unordered_map<char, uint64_t> localFreq;
            countFreqChunk(chunk, localFreq);
            mergeFreqs(localFreq);
        });
    }
    for (thread& frequencyThread : frequencyThreads)
        frequencyThread.join();

    vector<pair<unsigned char, uint64_t>> frequencies;
    frequencies.reserve(globalFreq.size());
    for (const auto& entry : globalFreq)
        frequencies.emplace_back(static_cast<unsigned char>(entry.first), entry.second);
    sort(frequencies.begin(), frequencies.end());

    priority_queue<Node*, vector<Node*>, Compare> pq;
    for (const auto& entry : frequencies)
        pq.push(new Node(static_cast<char>(entry.first), entry.second));

    while (pq.size() > 1) {
        Node* left = pq.top();
        pq.pop();
        Node* right = pq.top();
        pq.pop();
        pq.push(new Node(left, right));
    }

    Node* root = pq.top();
    unordered_map<char, string> huffmanCode;
    if (!root->left && !root->right)
        huffmanCode[root->ch] = "0";
    else
        generateCodes(root, "", huffmanCode);

    ThreadPool pool(numThreads);
    vector<string> encodedChunks(numThreads);
    pool.setCodeMap(&huffmanCode, &encodedChunks);
    for (size_t i = 0; i < numThreads; ++i)
        pool.enqueue(i, chunks[i]);
    pool.shutdown();

    outFile << "HUF1\n";
    outFile << frequencies.size() << '\n';
    for (const auto& entry : frequencies)
        outFile << static_cast<unsigned int>(entry.first) << ' ' << entry.second << '\n';
    outFile << "DATA\n";
    for (const string& chunk : encodedChunks)
        outFile << chunk;

    if (!outFile) {
        cerr << "Failed while writing compressed.huff\n";
        deleteTree(root);
        return 1;
    }

    deleteTree(root);
    cout << "Compression complete.\n";
    return 0;
}
