#!bin/bash
file="$1"
../../mcpat/mcpat -infile "./$file" -print_level 5 > "./$file.mcpat"