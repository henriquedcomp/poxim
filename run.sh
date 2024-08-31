#!/bin/sh

gcc henriquesouza_202300061699_poxim2.c -o henriquesouza_202300061699_poxim2

if [ $? -eq 0 ]; then
    ./henriquesouza_202300061699_poxim2 input.hex output.out
else
    echo "Erro na compilação. Não foi possível executar o programa."
fi