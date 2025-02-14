#!/bin/bash
#PBS -S /bin/bash
#PBS -N beamform
#PBS -l nodes=1:gpus=1
#PBS -l walltime=00:05:20
#PBS -q batch 

# Ultrasound Beamforming job submission script for EECS 570.

INPUT_SIZE=64

USERDIR=/n/typhon/data1/home/$USER/eecs570_p1
QUEUE_FILE="/n/typhon/data1/PA1_Submissions.txt"

#!/bin/bash

# Check if the first argument equals "E"
if [ "$1" = "" ]; then

  let "USE_LOCAL_MIC_NUMBER=$RANDOM % 5"
  echo "Running interactive job on mic$USE_LOCAL_MIC_NUMBER, use 'sh 570_submit.sh E' to run a job exclusively on a Xeon Phi"
  micnativeloadex beamform.mic -t 300 -d $USE_LOCAL_MIC_NUMBER -a "$INPUT_SIZE" 
  micnativeloadex solution_check.mic -t 30 -d $USE_LOCAL_MIC_NUMBER -a "$INPUT_SIZE"

elif [ "$1" = "E" ]; then

    # Check if student PA1 directory exists
  if [ ! -d "$USERDIR" ]; then
      echo "Error: Directory $USERDIR does not exist."
      exit 1
  fi

  # Check if source file exists
  source_file="$USERDIR/beamform.c"
  if [ ! -f "$source_file" ]; then
      echo "Error: beamform.c file not found for $USER in $USERDIR"
      continue
  fi

  # Compile the source code
  #icc -o "$USERDIR/beamform.mic" -mmic "$source_file" 2>&1
  icc -mmic -lpthread -o "$USERDIR/beamform.mic" "$source_file" -lm 2>&1
  if [ $? -ne 0 ]; then
      echo "Error: beamform.c compilation failed."
      exit 1
  fi

  # Check solution_check.c exists
  solution_check_file="$USERDIR/solution_check.c"
  if [ ! -f "$solution_check_file" ]; then
      echo "Error: solution_check.c file not found for $USER in $USERDIR"
      continue
  fi

  # Compile the solution_check code
  #icc -o "$USERDIR/solution_check.mic" -mmic "$solution_check_file" 2>&1
  icc -mmic -lpthread -o "$USERDIR/solution_check.mic" "$solution_check_file" -lm 2>&1
  if [ $? -ne 0 ]; then
      echo "Error: solution_check.c compilation failed."
      exit 1
  fi

  # Append username to queue.txt if compilation succeeded
  flock -x "$QUEUE_FILE" bash -c "echo \"$USER\" >> \"$QUEUE_FILE\""
  echo "Successfully compiled & queued $USER's code, waiting to run."

fi


