#include <iostream>
#include <fstream> 
#include <cstdint> 
#include <map>
#include <vector>
#include <unistd.h>
#include "ZSwapTest.h"
#include <algorithm> 
#include <cmath> 




using namespace std; 

#define DEFAULT_SLOT_SIZE 4 * 1024 // 4 KB

char *data_filename;
char *trace_filename;
char *replay_filename;
CompressionAlgorithm *compr_alg = NULL;
unsigned long slot_size = DEFAULT_SLOT_SIZE;

int compalg = 0 ; //Flag 0 for both lz0, lz4. 1 for bd0 



// parse the input arguments
int parse_args(int argc, char *argv[])
{
	int opt;
	while((opt = getopt(argc, argv, "c:s:f:t:r:")) != -1) {
		switch(opt) {
			case 'c':
				if(strcmp(optarg, "lzo") == 0) {
					compr_alg = new LZOCompressionAlgorithm();
					compalg = 0;		
				} else if(strcmp(optarg, "lz4") == 0) {
					compr_alg = new LZ4CompressionAlgorithm();
					compalg = 0; 		
				}

				else if(strcmp(optarg, "bd0") == 0) {
					//BD0 compression algorithm. 
					compalg = 1; 		
				}

				else {
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

void CopyXPagesToVector(const std::vector<DataPage*>& dataPages, std::vector<uint32_t>& targetVector, uint32_t pagesize, uint32_t inp_X, uint32_t start_page) {
	size_t totalSize = inp_X * pagesize;

	// Resize the target vector to accommodate the data
	//targetVector.resize(totalSize / sizeof(uint32_t) + 1, 0);
	targetVector.resize(totalSize / sizeof(uint32_t) , 0); //assuming everything fits. 

	// Copy data from the first 3 pages to the target vector
	size_t offset = 0;
	for (size_t i = start_page; i < std::min(dataPages.size(), static_cast<size_t>(start_page + inp_X)); ++i) {
		const DataPage* dataPage = dataPages[i];
		if (dataPage->buf != nullptr) {
			size_t copySize = std::min(dataPage->meta.page_size, totalSize - offset);
			std::copy(dataPage->buf, dataPage->buf + copySize, reinterpret_cast<char*>(targetVector.data()) + offset);
			offset += copySize;
		}
	}
}







void BaseDeltaComp(const uint32_t  *data, size_t size,uint32_t inp_X, uint32_t len_base,
		std::vector<uint32_t>& base_array,
		std::vector<uint32_t>& val_array,
		std::vector<uint32_t>& baseid_array,
		uint32_t num_bases) {
	//	const uint32_t* data_uint32 = static_cast<const uint32_t*>(data);

	uint32_t unique_base = 0;
	std::map<uint32_t, uint32_t> base_to_id;
	std::map<uint32_t, size_t> base_count_map;

	for (size_t i = 0; i < size / sizeof(uint32_t); ++i) {
		uint32_t base_data = data[i] >> (8 - len_base) * 4;
		uint32_t val_data = data[i] & 0xFF;

		val_array.push_back(val_data);

		if (base_array.empty()) {
			base_array.push_back(base_data);
			baseid_array.push_back(0);
			base_to_id[base_data] = 0;
			base_count_map[base_data] = 1; // Initialize count for the base
			unique_base = 1;
		} else {
			auto base_id_iter = base_to_id.find(base_data);
			if (base_id_iter != base_to_id.end()) {
				baseid_array.push_back(base_id_iter->second);
				base_count_map[base_data]++; // Increment count for the existing base
			} else {
				base_array.push_back(base_data);
				baseid_array.push_back(unique_base);
				base_to_id[base_data] = unique_base;
				base_count_map[base_data] = 1; // Initialize count for the new base
				unique_base++;
			}
		}
	}

	std::vector<std::pair<uint32_t, size_t>> sorted_base_count(base_count_map.begin(), base_count_map.end());
	std::sort(sorted_base_count.begin(), sorted_base_count.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });


	size_t count = 0 ;
	size_t compressed_count = 0 ;
	std::vector <uint32_t > top_bases  ; 


	std::for_each(sorted_base_count.begin(), sorted_base_count.begin() + num_bases,
			[&count,&top_bases,&compressed_count](const auto& pair) {
			std::cout << "Base: " <<"0x" <<std::hex << pair.first << ",No of Values: " << pair.second << std::endl;
			top_bases.push_back(pair.first); 
			compressed_count = compressed_count + pair.second ; //no of values that will be compressed. 
			++count;
			});

	//Printing the top bases. 
	/*
	   for (int i = 0 ;  i < num_bases ; i++)
	   {
	   std::cout <<"top bases: "<<top_bases[i] << std::endl; 
	   }*/ 

	//Identified the top_bases.
	//Estimate the buffer size to be compressed.

	uint32_t total_vals = size/sizeof(uint32_t); 	

	std::cout <<"Number of values in the file: "<< total_vals <<" no. of values to be compressed: "<< compressed_count <<std::endl; 	 

	//Total size in bits.

	uint32_t logNbases = static_cast<uint32_t>(log2(static_cast<double>(num_bases))) ;
	uint32_t total_bits = 0 ;

	// 1b flag for each value.
	// num_bases*24
	// 32 bits: no. of compressed values. 
	// num of bits for compressed values : (logN + 8) 
	// num of bits for non compressed values: (32b) 



	total_bits = total_vals*1 + (32 - ( 8 - len_base )*4 )*num_bases +  ((8-len_base)*4 + logNbases)*compressed_count 
		+ (total_vals - compressed_count)*32 + 32 ;   


	uint32_t rounded_bits = 0 ;

	rounded_bits = std::ceil(total_bits / 8.0) * 8;

	std::cout<<"Total bits: "<<total_bits <<" Rounded_bits: "<<rounded_bits <<std::endl; 

	uint32_t compressed_buffer_size = rounded_bits/8 ; 

	uint32_t num_pages_needed = std::ceil (compressed_buffer_size/4096.0) ; 	 	

	cout <<"Compressed buffer size:  "<<compressed_buffer_size <<" Num of pages needed: "<<num_pages_needed <<std::endl ; 

	if ( num_pages_needed == inp_X)
		cout<<"No compression needed: " <<endl ;
	else
		cout<<"Performing BaseDelta compression: "<<endl; 	 


	uint32_t BD_comp_buffersize = num_pages_needed*4096 ; 


	//Forming the base buffer. 
	uint32_t  base_size = ((32 - ( 8 - len_base )*4 )/8) ; //24 bit, (3 byte) and always should be multiple of 8. 
	char *base_buffer = new char  [num_bases*base_size] ;

	std::cout <<"base buffer size: "<< base_size*num_bases <<std::endl;

	for (int i = 0 ; i < num_bases ; i++)
	{
		for (int j = 0 ; j < base_size ; j++) {

			base_buffer[i*base_size + j ] =  ((top_bases[i] >> 8*j) & 0xFF)  ;
		}
	}

	//memcpy (bd_compressed_buffer,base_buffer, num_bases*base_size);
	//Copy the number of compressed values to bd_compressed_buffer. 
	// memcpy (bd_compressed_buffer + num_bases*size , &compressed_count,4 ); //32b for total number of compressed vals, file end with this number.   

	uint32_t bit_count = 0;
	uint32_t byte_transfer = 0;
	uint32_t byte_count = 0;

	uint32_t data_bits = 0 ;
	uint32_t rounded_data_bits = 0 ;

	//Bits corresponding to just data. 
	data_bits  = total_vals*1  +  ((8-len_base)*4 + logNbases)*compressed_count + (total_vals - compressed_count)*32  ;
	rounded_data_bits = std::ceil(data_bits / 8.0) * 8;

	uint32_t data_buffer_size = rounded_data_bits/8 ; 
	std::cout <<"Data bits: "<<data_bits <<" Rounded data bits: "<<rounded_data_bits <<" Data buf size: "<<data_buffer_size << std::endl;

	char *data_buffer = new char  [data_buffer_size] ;
	uint32_t f_count = 0 ; 
	
	uint64_t stream_val = 0 ; //64 bits to fit everything.  
	uint64_t transfer_stream_val = 0 ; //64 bits to fit everything.  

	uint32_t base_index = 0; 
	uint32_t flag = 0 ;
	uint32_t offset = 0;
	uint32_t shift = 0; 
	uint8_t flag1 = 0; 
	uint64_t mask = 0 ; 

	uint8_t v_byte[2] ;
	uint16_t v1_byte ; //Fitting the assumption  ( 8 + 1 + logNbases  < 16 )


	for (size_t i = 0; i < size / sizeof(uint32_t); ++i) {

		uint32_t b_data = data[i] >> (8 - len_base) * 4;
		uint32_t v_data = data[i] & 0xFF;

		auto it = std::find(top_bases.begin(), top_bases.end(), b_data);
		

		if (it != top_bases.end()) {
			
			//Val compressed:
			//Total size: 1b (flag) + log   8b (v_data)  			
			base_index = std::distance (top_bases.begin(), it) ; 
		//	std::cout <<" Base index: "<<base_index<<" val_data : "<<v_data<<std::endl ;
		
			offset = bit_count ; 
			//Write new bits to offset.
					
			shift = 8 + logNbases + 1 ;
			bit_count = bit_count  + shift ; 		
			flag = 1 ; 
			byte_transfer = bit_count / 8 ; //No of bytes to transfer		
			bit_count = bit_count % 8 ;  //No of bits still remaining in the buffer. 
	
			//stream buf currently has offset bits (old bit count).
			// New bits: 8 + logNbases + 1 (shift)
			// Current bits are shifted -> (shift) towards left. 
			//assumption: 8 + logN + 1 < 16 (fits in 2 bytes). 
			
		//	std::cout<<"Flag: "<<flag <<" Offset: "<<offset  << " Bit count: "<<bit_count << " Byte transfer: "<< byte_transfer <<" Byte count: "<<byte_count <<endl; 

			v_byte[0] = (uint8_t) v_data ; //8 value bits are copied.
			v_byte[1]  = (uint8_t) ( base_index & 0xFF)  ; 
			uint8_t flag1 = 1 << logNbases ;
			//appending flag. 
			v_byte[1] = flag1 | v_byte[1]; 
			v1_byte = (v_byte[1] << 8 ) | v_byte[0] ; 
		
			//std::cout <<"Base index: "<<base_index <<" v_data  " << v_data << " v1_byte  " <<v1_byte <<std::endl;  

			stream_val =  (stream_val << shift ) | v1_byte ; 		
			transfer_stream_val = stream_val >> bit_count ; //These bytes will be transfered to buffer. 
			mask = ( 1ULL << bit_count )  -1 ; 	
			stream_val = stream_val & mask ; //preserve the remaining bits. 

			for (uint32_t k = 0 ; k < byte_transfer ; k ++)
			{
				
				data_buffer[byte_count + k ] = (((transfer_stream_val) >> 8*(byte_transfer - k - 1)) & 0xFF); 

			}

			byte_count = byte_count + byte_transfer ; 		

		//	std::cout <<"no of vals transferred: "<<byte_transfer << " value transferred: "<<transfer_stream_val << " bits remain: "<<bit_count << std::endl ;

		//	std::cout << "Base: "<< b_data <<" Value: "<<v_data << " Base Index: "<< base_index << " v1 byte " << v1_byte <<" transfer stream val: " << transfer_stream_val << " stream val: "<<stream_val <<std::endl ; 		

		}
	       
		else {
		//	std::cout <<" Base value: "<<b_data<<" val_data : "<<v_data<<std::endl ;
			flag = 0 ; 
			offset = bit_count ; 
			shift = 32 + 1 ; 
			bit_count = bit_count + shift ; 
			byte_transfer = bit_count / 8 ; //No of bytes to transfer		
			bit_count = bit_count % 8 ; 
		//	std::cout<<"Flag: "<<flag <<" Offset: "<<offset  << " Bit count: "<<bit_count << " Byte transfer: "<< byte_transfer<<" Byte Count: "<<byte_count <<endl; 
			//appending 32 bit data to stream val.
			//Flag is automatically set to 0 when we shift 33 bits. 

		
				
			stream_val =  (stream_val << shift ) | data[i]  ; 		
			transfer_stream_val = stream_val >> bit_count ; //These bytes will be transfered to buffer. 
                        mask = ( 1ULL << bit_count )  -1 ;
                        stream_val = stream_val & mask ; //preserve the remaining bits. 

                        for (uint32_t k = 0 ; k < byte_transfer ; k ++)
                        {

                                data_buffer[byte_count + k ] = (((transfer_stream_val) >> 8*(byte_transfer - k - 1)) & 0xFF);

                        }
		
		//	std::cout <<"no of vals transferred: "<<byte_transfer << " value transferred: "<<transfer_stream_val << " bits remain: "<<bit_count << std::endl ;
		//	std::cout << "Base: "<< b_data <<" Value: "<< v_data  << " stream val: "<<stream_val <<std::endl ; 		

			byte_count = byte_count + byte_transfer ; 		
			
		
		}		


	}
	std::cout <<" No of bytes transferred: "<< byte_count << " bits remaining in buffer: "<<bit_count << " value remaining in buffer: "<< stream_val <<std::endl ;
		

	//Handling last byte. 
	char last_val = ((stream_val | 0xFF) << (8 - bit_count));
	data_buffer[data_buffer_size- 1] = last_val; 	

	delete [] base_buffer ;
	delete [] data_buffer ;



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

	std::cout<< "Total No. of pages : " << dataPages.size() << std::endl;
	ZRamBufferSlot *current_slot = new ZRamBufferSlot(slot_size);


	//LZ0 / LZ4 	
	if (compalg == 0)  {	
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

	}	
	//BD0 
	else if (compalg == 1 )
	{	  
		//Copy the contents of all the pages into a single buffer. 

		size_t totalBufferSize = 0;
		for (const DataPage* dataPage : dataPages) {
			totalBufferSize += dataPage->meta.page_size;
		}


		//compBuffer format.
		//1) Count the total number of unique bases and find number of values associated for each base.
		//2) Sort the bases, and find the first 8 bases and values associated with them. 
		//3) Compress and write to the output to buffer.
		//4) Fill the output buffer to slots. 

		std::cout <<"Size of buffer: "<< totalBufferSize <<std::endl ; 		 	

		uint32_t pagesize = totalBufferSize/dataPages.size(); 	

		uint32_t inp_X = 3; //No of input pages to compress. 	
		std::vector<uint32_t> data ; 
		uint32_t start_page = 0 ; //page offset.  

		CopyXPagesToVector(dataPages, data,pagesize, inp_X, start_page);

		// Define len_base
		uint32_t len_base = 6;  //6 bits. 

		// Initialize result vectors


		std::vector<uint32_t> base_array;
		std::vector<uint32_t> val_array;
		std::vector<uint32_t> baseid_array;
		uint32_t  num_bases =  8 ; 



		// Process the data
		BaseDeltaComp(data.data(), data.size() * sizeof(uint32_t), inp_X, len_base, base_array, val_array, baseid_array, num_bases);

		// Print the results as an example:
		std::cout << "Num of unique bases: " << base_array.size() << std::endl;
		//		std::cout << "Num of bytes read: " << val_array.size() << std::endl;

		std::ofstream BD0_compfile ("BD0_comp.bin",std::ios::binary); 

		uint32_t logNbases = static_cast<uint32_t>(log2(static_cast<double>(num_bases))) ;
		std::cout <<"logNbases : "<<logNbases <<std::endl; 	

	}
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
	std::cout << "Finished!" << std::endl;


	// we need to release resources in some steps ... but when the program exits, the OS will do it for us ... 
	// we just make sure that there is not memory leak during the execution of the program
	return 0;
}

