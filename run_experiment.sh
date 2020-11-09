# DATASETNAME="usps"
# DATASETNAME="satimage.scale"
DATASETNAME="dna.scale"
# DATASETNAME="letter.scale"
# DATASETNAME="news20.scale"
# DATASETNAME="mnist.scale"

ITER=1
FILENAME="results/${DATASETNAME}_RC"
REG=1
echo $DATASETNAME > $FILENAME
for i in {1..13}
  do
    echo $ITER

    ./train -s 30 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME
    ./predict data/$DATASETNAME.t $DATASETNAME.model output >> $FILENAME
    ITER=$((2*$ITER))
  done



ITER=1
FILENAME="results/${DATASETNAME}_Shark"
echo $DATASETNAME > $FILENAME
for i in {1..15}
  do
    echo $ITER

    ./train -s 31 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME
    ./predict data/$DATASETNAME.t $DATASETNAME.model output >> $FILENAME
    ITER=$((2*$ITER))
  done
