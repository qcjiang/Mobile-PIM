#include <iostream>
#include <map>
#include <vector>
#include <unistd.h>
#include "ZSwapTest.h"
#include "../../zsim_hooks.h"
#define DEFAULT_SLOT_SIZE 4 * 1024 // 4 KB

char *data_filename;
char *trace_filename;
CompressionAlgorithm *compr_alg = NULL;
unsigned long slot_size = DEFAULT_SLOT_SIZE;

// parse the input arguments
int parse_args(int argc, char *argv[])
{
    int opt;
    while((opt = getopt(argc, argv, "c:s:f:t:")) != -1) {
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

int main(int argc, char *argv[])
{
    int r;
    std::map<int, ZRamBufferSlot *> slot_tree; // we use the pfn of a page as the key to find the corresponding slot in this tree
    std::vector<ZRamBufferSlot *> slot_list; // store all the slots

    parse_args(argc, argv);

    SwapoutTraceReader traceReader(trace_filename, data_filename);

    // Step 1. load trace from the trace file
    r = traceReader.LoadTrace(); // load trace from the trace file
    if(r != 0) {
        printf("Error loading trace\n");
        return 1;
    }

    // Step 2. convert the trace data into data pages
    std::vector<DataPage *> dataPages = traceReader.GetAllDataPages(); // get all the data pages from the trace file
    std::cout<< "Total Page Size is : " << dataPages.size() << std::endl;
    

    // Step3. insert all data pages into slots to simulaet zswap
    // this step includes page compression and slot insertion
    
    ZRamBufferSlot *current_slot = new ZRamBufferSlot(slot_size);
    zsim_begin();
	// zsim_roi_begin();
    for(int i = 0; i < dataPages.size(); i++) {
        DataPage *dataPage = dataPages[i];
        DataPage *compressedPage = compr_alg->CompressDataPage(dataPage);  // compress the data page
        if(compressedPage == nullptr) {
            std::cout << "Error: cannot compress data page" << std::endl;
            return 1;
        }

        bool ret = current_slot->InsertCompressedDataPage(compressedPage); // the function tries to insert the compressed page into the slot
        if(ret == false) { 
            // std::cout << "current slot size is : " << current_slot->getStoredPageCnt() << std::endl;
            slot_list.push_back(current_slot);
            current_slot = new ZRamBufferSlot(slot_size); // if the current slot is full, we need to create a new slot
        } else {
            slot_tree[dataPage->meta.pfn] = current_slot; // if the pfn is already in the tree, update it but not delete it because other pfn would share the same slot
        }

        delete compressedPage;
    }
    zsim_end();
    // zsim_roi_end();

    if(current_slot->getStoredPageCnt() > 0) {
        slot_list.push_back(current_slot);
    }

    std::cout << "total slot cnt : " << slot_list.size() << std::endl;
    std::cout << "last slot size is : " << current_slot->getStoredPageCnt() << std::endl;

    // we need to release resources in some steps ... but when the program exits, the OS will do it for us ... 
    // we just make sure that there is not memory leak during the execution of the program
    return 0;
}

