#!/bin/bash
#PBS -S /bin/bash
#PBS -N beamform
#PBS -l nodes=1:gpus=1
#PBS -l walltime=00:05:20
#PBS -q batch 

# Ultrasound Beamforming job submission script for EECS 570.

INPUT_SIZE=16

if [[ $PBS_JOBID == "" ]] ; then

  # Not running under PBS
  let "USE_LOCAL_MIC_NUMBER=$RANDOM % 6"
  echo "Running interactive job on mic$USE_LOCAL_MIC_NUMBER, use 'qsub 570_pbs_submit.sh' to submit a batch job to penguin."
  micnativeloadex beamform.mic -t 300 -d $USE_LOCAL_MIC_NUMBER -a "$INPUT_SIZE" 
  micnativeloadex solution_check.mic -t 30 -d $USE_LOCAL_MIC_NUMBER -a "$INPUT_SIZE" 

else

  # Running batch job under PBS
  HOST=`cat $PBS_NODEFILE`
  MICNUM=$(cat $PBS_GPUFILE | cut -c27-)
  USERDIR=/n/typhon/data1/home/$USER/eecs570_p1
  OUTFILE=$USERDIR/$PBS_JOBID.stdout
  echo "I'm running on: $HOST mic$MICNUM" > $OUTFILE
  # Launching job to MIC
  # timeout set to 5 minutes
  micnativeloadex $USERDIR/beamform.mic -t 300 -d $MICNUM -a "$INPUT_SIZE" >> $OUTFILE
  micnativeloadex $USERDIR/solution_check.mic -t 30 -d $MICNUM -a "$INPUT_SIZE" >> $OUTFILE

fi

