#include <iostream>
#include <fstream>
#include <unordered_map>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <sstream>

using namespace std;

struct Node {
    char ch;
    int freq;
    Node *left, *right;
    Node(char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
};

struct Compare {
    bool operator()(Node* a, Node* b) {
        return a->freq > b->freq;
    }
};

mutex freq_mutex;
unordered_map<char, int> global_freq;

void countFreqChunk(const string& chunk, unordered_map<char, int>& local_freq) {
    for (char ch : chunk) {
        local_freq[ch]++;
    }
}

void mergeFreqs(const unordered_map<char, int>& local_freq) {
    lock_guard<mutex> lock(freq_mutex);
    for (const auto& p : local_freq) {
        global_freq[p.first] += p.second;
    }
}

void generateCodes(Node* root, string code, unordered_map<char, string>& huffmanCode) {
    if (!root) return;
    if (!root->left && !root->right) {
        huffmanCode[root->ch] = code;
    }
    generateCodes(root->left, code + "0", huffmanCode);
    generateCodes(root->right, code + "1", huffmanCode);
}

class ThreadPool {
public:
    ThreadPool(size_t n) : done(false) {
        for (size_t i = 0; i < n; ++i)
            workers.emplace_back(&ThreadPool::worker, this);
    }

    void enqueue(string chunk) {
        {
            unique_lock<mutex> lock(mtx);
            tasks.push(chunk);
        }
        cv.notify_one();
    }

    void shutdown() {
        {
            unique_lock<mutex> lock(mtx);
            done = true;
        }
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

    void setCodeMap(unordered_map<char, string>* map, vector<string>* outVec) {
        codeMap = map;
        outputs = outVec;
    }

private:
    vector<thread> workers;
    queue<string> tasks;
    mutex mtx;
    condition_variable cv;
    bool done;
    unordered_map<char, string>* codeMap = nullptr;
    vector<string>* outputs = nullptr;

    void worker() {
        while (true) {
            string chunk;
            {
                unique_lock<mutex> lock(mtx);
                cv.wait(lock, [&] { return !tasks.empty() || done; });
                if (done && tasks.empty()) return;
                chunk = tasks.front();
                tasks.pop();
            }

            string encoded;
            for (char ch : chunk) {
                encoded += (*codeMap)[ch];
            }

            lock_guard<mutex> lock(freq_mutex);
            outputs->push_back(encoded);
        }
    }
};

int main() {
    ifstream inFile("input.txt");
    ofstream outFile("compressed.huff");

    if (!inFile) {
        cerr << "Failed to open input file.\n";
        return 1;
    }

    string text;
    inFile.seekg(0, ios::end);
    text.resize(inFile.tellg());
    inFile.seekg(0, ios::beg);
    inFile.read(&text[0], text.size());
    inFile.close();

    int numThreads = 4;
    vector<thread> freqThreads;
    int chunkSize = text.size() / numThreads;

    for (int i = 0; i < numThreads; ++i) {
        int start = i * chunkSize;
        int end = (i == numThreads - 1) ? text.size() : (i + 1) * chunkSize;
        unordered_map<char, int> local_freq;
        string chunk = text.substr(start, end - start);
        freqThreads.emplace_back([chunk, &local_freq]() mutable {
            countFreqChunk(chunk, local_freq);
            mergeFreqs(local_freq);
        });
    }

    for (auto& t : freqThreads) t.join();

    priority_queue<Node*, vector<Node*>, Compare> pq;
    for (const auto& p : global_freq) {
        pq.push(new Node(p.first, p.second));
    }

    while (pq.size() > 1) {
        Node* left = pq.top(); pq.pop();
        Node* right = pq.top(); pq.pop();
        Node* merged = new Node('\0', left->freq + right->freq);
        merged->left = left;
        merged->right = right;
        pq.push(merged);
    }

    Node* root = pq.top();
    unordered_map<char, string> huffmanCode;
    generateCodes(root, "", huffmanCode);

    ThreadPool pool(numThreads);
    vector<string> encodedChunks;
    pool.setCodeMap(&huffmanCode, &encodedChunks);

    for (int i = 0; i < numThreads; ++i) {
        int start = i * chunkSize;
        int end = (i == numThreads - 1) ? text.size() : (i + 1) * chunkSize;
        pool.enqueue(text.substr(start, end - start));
    }

    pool.shutdown();

    for (const string& part : encodedChunks) {
        outFile << part;
    }
    outFile.close();

    cout << "Compression complete.\n";
    return 0;
}
