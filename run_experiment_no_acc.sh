# DATASETNAME="usps"
# DATASETNAME="satimage.scale"
# DATASETNAME="dna.scale"
# DATASETNAME="letter.scale"
# DATASETNAME="pendigits"
# DATASETNAME="news20.scale"
# DATASETNAME="sector.scale"
# DATASETNAME="mnist.scale"
# DATASETNAME="cifar10"
# DATASETNAME="toy"
# DATASETNAME="rcv1" # REG = 1, 32 ITER converges to optimality
DATASETNAME="aloi.scale" # REG = 0.01, 32 ITER runs in about 5 minutes

# REG=1
REG=.01
# REG=.01
# REG=0.000000004
ITER=32
FILENAME="results/${DATASETNAME}_RC"
echo $DATASETNAME > $FILENAME
echo $ITER
./train -s 30 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME



FILENAME="results/${DATASETNAME}_Shark"
echo $DATASETNAME > $FILENAME
echo $ITER
./train -s 31 -c $REG -t $ITER data/$DATASETNAME >> $FILENAME
