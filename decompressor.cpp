#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <queue>
#include <string>
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

// Must exactly match the compressor's ordering, including internal nodes.
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

int fail(Node* root, const string& message) {
    deleteTree(root);
    cerr << message << '\n';
    return 1;
}

int main() {
    ifstream inFile("compressed.huff", ios::binary);
    if (!inFile) {
        cerr << "Failed to open compressed.huff\n";
        return 1;
    }

    string magic;
    size_t distinctChars = 0;
    if (!(inFile >> magic >> distinctChars) || magic != "HUF1" || distinctChars > 256) {
        cerr << "Invalid compressed file header\n";
        return 1;
    }

    vector<pair<unsigned char, uint64_t>> frequencies;
    frequencies.reserve(distinctChars);
    bool seen[256] = {};
    uint64_t expectedCharacters = 0;

    for (size_t i = 0; i < distinctChars; ++i) {
        unsigned int byteValue;
        uint64_t frequency;
        if (!(inFile >> byteValue >> frequency) || byteValue > 255 || frequency == 0 || seen[byteValue]) {
            cerr << "Invalid frequency table\n";
            return 1;
        }
        seen[byteValue] = true;
        frequencies.emplace_back(static_cast<unsigned char>(byteValue), frequency);
        expectedCharacters += frequency;
    }

    string marker;
    if (!(inFile >> marker) || marker != "DATA") {
        cerr << "Invalid compressed data marker\n";
        return 1;
    }
    if (inFile.get() != '\n') {
        cerr << "Invalid compressed file: expected newline after DATA\n";
        return 1;
    }

    const string bits((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());
    inFile.close();

    if (distinctChars == 0) {
        if (!bits.empty()) {
            cerr << "Invalid compressed data for empty input\n";
            return 1;
        }
        ofstream outFile("decompressed.txt", ios::binary);
        if (!outFile) {
            cerr << "Failed to create decompressed.txt\n";
            return 1;
        }
        cout << "Decompression complete (empty input).\n";
        return 0;
    }

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

    ofstream outFile("decompressed.txt", ios::binary);
    if (!outFile)
        return fail(root, "Failed to create decompressed.txt");

    uint64_t written = 0;
    if (!root->left && !root->right) {
        for (char bit : bits) {
            if (bit != '0') {
                outFile.close();
                return fail(root, "Invalid bit in single-character data");
            }
            outFile.put(root->ch);
            ++written;
        }
    } else {
        Node* current = root;
        for (char bit : bits) {
            if (bit == '0')
                current = current->left;
            else if (bit == '1')
                current = current->right;
            else {
                outFile.close();
                return fail(root, "Invalid bit in compressed data");
            }

            if (!current) {
                outFile.close();
                return fail(root, "Invalid Huffman tree traversal");
            }
            if (!current->left && !current->right) {
                outFile.put(current->ch);
                ++written;
                current = root;
            }
        }
        if (current != root) {
            outFile.close();
            return fail(root, "Compressed data ends in the middle of a code");
        }
    }

    if (written != expectedCharacters) {
        outFile.close();
        return fail(root, "Decoded length does not match the frequency table");
    }
    if (!outFile) {
        outFile.close();
        return fail(root, "Failed while writing decompressed.txt");
    }

    outFile.close();
    deleteTree(root);
    cout << "Decompression complete. Output written to decompressed.txt\n";
    return 0;
}
