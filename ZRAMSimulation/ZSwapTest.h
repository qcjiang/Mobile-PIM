#ifndef _COMPRESSION_ALGORITHMS_H_
#define _COMPRESSION_ALGORITHMS_H_

#include <iostream>
#include <string.h>
#include <vector>
#include "lzo/minilzo.h"
#include "lz4/lz4.h"

#define PAGE_SIZE 4096

class PageMetadata {
public:
    int pfn;
    int uid;
    unsigned long page_size = PAGE_SIZE;
    unsigned long compressed_size = 0;
    PageMetadata(int pfn, int uid) : pfn(pfn), uid(uid) {}
    PageMetadata(int pfn, int uid, unsigned long page_size) : pfn(pfn), uid(uid), page_size(page_size) {}
    ~PageMetadata() {}
    void setCompressedSize(unsigned long compressed_size) {this->compressed_size = compressed_size;}
};

class DataPage {
public:
    PageMetadata meta;
    char *buf;
    DataPage(int pfn, int uid) : meta(pfn, uid) {
        this->buf = (char *) malloc(this->meta.page_size);
    }
    DataPage(int pfn, int uid, unsigned long page_size) : meta(pfn, uid, page_size) {
        this->buf = (char *) malloc(this->meta.page_size);
    }
    ~DataPage() {
        free(this->buf);
    }
};


class CompressionAlgorithm {
public:
    virtual DataPage *CompressDataPage(DataPage *dataPage) = 0;
    virtual DataPage *DecompressDataPage(DataPage *compressedPage) = 0;
};

class LZOCompressionAlgorithm : public CompressionAlgorithm {
private:
    char *wrkmem;
public:
    LZOCompressionAlgorithm() {
        this->wrkmem = (char *) malloc(LZO1X_1_MEM_COMPRESS + (sizeof(lzo_align_t) - 1));
    }
    ~LZOCompressionAlgorithm() {
        free(this->wrkmem);
    }

    DataPage *CompressDataPage(DataPage *dataPage) {
        unsigned long max_compr_len = dataPage->meta.page_size + dataPage->meta.page_size / 16 + 64 + 3; // LZO specific
        DataPage *compressedPage = new DataPage(dataPage->meta.pfn, dataPage->meta.uid, max_compr_len);

        int r = lzo1x_1_compress((const unsigned char *) dataPage->buf, dataPage->meta.page_size, (unsigned char *) compressedPage->buf, &max_compr_len, (void *) this->wrkmem);
        if(r != LZO_E_OK) {
            std::cout << "Error LZO compressing data" << std::endl;
            delete compressedPage;
            return nullptr;
        }

        compressedPage->meta.setCompressedSize(max_compr_len);
        return compressedPage;
    }

    DataPage *DecompressDataPage(DataPage *compressedPage) {
        DataPage *dataPage = new DataPage(compressedPage->meta.pfn, compressedPage->meta.uid);

        int r = lzo1x_decompress( (const unsigned char *) compressedPage->buf, compressedPage->meta.compressed_size, (unsigned char *) dataPage->buf, &dataPage->meta.page_size, NULL);
        if(r != LZO_E_OK) {
            std::cout << "Error LZO decompressing data" << std::endl;
            delete dataPage;
            return nullptr;
        }

        return dataPage;
    }
};

class LZ4CompressionAlgorithm : public CompressionAlgorithm {
public:

    DataPage *CompressDataPage(DataPage *dataPage) {
        unsigned long max_compr_len = (unsigned long) LZ4_compressBound(dataPage->meta.page_size); // lz4 specific
        DataPage *compressedPage = new DataPage(dataPage->meta.pfn, dataPage->meta.uid, max_compr_len);

        int new_len = LZ4_compress_default((const char*) dataPage->buf, (char*) compressedPage->buf, (int) dataPage->meta.page_size, (int) max_compr_len);
        if(new_len <= 0) {
            std::cout << "Error LZ4 compressing data" << std::endl;
            delete compressedPage;
            return nullptr;
        }

        compressedPage->meta.setCompressedSize(new_len);
        return compressedPage;
    }


    DataPage *DecompressDataPage(DataPage *compressedPage) {
        DataPage *dataPage = new DataPage(compressedPage->meta.pfn, compressedPage->meta.uid);

        int new_len = LZ4_decompress_safe((const char*) compressedPage->buf, (char*) dataPage->buf, (int) compressedPage->meta.compressed_size, (int) dataPage->meta.page_size);
        if(new_len <= 0) {
            std::cout << "Error LZ4 decompressing data" << std::endl;
            delete dataPage;
            return nullptr;
        }

        return dataPage;
    }

};

class ZRamBufferSlot {
private:
    char *buf;
    unsigned long size;
    unsigned long offset;
    std::vector<PageMetadata *> metainfo;
public:
    ZRamBufferSlot(unsigned long size) {
        this->size = size;
        this->buf = (char *) malloc(this->size);
        this->offset = 0;
    }

    ~ZRamBufferSlot() {
        free(this->buf);
    }

    int getStoredPageCnt() {
        return this->metainfo.size();
    }

    bool InsertCompressedDataPage(DataPage *compressedPage) {
        // check if there is enough space in the buffer
        if(this->offset + compressedPage->meta.compressed_size > this->size) {
            return false;
        }

        // copy compressed page to buffer
        memcpy(this->buf + this->offset, compressedPage->buf, compressedPage->meta.compressed_size);
        this->offset += compressedPage->meta.compressed_size;

        // add meta info for searching based on pfn
        PageMetadata *pageMetadata = new PageMetadata(compressedPage->meta.pfn, compressedPage->meta.uid);
        pageMetadata->setCompressedSize(compressedPage->meta.compressed_size);
        this->metainfo.push_back(pageMetadata);

        return true;
    }

    DataPage *GetCompressedDataPage(int pfn) {
        DataPage *compressedPage;
        unsigned long data_offset = 0;
        for(int i = 0; i < this->metainfo.size(); i++) {
            if(this->metainfo[i]->pfn == pfn) {
                compressedPage = new DataPage(this->metainfo[i]->pfn, this->metainfo[i]->uid, this->metainfo[i]->compressed_size);
                memcpy(compressedPage->buf, this->buf + data_offset, this->metainfo[i]->compressed_size);
                compressedPage->meta.setCompressedSize(this->metainfo[i]->compressed_size);
                return compressedPage;
            }
            data_offset += this->metainfo[i]->compressed_size;
        }
        return nullptr;
    }

};

class SwapoutTraceReader {
private:
    FILE *traceFile;
    FILE *dataFile;
    std::vector<DataPage *> dataPages;
public:
    SwapoutTraceReader(const char *traceFilename, const char *dataFilename) {
        this->traceFile = fopen(traceFilename, "r");
        this->dataFile = fopen(dataFilename, "rb+");
        if(this->traceFile == nullptr || this->dataFile == nullptr) {
            std::cout << "Error opening trace file " << traceFilename << std::endl;
            exit(1);
        }
    }

    ~SwapoutTraceReader() {
        for(int i = 0; i < this->dataPages.size(); i++) {
            delete this->dataPages[i];
        }
        fclose(this->traceFile);
        fclose(this->dataFile);
    }

    int LoadTrace() {
        if(this->dataPages.size() > 0) {
            std::cout << "Error: data pages already loaded" << std::endl;
            return -1;
        }

        char *buffer = (char *) malloc(PAGE_SIZE);
        if(buffer == nullptr) {
            std::cout << "Error allocating buffer" << std::endl;
            return -1;
        }

        unsigned long line_num = 1;
        while(true) {

            int uid, pfn;
            int r = fscanf(this->traceFile, "%d,%d\n", &uid, &pfn);
            if(r != 2) {
                break;
            }

            int r2 = fread(buffer, PAGE_SIZE, 1, this->dataFile);
            if(r2 != 1) {
                std::cout << "Error reading data file, r2 = " << r2 << ", line_num = " << line_num << ", r1 = " << r << std::endl;
                free(buffer);
                return -1;
            }

            DataPage *dataPage = new DataPage(pfn, uid);
            memcpy(dataPage->buf, buffer, PAGE_SIZE);
            this->dataPages.push_back(dataPage);
            line_num++;
        }
        free(buffer);
        return 0;
    }

    std::vector<DataPage *> GetAllDataPages() {
        return this->dataPages;
    }

};

class SwapinTraceReader {
private:
    FILE *replayFile;
    std::vector<PageMetadata *> ops_records;
public:
    SwapinTraceReader(const char *replayFile) {
        this->replayFile = fopen(replayFile, "r");
        if(this->replayFile == nullptr) {
            std::cout << "Error opening trace file " << replayFile << std::endl;
            exit(1);
        }
    }

    ~SwapinTraceReader() {
        for(int i = 0; i < this->ops_records.size(); i++) {
            delete this->ops_records[i];
        }
        fclose(this->replayFile);
    }

    int LoadTrace() {
        if(this->ops_records.size() > 0) {
            std::cout << "Error: tracing records already loaded" << std::endl;
            return -1;
        }
        unsigned long line_num = 1;
        while(true) {
            int uid, pfn;
            int r = fscanf(this->replayFile, "%d,%d\n", &uid, &pfn);
            // std::cout << "line_num = " << line_num << ", r = " << r << std::endl;
            if(r != 2) {
                break;
            }
            PageMetadata *record = new PageMetadata(pfn, uid);
            this->ops_records.push_back(record);
            line_num++;
        }
        return 0;
    }

    std::vector<PageMetadata *> GetOperationRecords() {
        return this->ops_records;
    }

};


#endif