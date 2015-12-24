#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <string.h>

using namespace std;

const int DATA_BLOCK_SIZE = 2048;
const int MAX_DATABLOCKS_COUNT = 1024;
const int MAX_FILES_COUNT = 32;
const char* PATH_TO_LLFSFILE = "/home/addition/LLFS/fs";

struct fileDescriptor {
    bool isFolder;
    bool isEmpty;
    int startingDataBlock;
    size_t dataSize;
    string name;
};

struct dataBlock {
    const char* data;
    int next;
};

int filesCount = 0;
bool isEmptyBlocks[MAX_DATABLOCKS_COUNT];
fileDescriptor fds[MAX_FILES_COUNT];

int getEmptyFileDescriptorNum() {
    for (int i = 0; i < MAX_FILES_COUNT; ++i) {
        if (fds[i].isEmpty) {
            return i;
        }
    }

    return -1;
}

int getEmptyDataBlockNum() {
    for (int i = 0; i < MAX_DATABLOCKS_COUNT; ++i) {
        if (isEmptyBlocks[i]) {
            return i;
        }
    }

    return -1;
}

void init() {
    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "r");

    fread(fds, sizeof(fileDescriptor), MAX_FILES_COUNT, llfsFile);
    fread(isEmptyBlocks, sizeof(bool), MAX_DATABLOCKS_COUNT, llfsFile);

    fclose(llfsFile);
}

void createNew() {
    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "w");

    int* buf = (int*)malloc(sizeof(fileDescriptor));
    for (int i = 0; i < MAX_FILES_COUNT; ++i) {
        fwrite(buf, sizeof(fileDescriptor), 1, llfsFile);
    }
    free(buf);

    buf = (int*)malloc(sizeof(bool));
    for (int i = 0; i < MAX_DATABLOCKS_COUNT; ++i) {
        fwrite(buf, sizeof(bool), 1, llfsFile);
    }
    free(buf);

    buf = (int*)malloc(sizeof(dataBlock));
    for (int i = 0; i < MAX_DATABLOCKS_COUNT; ++i) {
        fwrite(buf, sizeof(dataBlock), 1, llfsFile);
    }
    free(buf);

    fclose(llfsFile);
}

int getFileDescriptor(const char* name) {
    for (int i = 0; i < MAX_FILES_COUNT; ++i) {
        if (fds[i].name == name) {
            return i;
        }
    }

    return -1;
}

void getData(int blockNum, char* data, int handledBlocksNum, FILE* file) {
    size_t offset = sizeof(fileDescriptor) * MAX_FILES_COUNT + sizeof(bool) * MAX_DATABLOCKS_COUNT
                    + sizeof(dataBlock) * (blockNum - 1);

    dataBlock* db = NULL;
    fseek(file, offset, SEEK_SET);
    fread(db, sizeof(dataBlock), 1, file);

    char* buf = (data + handledBlocksNum * DATA_BLOCK_SIZE);
    *buf = *(db->data);

    if (db->next != -1) {
        getData(db->next, data, handledBlocksNum + 1, file);
    }
}

void writeFileDescriptor(fileDescriptor* fd, int position, FILE* file) {
    fseek(file, (position - 1) * sizeof(fileDescriptor), SEEK_SET);
    fwrite(fd, sizeof(fileDescriptor), 1, file);
}

void writeDataBlock(int blockNum, const char* data, FILE* file, size_t cnt, size_t size) {
    size_t offset = sizeof(fileDescriptor) * MAX_FILES_COUNT + sizeof(bool) * MAX_DATABLOCKS_COUNT
                    + sizeof(dataBlock) * (blockNum - 1);
    dataBlock* block = (dataBlock*)malloc(sizeof(dataBlock));
    block->data = data + (size - cnt) / DATA_BLOCK_SIZE;
    block->next = -1;

    cnt -= DATA_BLOCK_SIZE;

    int nBlockNum;
    if (cnt > 0) {
        nBlockNum = getEmptyDataBlockNum();
        isEmptyBlocks[nBlockNum] = false;
        block->next = nBlockNum;
    }

    fseek(file, offset, SEEK_SET);
    fwrite(block, sizeof(dataBlock), 1, file);

    if (cnt > 0) {
        writeDataBlock(nBlockNum, data, file, cnt, size);
    }

    free(block);
}

static int llGetattr(const char* path, struct stat *stbuf) {
    int fdNum = getFileDescriptor(path);
    if (fdNum == -1)
        return -ENOENT;

    fileDescriptor* fd = &fds[fdNum];

    if (fd->isFolder) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = fd->dataSize;
    }

    return 0;
}

static int llReaddir(const char* path, void* buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info* fi) {

}

static int llOpen(const char* path, fuse_file_info* fi) {
    int fd = getFileDescriptor(path);
    if (fd == -1)
        return -ENOENT;
    return 0;
}

static int llRead(const char* path, char* buf, size_t size, off_t offset,
                    fuse_file_info* fi) {
    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "r");
    fileDescriptor* fd = &fds[getFileDescriptor(path)];
    char* data = (char*)malloc(sizeof(char) * (offset + size));
    getData(fd->startingDataBlock, data, 0, llfsFile);
    memcpy(buf, data + offset, size);
    fclose(llfsFile);
    free(data);
    return size;
}

static int llMkdir(const char* path, mode_t mode) {

}

static int llWrite(const char* path, const char* buf, size_t size, off_t offset,
                    fuse_file_info* fi) {
    fileDescriptor* fd;
    int fdNum = getFileDescriptor(path);
    if (fdNum == -1) {
        fdNum = getEmptyFileDescriptorNum();
    }
    fd = &fds[fdNum];
    int emptyBlockNum = getEmptyDataBlockNum();
    isEmptyBlocks[emptyBlockNum] = false;
    fd->isEmpty = false;
    fd->name = path;
    fd->startingDataBlock = emptyBlockNum;
    fd->dataSize = size;

    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "w");
    writeFileDescriptor(fd, fdNum, llfsFile);
    writeDataBlock(emptyBlockNum, buf, llfsFile, size, size);
    fclose(llfsFile);
}

int main(int argc, char *argv[])
{
    struct fuse_operations* oper = (fuse_operations*)malloc(sizeof(fuse_operations));
    oper->getattr = &llGetattr;
    oper->readdir = &llReaddir;
    oper->open = &llOpen;
    oper->read = &llRead;
    oper->mkdir = &llMkdir;
    oper->write = &llWrite;

    return fuse_main(argc, argv, oper, NULL);
}