This repo is to simulate the operations of zswap based on traces collected from real application workloads on real mobile devices.

The traces can be downloaded from https://portland-my.sharepoint.com/:f:/g/personal/riweipan-c_my_cityu_edu_hk/EpIcoT5xlQBHmvERtvaFyIwBaHtzhvQvvmCbiQw3mDcYVQ?e=kOIrx4 

You could download two folders, one is the traces of swap out operations, and the other is the traces of swap in operations.

To avoid the influence of the randomness during collecting traces, we repeated 3 times for each trace, so you will see files with suffixes of `swap1`, `swap2`, and `swap3` in the folder.

Based on these traces, we could replay it to simulate the operations of zswap. Before replaying, you can compile the source code with the command below:
```shell
g++ EvalWriteCompressedFile.cpp lzo/minilzo.c lz4/lz4.c -o EvalWriteCompressedFile
g++ EvalReadCompressedFile.cpp lzo/minilzo.c lz4/lz4.c -o EvalReadCompressedFile
```

To illustrate how use these two program, we use a the trace of `swap3` as an example.

The `EvalWriteCompressedFile` is to simulate the swapout operations. Specifically, it reads the operations records of zswap from `swapout_trace_swap3.txt.meta` and the page data from `swapout_trace_swap3.txt.data`. Then, it compresses each page and stores it in a key-value tree. You could use this command to run it:
```shell
./EvalWriteCompressedFile -f ./trace_data_swapout/swapout_trace_swap3.txt.data -t ./trace_data_swapout/swapout_trace_swap3.txt.meta
```

The `EvalReadCompressedFile` is to simulate the swapin operations. Specifically, it firstly repeats the same steps as `EvalWriteCompressedFile` to create a key-value tree with compressed pages. Then, it follows the order of opeartion records in `swapin_trace_swap3.txt.meta` to search the corresponding page in the key-value tree and decompress it.
```shell
./EvalReadCompressedFile -f ./trace_data_swapout/swapout_trace_swap3.txt.data -t ./trace_data_swapout/swapout_trace_swap3.txt.meta -r ./trace_data_swapin/swapin_trace_swap3.txt.meta
```

You could refer the source codes for more information.




