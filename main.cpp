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
const char* PATH_TO_LLFSFILE = "/home/taqtaq11/ClionProjects/LLFileSystem/fs";

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

void createNewFS() {
    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "w");

    fileDescriptor* buf = (fileDescriptor*)malloc(sizeof(fileDescriptor));
    for (int i = 0; i < MAX_FILES_COUNT; ++i) {
        fwrite(buf, sizeof(fileDescriptor), 1, llfsFile);
    }
    free(buf);

    bool* bufBool = (bool *)malloc(sizeof(bool));
    for (int i = 0; i < MAX_DATABLOCKS_COUNT; ++i) {
        fwrite(bufBool, sizeof(bool), 1, llfsFile);
    }
    free(bufBool);

    dataBlock* bufInt = (dataBlock*)malloc(sizeof(dataBlock));
    for (int i = 0; i < MAX_DATABLOCKS_COUNT; ++i) {
        fwrite(bufInt, sizeof(dataBlock), 1, llfsFile);
    }
    free(bufInt);

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

char* getDirName(char* path) {
    char* directory;
    char* p = NULL;
    char* ptr = path;
    while (p = *ptr == '/' ? ptr : p, *ptr++ != '\0');
    if ((p - path) != 0) {
        directory = (char*)malloc(sizeof(char) * (p - path));
        strncpy(directory, path, p - path);
        directory[p - path] = '\0';
    }
    else {
        directory = (char*)malloc(sizeof(char) * 2);
        strcpy(directory, "/\0");
    }
    return directory;
}

char* getFileName(char *path) {
    char* filename;
    char* p = NULL;
    char *ptr = path;
    while (p = *ptr == '/' ? ptr : p, *ptr++ != '\0');
    filename = (char*)malloc(sizeof(char) * (ptr - p));
    strncpy(filename, p + 1, ptr - p);
    return filename;
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
    printf("check readdir");
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    return 0;
}

static int llOpen(const char* path, fuse_file_info* fi) {
    printf("check open");
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

    return 0;
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

    return 0;
}

int main(int argc, char *argv[])
{
    createNewFS();
    init();
    for (int i = 0; i < MAX_FILES_COUNT; ++i) {
        printf("name: ");
        printf("%s\n", fds[i].name.c_str());
    }

    struct fuse_operations* oper = (fuse_operations*)malloc(sizeof(fuse_operations));
    oper->getattr = &llGetattr;
    oper->readdir = &llReaddir;
    oper->open = &llOpen;
    oper->read = &llRead;
    oper->mkdir = &llMkdir;
    oper->write = &llWrite;

    return fuse_main(argc, argv, oper, NULL);
}