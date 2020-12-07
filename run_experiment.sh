# DATASETNAME="usps"
# DATASETNAME="satimage.scale"
# DATASETNAME="dna.scale"
# DATASETNAME="letter.scale"
# DATASETNAME="pendigits"
# DATASETNAME="news20.scale"
# DATASETNAME="sector.scale"
DATASETNAME="mnist.scale"
# DATASETNAME="cifar10"
# DATASETNAME="toy"

REG=1
# REG=0.000000004
ITER=1
FILENAME="results/${DATASETNAME}_RC"
echo $DATASETNAME > $FILENAME
for i in {1..7}
  do
    echo $ITER
    ./train -s 30 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME
    ./predict data/$DATASETNAME.t $DATASETNAME.model output >> $FILENAME
    ITER=$((2*$ITER))
  done



ITER=1
FILENAME="results/${DATASETNAME}_Shark"
echo $DATASETNAME > $FILENAME
for i in {1..7}
  do
    echo $ITER
    ./train -s 31 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME
    ./predict data/$DATASETNAME.t $DATASETNAME.model output >> $FILENAME
    ITER=$((2*$ITER))
  done
