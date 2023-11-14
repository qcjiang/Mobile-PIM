#include <iostream>
#include <map>
#include <vector>
#include <unistd.h>
#include "ZSwapTest.h"
#include "../../zsim_hooks.h"
#define DEFAULT_SLOT_SIZE 4 * 1024 // 4 KB

char *data_filename;
char *trace_filename;
char *replay_filename;
CompressionAlgorithm *compr_alg = NULL;
unsigned long slot_size = DEFAULT_SLOT_SIZE;

// parse the input arguments
int parse_args(int argc, char *argv[])
{
    int opt;
    while((opt = getopt(argc, argv, "c:s:f:t:r:")) != -1) {
        switch(opt) {
            case 'c':
                if(strcmp(optarg, "lzo") == 0) {
                    compr_alg = new LZOCompressionAlgorithm();
                } else if(strcmp(optarg, "lz4") == 0) {
                    compr_alg = new LZ4CompressionAlgorithm();
                } else {
                    std::cout << "Error: Cannot obtain compression algorithm: " << optarg << std::endl;
                    exit(1);
                }
                break;
            case 's':
                slot_size = atoi(optarg) * 1024; // set buffer size to optarg (unit: kb)
                break;
            case 'f':
                data_filename = optarg; // set data filename to optarg
                break;
            case 't':
                trace_filename = optarg; // set trace filename to optarg
                break;
            case 'r':
                replay_filename = optarg; // set replay filename to optarg
                break;
            default:
                printf("Usage: %s [-c] [-s] [-b] [-f filename]\n", argv[0]);
                exit(1);
        }
    }
    if(compr_alg == NULL) { // use LZO as default compression algorithm
        compr_alg = new LZOCompressionAlgorithm();
    }
    return 0;
}

int prepare_read_evaluation_env(std::map<int, ZRamBufferSlot *>& slot_tree, std::vector<ZRamBufferSlot *>& slot_list)
{
    int r;
    SwapoutTraceReader traceReader(trace_filename, data_filename);

    r = traceReader.LoadTrace(); // load trace from the trace file
    if(r != 0) {
        printf("Error loading trace\n");
        return -1;
    }

    std::vector<DataPage *> dataPages = traceReader.GetAllDataPages(); // get all the data pages from the trace file
    std::cout<< "Total Page Size is : " << dataPages.size() << std::endl;
    
    ZRamBufferSlot *current_slot = new ZRamBufferSlot(slot_size);
    for(int i = 0; i < dataPages.size(); i++) {
        DataPage *dataPage = dataPages[i];
        
        DataPage *compressedPage = compr_alg->CompressDataPage(dataPage);  // compress the data page
        if(compressedPage == nullptr) {
            std::cout << "Error: cannot compress data page" << std::endl;
            return -1;
        }

        bool ret = current_slot->InsertCompressedDataPage(compressedPage); // the function tries to insert the compressed page into the slot
        if(ret == false) { 
            slot_list.push_back(current_slot);
            current_slot = new ZRamBufferSlot(slot_size); // if the current slot is full, we need to create a new slot
        } else {
            slot_tree[dataPage->meta.pfn] = current_slot; // if the pfn is already in the tree, update it but not delete it because other pfn would share the same slot
        }

        delete compressedPage;
    }

    if(current_slot->getStoredPageCnt() > 0) {
        slot_list.push_back(current_slot);
    }

    std::cout << "total slot cnt : " << slot_list.size() << std::endl;
    std::cout << "last slot size is : " << current_slot->getStoredPageCnt() << std::endl;

    return 0;
}

int main(int argc, char *argv[])
{
    int r;
    std::map<int, ZRamBufferSlot *> slot_tree; // we use the pfn of a page as the key to find the corresponding slot in this tree
    std::vector<ZRamBufferSlot *> slot_list; // store all the slots

    parse_args(argc, argv);

    // this function will read and compress data pages from the trace file, and store them into slot_tree with the pfn as the key
    r = prepare_read_evaluation_env(slot_tree, slot_list); // prepare read test environment
    if(r != 0) {
        printf("Error preparing read evaluation environment\n");
        return 1;
    }


    // Step 1. load trace from the replay trace file
    SwapinTraceReader swapinTraceReader(replay_filename);

    r = swapinTraceReader.LoadTrace(); // load trace from the trace file
    if(r != 0) {
        printf("Error loading swapin trace\n");
        return 1;
    }

    // Step 2.  obtain all operation records from the trace file
    std::vector<PageMetadata *> swapinDataPages = swapinTraceReader.GetOperationRecords();

    // Step 3. decompress the data pages based on the operation records
    zsim_begin();
    for(int i = 0; i < swapinDataPages.size(); i++) {
        PageMetadata *pageMetadata = swapinDataPages[i];
        if(slot_tree.find(pageMetadata->pfn) == slot_tree.end()) { // if the pfn is not in the tree, we just skip it
            continue;
        }
        ZRamBufferSlot *slot = slot_tree[pageMetadata->pfn]; // find the slot based on the pfn
        DataPage *compressedPage = slot->GetCompressedDataPage(pageMetadata->pfn); // get the compressed page from the slot
        if(compressedPage == nullptr) {
            std::cout << "Error: cannot find the page in the slot" << std::endl;
            return 1;
        }
        DataPage *dataPage = compr_alg->DecompressDataPage(compressedPage); // decompress the page
        if(dataPage == nullptr) {
            std::cout << "Error: cannot decompress the page" << std::endl;
            return 1;
        }
    }
    zsim_end();
    std::cout << "Finished!" << std::endl;
    

    // we need to release resources in some steps ... but when the program exits, the OS will do it for us ... 
    // we just make sure that there is not memory leak during the execution of the program
    return 0;
}

