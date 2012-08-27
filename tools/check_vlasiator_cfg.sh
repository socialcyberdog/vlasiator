#!/bin/bash

mpirun_cmd="aprun -n 1"
vlasiator=$1
cfg=$2





if [ $# -ne 2 ] 
then
    cat    <<EOF
Prints out differences between parameters in a cfg file and the options that the vlasiator executable understands.

Usage: $0 vlasiator_executable cfg_file

EOF
    exit
fi

if [ ! -x $vlasiator ] 
then    
    echo "ERROR: Vlasiator executable $vlasiator does not exist or is not executable"
    exit
fi


if [ ! -e $cfg ]
then
    echo "ERROR: cfg file $cfg does not exist"
    exit
fi



#long one-liner. First remove comments, then add prefix to each name and only print if line is not empty
cat $cfg |  grep -v "^[ ]*#" |gawk '{if ( $1 ~ /\[/) {prefix=substr($1,2,length($1)-2);prefix=sprintf("%s.",prefix);} else if(NF>0) printf("%s%s\n",prefix,$0)}' > .cfg_variables

$mpirun_cmd $vlasiator --help|grep "\-\-" | sed 's/--//g'  > .vlasiator_variables

cat .vlasiator_variables | gawk '{print $1}'|sort -u >.vlasiator_variable_names
cat .cfg_variables | gawk '{print $1}'|sort -u >.cfg_variable_names


echo "------------------------------------------------------------------------------------------------------------" 
echo "               Vlasiator options                              |             CFG  options" 
echo "------------------------------------------------------------------------------------------------------------" 

diff --side-by-side --suppress-common-lines .vlasiator_variable_names .cfg_variable_names
echo "------------------------------------------------------------------------------------------------------------"

rm .cfg_variables .cfg_variable_names .vlasiator_variables .vlasiator_variable_names
