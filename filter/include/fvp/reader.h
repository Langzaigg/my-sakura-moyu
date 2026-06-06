#ifndef FILTER_FVP_READER_H
#define FILTER_FVP_READER_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class FvpReader {
public:
    FvpReader();
    ~FvpReader();

    int open(const char *path);
    bool hasFile(const char *name) const;
    void close();

private:
    FILE *packageFile;
    uint32_t fileCount;
    char *fileNameBuffer;
    std::vector<std::string> fileNames;

    void cleanup();
};

#endif
