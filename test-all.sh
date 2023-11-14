#!/bin/bash
start_dir=$(pwd)
for dir in compress decompress; do
  for alg in lzo lz4; do
    for script in test-ndp.sh test-cpu.sh; do
      script_path="$dir/$alg/$script"
      if [[ -f "$script_path" ]]; then
        (cd "$start_dir/$dir/$alg" && bash ./"$script" &)
      else
        echo "Error: Script $script_path does not exist or is not executable."
      fi
    done
  done
done

wait
echo "All scripts have completed."