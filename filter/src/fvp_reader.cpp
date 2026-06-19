#include "fvp/reader.h"
#include <cstring>

FvpReader::FvpReader()
    : packageFile(nullptr)
    , fileCount(0)
    , fileNameBuffer(nullptr)
{
}

FvpReader::~FvpReader() {
    cleanup();
}

void FvpReader::cleanup() {
    if (packageFile) {
        fclose(packageFile);
        packageFile = nullptr;
    }
    delete[] fileNameBuffer;
    fileNameBuffer = nullptr;
    fileCount = 0;
    fileNames.clear();
}

void FvpReader::close() {
    cleanup();
}

int FvpReader::open(const char *path) {
    cleanup();

    packageFile = fopen(path, "rb");
    if (!packageFile) return -1;

    uint32_t nameHeaderSize;
    if (fread(&fileCount, 4, 1, packageFile) < 1) { cleanup(); return -2; }
    if (fread(&nameHeaderSize, 4, 1, packageFile) < 1) { cleanup(); return -3; }

    struct RawEntry {
        uint32_t nameOffset, offset, storeSize;
    };

    RawEntry *table = new RawEntry[fileCount];
    if (fread(table, sizeof(RawEntry), fileCount, packageFile) < fileCount) {
        delete[] table; cleanup(); return -4;
    }

    if (nameHeaderSize == 0) {
        delete[] table; cleanup(); return -6;
    }

    fileNameBuffer = new char[nameHeaderSize];
    if (fread(fileNameBuffer, nameHeaderSize, 1, packageFile) < 1) {
        delete[] table; cleanup(); return -5;
    }

    fileNames.reserve(fileCount);
    for (uint32_t i = 0; i < fileCount; i++) {
        if (table[i].nameOffset < nameHeaderSize) {
            fileNames.push_back(fileNameBuffer + table[i].nameOffset);
        }
    }

    delete[] table;
    return 0;
}

bool FvpReader::hasFile(const char *name) const {
    if (!name || *name == '\0') return false;
    for (const auto &n : fileNames) {
        if (n == name) return true;
    }
    return false;
}
